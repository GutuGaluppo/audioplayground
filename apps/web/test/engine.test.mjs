// The browser host as the AudioWorklet runs it, without a browser: the compiled module, loaded by
// the same engine.js. Build it first (tools/web/build.sh).
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';

import { WasmEngine } from '../../../ui/public/engine/engine.js';

const wasmPath = fileURLToPath(new URL('../../../ui/public/engine/ap_web.wasm', import.meta.url));
const module = await WebAssembly.compile(await readFile(wasmPath));

const create = (options) =>
  WasmEngine.create(module, {
    sampleRate: 48000,
    blockSize: 128,
    ...options,
  });
const send = (engine, type, payload = {}) => engine.intent({ type, payload });

function render(engine, seconds, chunk = 128) {
  const frames = Math.round(seconds * engine.sampleRate);
  const out = [new Float32Array(frames), new Float32Array(frames)];
  for (let done = 0; done < frames; done += chunk) {
    const n = Math.min(chunk, frames - done);
    const left = new Float32Array(n);
    const right = new Float32Array(n);
    engine.process(left, right);
    out[0].set(left, done);
    out[1].set(right, done);
  }
  return out;
}

const energy = (channel) => channel.reduce((sum, x) => sum + x * x, 0);
const typesOf = (events) => new Set(events.map((e) => e.type));
const last = (events, type) => events.filter((e) => e.type === type).at(-1)?.payload;

test('app.ready describes the whole starter song', async () => {
  const engine = await create();
  assert.equal(send(engine, 'app.ready'), true);
  const events = engine.takeEvents();
  for (const type of [
    'engine.status',
    'transport.state',
    'transport.position',
    'param.value',
    'history.state',
    'project.state',
    'instrument.state',
    'drums.kit',
    'drums.pad',
    'timeline.state',
    'project.assets',
    'export.state',
    'accompaniment.state',
  ])
    assert.ok(typesOf(events).has(type), `missing ${type}`);

  const timeline = last(events, 'timeline.state');
  assert.equal(timeline.tracks.length, 2); // a drum track with the beat and an empty synth track
  assert.ok(timeline.tracks[0].clips[0].notes.length > 0);
  assert.equal(last(events, 'engine.status').deviceName, 'Web Audio');
  assert.equal(events.filter((e) => e.type === 'param.value').length >= 10, true);
  assert.deepEqual(engine.takeEvents(), []); // nothing left over
});

test('the starter beat plays, with meters and a moving playhead', async () => {
  const engine = await create();
  send(engine, 'app.ready');
  engine.takeEvents();
  assert.equal(energy(render(engine, 0.5)[0]), 0); // not playing yet: silence

  send(engine, 'transport.play');
  const audio = render(engine, 2);
  assert.ok(energy(audio[0]) > 0.01, 'the beat is audible');
  assert.ok(energy(audio[1]) > 0.01);
  assert.ok(audio[0].every(Number.isFinite));

  const events = engine.takeEvents();
  assert.equal(last(events, 'transport.state').playing, true);
  assert.ok(
    Math.max(...events.filter((e) => e.type === 'engine.meters').map((e) => e.payload.peak)) > 0.01,
  );
  const positions = events
    .filter((e) => e.type === 'transport.position')
    .map((e) => e.payload.ticks);
  assert.ok(positions.length > 3);
  assert.ok(positions.at(-1) > positions[0]);

  send(engine, 'transport.stop');
  render(engine, 0.1); // the audio thread takes the request at its next block, then the page is told
  assert.equal(last(engine.takeEvents(), 'transport.state').playing, false);
});

test('a played note sounds, with the synth chosen from the page', async () => {
  const engine = await create();
  send(engine, 'app.ready');
  send(engine, 'instrument.select', { instrument: 0 });
  send(engine, 'note.on', { note: 60, velocity: 0.8 });
  const audio = render(engine, 0.5);
  assert.ok(energy(audio[0]) > 0.001);
  send(engine, 'note.off', { note: 60 });
  send(engine, 'note.allOff');
  assert.equal(last(engine.takeEvents(), 'instrument.state').instrument, 0);
});

test('edits come back as events, and undo and redo work', async () => {
  const engine = await create();
  send(engine, 'app.ready');
  engine.takeEvents();

  send(engine, 'param.set', { id: 'synth.cutoff', value: 1500, gesture: 0 });
  let events = engine.takeEvents();
  assert.equal(
    events.find((e) => e.type === 'param.value' && e.payload.id === 'synth.cutoff').payload.value,
    1500,
  );
  assert.equal(last(events, 'history.state').canUndo, true);

  send(engine, 'drums.setStep', {
    clip: 0,
    pad: 5,
    step: 3,
    on: true,
    gesture: 0,
  });
  events = engine.takeEvents();
  const clip = last(events, 'timeline.state').tracks[0].clips[0];
  assert.ok(clip.notes.some((n) => n.pitch === 36 + 5 && n.start === 3 * 240));

  send(engine, 'edit.undo');
  events = engine.takeEvents();
  assert.ok(
    !last(events, 'timeline.state').tracks[0].clips[0].notes.some(
      (n) => n.pitch === 41 && n.start === 720,
    ),
  );
  send(engine, 'edit.undo');
  events = engine.takeEvents();
  assert.equal(
    events.find((e) => e.type === 'param.value' && e.payload.id === 'synth.cutoff').payload.value,
    4000,
  );
  assert.equal(last(events, 'history.state').canRedo, true);
  send(engine, 'edit.redo');
  assert.equal(
    engine.takeEvents().find((e) => e.type === 'param.value' && e.payload.id === 'synth.cutoff')
      .payload.value,
    1500,
  );
});

test('a refused change is echoed so the page never keeps a value the core did not accept', async () => {
  const engine = await create();
  send(engine, 'app.ready');
  engine.takeEvents();
  send(engine, 'param.set', { id: 'synth.cutoff', value: 4000, gesture: 0 }); // already there
  assert.equal(engine.takeEvents().find((e) => e.type === 'param.value').payload.value, 4000);
  send(engine, 'track.rename', { track: 999, name: 'x' }); // no such track
  assert.ok(typesOf(engine.takeEvents()).has('timeline.state'));
});

test('malformed and oversized messages are dropped, and what the browser lacks says so', async () => {
  const engine = await create();
  send(engine, 'app.ready');
  engine.takeEvents();

  assert.equal(send(engine, 'track.add', { kind: 99 }), false);
  assert.equal(send(engine, 'track.add', { kind: 1, extra: true }), false);
  assert.equal(send(engine, 'no.such.intent'), false);
  assert.equal(engine.intent({ type: 'transport.setTempo', payload: { bpm: 5000 } }), false);
  assert.equal(
    send(engine, 'drums.setPattern', {
      clip: 0,
      pads: new Array(1000).fill(1),
    }),
    false,
  );
  assert.equal(
    engine.intent({
      type: 'project.rename',
      payload: { name: 'x'.repeat(2 * 1024 * 1024) },
    }),
    false,
  );
  assert.deepEqual(engine.takeEvents(), []);

  assert.equal(send(engine, 'project.save'), true);
  const notice = last(engine.takeEvents(), 'app.notice');
  assert.match(notice.message, /not available in the browser/);

  // Text that is not JSON never reaches the codec.
  const text = new TextEncoder().encode('{"type": "track.add", "payload": {"kind": 1');
  new Uint8Array(engine.memory, engine.scratch, text.length).set(text);
  assert.equal(engine.exports.ap_intent(engine.scratch, text.length), 0);
});

test('unicode in names survives the round trip', async () => {
  const engine = await create();
  send(engine, 'app.ready');
  engine.takeEvents();
  send(engine, 'project.rename', { name: 'Canção — 曲 🎵' });
  assert.equal(last(engine.takeEvents(), 'project.state').name, 'Canção — 曲 🎵');
});

test('rendering is deterministic and does not depend on how the audio is pulled', async () => {
  const play = async (chunk) => {
    const engine = await create();
    send(engine, 'app.ready');
    send(engine, 'transport.play');
    return render(engine, 2, chunk);
  };
  const a = await play(128);
  const b = await play(128);
  const c = await play(500); // not a multiple of the engine's block
  assert.deepEqual(a[0], b[0]);
  assert.deepEqual(a[0], c[0]);
  assert.deepEqual(a[1], c[1]);
});

test('a long session stays bounded', async () => {
  const engine = await create();
  send(engine, 'app.ready');
  send(engine, 'transport.play');
  const before = engine.memory.byteLength;
  for (let second = 0; second < 30; second++) {
    render(engine, 1);
    engine.takeEvents();
  }
  assert.ok(engine.memory.byteLength <= before * 2, 'memory did not run away');
});

test('the engine refuses unreasonable settings', async () => {
  await assert.rejects(WasmEngine.create(module, { sampleRate: 100, blockSize: 128 }));
  await assert.rejects(WasmEngine.create(module, { sampleRate: 48000, blockSize: 0 }));
});

const projectOf = (engine) => {
  send(engine, 'app.ready');
  return engine.takeEvents();
};

test('a project is exported as the desktop file format and opens again, as it was', async () => {
  const first = await create();
  projectOf(first);
  send(first, 'project.rename', { name: 'Late night' });
  send(first, 'param.set', { id: 'synth.cutoff', value: 1500, gesture: 0 });
  send(first, 'drums.setStep', {
    clip: 0,
    pad: 7,
    step: 9,
    on: true,
    gesture: 0,
  });
  send(first, 'transport.setTempo', { bpm: 101 });
  const text = first.exportProject('2026-10-07T12:00:00Z');

  const file = JSON.parse(text);
  assert.equal(file.schemaVersion, 1);
  assert.equal(file.name, 'Late night');
  assert.equal(file.tempo, 101);
  assert.equal(file.createdAt, '2026-10-07T12:00:00Z');

  const second = await create();
  projectOf(second);
  second.takeEvents();
  assert.deepEqual(second.importProject(text, { hasLocation: true }), {
    ok: true,
  });
  const events = second.takeEvents();
  assert.equal(last(events, 'project.state').name, 'Late night');
  assert.equal(last(events, 'project.state').dirty, false);
  assert.equal(last(events, 'project.state').hasLocation, true);
  assert.equal(last(events, 'transport.state').bpm, 101);
  assert.equal(
    events.find((e) => e.type === 'param.value' && e.payload.id === 'synth.cutoff').payload.value,
    1500,
  );
  assert.ok(
    last(events, 'timeline.state').tracks[0].clips[0].notes.some(
      (n) => n.pitch === 43 && n.start === 9 * 240,
    ),
  );
  assert.equal(last(events, 'history.state').canUndo, false); // a fresh history, like the desktop

  // Exporting again gives the same project (only the update time moves).
  const again = JSON.parse(second.exportProject('2026-10-07T13:00:00Z'));
  assert.equal(again.createdAt, '2026-10-07T12:00:00Z');
  assert.equal(again.updatedAt, '2026-10-07T13:00:00Z');
  delete file.updatedAt;
  delete again.updatedAt;
  assert.deepEqual(again, file);

  // And it sounds the same.
  send(first, 'transport.play');
  send(second, 'transport.play');
  assert.deepEqual(render(first, 1)[0], render(second, 1)[0]);
});

test('unsaved changes are tracked until the page says the project was stored', async () => {
  const engine = await create();
  assert.equal(last(projectOf(engine), 'project.state').dirty, false); // the starter song is clean
  send(engine, 'param.set', { id: 'synth.cutoff', value: 900, gesture: 0 });
  assert.equal(last(engine.takeEvents(), 'project.state').dirty, true);

  engine.projectSaved(true);
  const saved = last(engine.takeEvents(), 'project.state');
  assert.equal(saved.dirty, false);
  assert.equal(saved.hasLocation, true);

  send(engine, 'edit.undo'); // back before the save: that is a change from what is stored
  assert.equal(last(engine.takeEvents(), 'project.state').dirty, true);

  send(engine, 'project.new');
  const fresh = engine.takeEvents();
  assert.equal(last(fresh, 'project.state').dirty, false);
  assert.equal(last(fresh, 'project.state').hasLocation, false);
  assert.equal(last(fresh, 'timeline.state').tracks.length, 0);
  assert.equal(last(fresh, 'history.state').canUndo, false);
});

test('a recovered project can be opened as unsaved', async () => {
  const source = await create();
  projectOf(source);
  const text = source.exportProject('2026-10-07T12:00:00Z');
  const engine = await create();
  projectOf(engine);
  engine.takeEvents();
  assert.deepEqual(engine.importProject(text, { dirty: true, hasLocation: false }), { ok: true });
  const state = last(engine.takeEvents(), 'project.state');
  assert.equal(state.dirty, true);
  assert.equal(state.hasLocation, false);
});

test('a project that cannot be trusted is refused with a reason, and the open one stays', async () => {
  const engine = await create();
  projectOf(engine);
  send(engine, 'project.rename', { name: 'Keep me' });
  engine.takeEvents();
  const good = JSON.parse(engine.exportProject('2026-10-07T12:00:00Z'));

  const refused = (text) => {
    const result = engine.importProject(text);
    assert.equal(result.ok, false, text.slice(0, 60));
    assert.ok(result.error.length > 0);
    assert.deepEqual(engine.takeEvents(), []); // nothing was announced: nothing changed
    return result.error;
  };
  refused('not json at all');
  refused('');
  assert.match(refused(JSON.stringify({ ...good, schemaVersion: 99 })), /newer/i);
  refused(JSON.stringify({ ...good, tempo: 'fast' }));
  refused(JSON.stringify({ ...good, tracks: 'many' }));
  refused(JSON.stringify({ ...good, surprise: true }));
  refused(JSON.stringify({ ...good, name: 'x'.repeat(5000) }));
  refused('['.repeat(200) + ']'.repeat(200));
  refused(JSON.stringify(good).slice(0, 40));
  refused('x'.repeat(17 * 1024 * 1024));

  send(engine, 'app.ready');
  assert.equal(last(engine.takeEvents(), 'project.state').name, 'Keep me');
});

test('a time that is not a time cannot reach the project file', async () => {
  const engine = await create();
  projectOf(engine);
  const file = JSON.parse(engine.exportProject('<script>alert(1)</script>'));
  assert.match(file.createdAt, /^\d{4}-\d\d-\d\dT/);
  assert.equal(file.createdAt.includes('<'), false);
});

// --- Audio files ----------------------------------------------------------------------------------

const RATE = 48000;
// A second of a steady tone at the level of `amplitude`, as the page would hand it over.
const tone = (seconds = 1, amplitude = 0.5, frequency = 440) =>
  Float32Array.from(
    { length: Math.round(seconds * RATE) },
    (_, i) => amplitude * Math.sin((2 * Math.PI * frequency * i) / RATE),
  );

const audioTrack = (events) => last(events, 'timeline.state').tracks.find((t) => t.kind === 0);
const freshWithAudioTrack = async () => {
  const engine = await create();
  send(engine, 'app.ready');
  send(engine, 'project.new'); // nothing but the audio
  send(engine, 'track.add', { kind: 0 });
  const events = engine.takeEvents();
  return { engine, track: audioTrack(events).id };
};
const sounds = (engine, seconds = 1.2) => {
  send(engine, 'transport.returnToStart');
  send(engine, 'transport.play');
  return render(engine, seconds);
};
const rms = (channel) => Math.sqrt(energy(channel) / channel.length);

test('an imported file becomes a clip of its own length, in one undo step, and plays', async () => {
  const { engine, track } = await freshWithAudioTrack();
  const samples = tone(1, 0.5);
  const result = engine.loadAudio({
    path: 'audio/11111111-aaaa.wav',
    sampleRate: RATE,
    channels: [samples],
    name: 'riff.wav',
    use: { kind: 'clip', track, ticks: 0 },
  });
  assert.deepEqual(result, { ok: true });

  const events = engine.takeEvents();
  const clip = audioTrack(events).clips[0];
  assert.equal(clip.start, 0);
  assert.equal(clip.length, 1920); // one second at 120 BPM is two beats
  assert.equal(last(events, 'history.state').undoLabel, 'Import audio');
  const asset = last(events, 'timeline.assets').assets.find((a) => a.id === clip.asset);
  assert.equal(asset.name, 'riff.wav');
  assert.equal(asset.loaded, true);
  assert.equal(asset.durationSeconds, 1);
  assert.equal(asset.overview.length, 512);
  assert.ok(Math.max(...asset.overview) > 0.45);
  const peaks = last(events, 'timeline.peaks');
  assert.equal(peaks.asset, clip.asset);
  assert.ok(peaks.data.length > 100);
  assert.equal(last(events, 'project.assets').assets[0].clips, 1);
  assert.equal(last(events, 'project.state').dirty, true);

  // It plays what was handed over, at the level it was (the track is at unity).
  const out = sounds(engine);
  assert.ok(Math.abs(rms(out[0].subarray(4800, 40000)) - 0.5 / Math.SQRT2) < 0.03);

  send(engine, 'edit.undo'); // clip and asset together
  const after = engine.takeEvents();
  assert.equal(audioTrack(after).clips.length, 0);
  assert.equal(last(after, 'project.assets').assets.length, 0);
});

test('a stereo file keeps its channels', async () => {
  const { engine, track } = await freshWithAudioTrack();
  const left = tone(1, 0.6);
  const right = new Float32Array(left.length); // silent on the right
  assert.ok(
    engine.loadAudio({
      path: 'audio/22222222.wav',
      sampleRate: RATE,
      channels: [left, right],
      name: 's.wav',
      use: { kind: 'clip', track, ticks: 0 },
    }).ok,
  );
  const out = sounds(engine);
  // The channels stay apart: what the file has on the left comes out on the left.
  assert.ok(rms(out[0].subarray(4800, 40000)) > 0.3);
  assert.ok(rms(out[1].subarray(4800, 40000)) < 0.01); // nothing on the right in the file
});

test('a song opens first and its audio arrives after, as it does from the browser', async () => {
  const { engine: first, track } = await freshWithAudioTrack();
  first.loadAudio({
    path: 'audio/33333333.wav',
    sampleRate: RATE,
    channels: [tone()],
    name: 'riff.wav',
    use: { kind: 'clip', track, ticks: 0 },
  });
  const text = first.exportProject('2026-10-07T12:00:00Z');
  assert.equal(JSON.parse(text).assets[0].path, 'audio/33333333.wav');

  const engine = await create();
  send(engine, 'app.ready');
  engine.takeEvents();
  assert.deepEqual(engine.importProject(text, { hasLocation: true }), {
    ok: true,
  });
  let events = engine.takeEvents();
  const asset = last(events, 'timeline.assets').assets[0];
  assert.equal(asset.loading, true); // the page is fetching it
  assert.equal(asset.loaded, false);
  assert.equal(rms(sounds(engine)[0]), 0); // nothing to play yet

  assert.deepEqual(
    engine.loadAudio({
      path: 'audio/33333333.wav',
      sampleRate: RATE,
      channels: [tone()],
    }),
    { ok: true },
  );
  events = engine.takeEvents();
  assert.equal(last(events, 'timeline.assets').assets[0].loaded, true);
  assert.ok(!events.some((e) => e.type === 'project.state' && e.payload.dirty)); // loading is not an edit
  assert.ok(rms(sounds(engine)[0].subarray(4800, 40000)) > 0.25);

  // Opening another song lets go of this one's audio.
  send(engine, 'project.new');
  engine.takeEvents();
  assert.equal(
    engine.loadAudio({
      path: 'audio/33333333.wav',
      sampleRate: RATE,
      channels: [tone()],
    }).ok,
    false,
  );
});

test('audio the page cannot find is shown as missing, and a replacement can be located', async () => {
  const { engine: first, track } = await freshWithAudioTrack();
  first.loadAudio({
    path: 'audio/44444444.wav',
    sampleRate: RATE,
    channels: [tone()],
    name: 'riff.wav',
    use: { kind: 'clip', track, ticks: 0 },
  });
  const text = first.exportProject('2026-10-07T12:00:00Z');

  const engine = await create();
  send(engine, 'app.ready');
  engine.importProject(text, { hasLocation: true });
  engine.takeEvents();
  engine.audioMissing('audio/44444444.wav');
  const events = engine.takeEvents();
  const asset = last(events, 'timeline.assets').assets[0];
  assert.equal(asset.missing, true);
  assert.equal(asset.loading, false);
  assert.equal(last(events, 'project.assets').assets[0].missing, true);

  const result = engine.loadAudio({
    path: 'audio/55555555.wav',
    sampleRate: RATE,
    channels: [tone(1, 0.4)],
    name: 'found.wav',
    use: { kind: 'relink', asset: asset.id },
  });
  assert.deepEqual(result, { ok: true });
  const after = engine.takeEvents();
  const relinked = last(after, 'timeline.assets').assets[0];
  assert.equal(relinked.loaded, true);
  assert.equal(relinked.name, 'found.wav');
  assert.equal(last(after, 'project.assets').assets[0].missing, false);
  assert.equal(last(after, 'history.state').undoLabel, 'Locate audio');
  assert.ok(rms(sounds(engine)[0].subarray(4800, 40000)) > 0.2);

  assert.equal(
    engine.loadAudio({
      path: 'audio/66666666.wav',
      sampleRate: RATE,
      channels: [tone()],
      use: { kind: 'relink', asset: 999 },
    }).ok,
    false,
  );
});

test('the sampler plays the sample it is given', async () => {
  const engine = await create();
  send(engine, 'app.ready');
  engine.takeEvents();
  send(engine, 'track.add', { kind: 2 }); // notes reach an instrument through its track
  engine.takeEvents();
  assert.deepEqual(
    engine.loadAudio({
      path: 'audio/77777777.wav',
      sampleRate: RATE,
      channels: [tone(1, 0.5)],
      name: 'pluck.wav',
      use: { kind: 'sampler' },
    }),
    { ok: true },
  );
  const events = engine.takeEvents();
  const state = last(events, 'sampler.state');
  assert.equal(state.name, 'pluck.wav');
  assert.equal(state.loaded, true);
  assert.equal(state.durationSeconds, 1);
  assert.equal(state.overview.length, 512);

  send(engine, 'instrument.select', { instrument: 1 });
  render(engine, 0.05); // the engine takes the new sample at its next block, before a note can use it
  send(engine, 'note.on', { note: 60, velocity: 1 });
  const out = render(engine, 0.5);
  assert.ok(rms(out[0].subarray(4800)) > 0.01);

  send(engine, 'edit.undo'); // the sampler goes back to empty
  assert.equal(last(engine.takeEvents(), 'sampler.state').loaded, false);
});

test('a drum pad can play a sample of its own, and goes back to the factory sound', async () => {
  const engine = await create();
  send(engine, 'app.ready');
  engine.takeEvents();
  assert.deepEqual(
    engine.loadAudio({
      path: 'audio/88888888.wav',
      sampleRate: RATE,
      channels: [tone(0.5, 0.5, 220)],
      name: 'thump.wav',
      use: { kind: 'pad', pad: 3 },
    }),
    { ok: true },
  );
  const events = engine.takeEvents();
  const pad = events.filter((e) => e.type === 'drums.pad' && e.payload.pad === 3).at(-1).payload;
  assert.equal(pad.custom, true);
  assert.equal(pad.name, 'thump.wav');

  send(engine, 'instrument.select', { instrument: 2 });
  send(engine, 'drums.trigger', { pad: 3, velocity: 1 });
  assert.ok(rms(render(engine, 0.3)[0].subarray(2400)) > 0.02);

  send(engine, 'drums.resetPad', { pad: 3 });
  const reset = engine
    .takeEvents()
    .filter((e) => e.type === 'drums.pad' && e.payload.pad === 3)
    .at(-1).payload;
  assert.equal(reset.custom, false);
  assert.equal(reset.name, 'Open Hat');
});

test('audio that is not used any more can be removed, and freed', async () => {
  const { engine, track } = await freshWithAudioTrack();
  engine.loadAudio({
    path: 'audio/99999999.wav',
    sampleRate: RATE,
    channels: [tone()],
    name: 'riff.wav',
    use: { kind: 'clip', track, ticks: 0 },
  });
  const clip = audioTrack(engine.takeEvents()).clips[0];
  send(engine, 'asset.removeUnused'); // in use: nothing goes
  assert.equal(last(engine.takeEvents(), 'project.assets') ?? undefined, undefined);
  send(engine, 'clip.remove', { clip: clip.id });
  engine.takeEvents();
  send(engine, 'asset.removeUnused');
  assert.equal(last(engine.takeEvents(), 'project.assets').assets.length, 0);
  engine.forgetAudio('audio/99999999.wav');
  assert.equal(
    engine.loadAudio({
      path: 'audio/99999999.wav',
      sampleRate: RATE,
      channels: [tone()],
    }).ok,
    false,
  ); // not part of the song
});

test('audio the engine cannot take is refused with a reason, and nothing changes', async () => {
  const { engine, track } = await freshWithAudioTrack();
  const use = { kind: 'clip', track, ticks: 0 };
  const refused = (args, pattern) => {
    const result = engine.loadAudio({
      sampleRate: RATE,
      channels: [tone(0.1)],
      use,
      name: 'x.wav',
      ...args,
    });
    assert.equal(result.ok, false, JSON.stringify(args).slice(0, 80));
    assert.match(result.error, pattern);
  };
  refused({ path: '../escape.wav' }, /file name/);
  refused({ path: 'audio/' }, /file name/);
  refused({ path: 'audio/a b.wav' }, /file name/);
  refused({ path: 'audio/ok.wav', sampleRate: 44100 }, /sample rate/);
  refused({ path: 'audio/ok.wav', channels: [] }, /format/);
  refused({ path: 'audio/ok.wav', channels: [tone(0.1), tone(0.1), tone(0.1)] }, /format/);
  refused({ path: 'audio/ok.wav', channels: [new Float32Array(0)] }, /format/);
  refused({ path: 'audio/ok.wav', channels: [new Float32Array(RATE * 601)] }, /10 minutes/);
  refused({ path: 'audio/ok.wav', use: { kind: 'clip', track: 9999, ticks: 0 } }, /Could not add/); // no such track
  engine.takeEvents();
  send(engine, 'app.ready');
  assert.equal(audioTrack(engine.takeEvents()).clips.length, 0); // nothing was added
});

test("audio is capped, so a page cannot fill the engine's memory", async () => {
  const engine = await create();
  send(engine, 'app.ready');
  const ten = () => new Float32Array(RATE * 590); // 113 MB of mono
  assert.equal(
    engine.loadAudio({
      path: 'audio/aaaa0001.wav',
      sampleRate: RATE,
      channels: [ten()],
      use: { kind: 'sampler' },
      name: 'one',
    }).ok,
    true,
  );
  assert.equal(
    engine.loadAudio({
      path: 'audio/aaaa0002.wav',
      sampleRate: RATE,
      channels: [ten()],
      use: { kind: 'pad', pad: 0 },
      name: 'two',
    }).ok,
    true,
  );
  const third = engine.loadAudio({
    path: 'audio/aaaa0003.wav',
    sampleRate: RATE,
    channels: [ten()],
    use: { kind: 'pad', pad: 1 },
    name: 'three',
  });
  assert.equal(third.ok, false);
  assert.match(third.error, /memory/);
});

// --- Exporting a WAV file -------------------------------------------------------------------------

// What a WAV file says about itself.
const wavInfo = (bytes) => {
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const text = (at, n) => String.fromCharCode(...bytes.subarray(at, at + n));
  return {
    riff: text(0, 4),
    wave: text(8, 4),
    audioFormat: view.getUint16(20, true),
    channels: view.getUint16(22, true),
    rate: view.getUint32(24, true),
    bits: view.getUint16(34, true),
    dataBytes: view.getUint32(40, true),
    size: bytes.length,
    view,
  };
};
const finishExport = (engine) => {
  let step = engine.exportStep();
  const seen = [];
  while (step.state === 'running') {
    seen.push(step.progress);
    step = engine.exportStep();
  }
  return { step, seen };
};

test('the starter song exports as a WAV file in each format and rate', async () => {
  for (const [format, bits, audioFormat] of [
    [0, 16, 1],
    [1, 24, 1],
    [2, 32, 3],
  ]) {
    const engine = await create();
    send(engine, 'app.ready');
    assert.deepEqual(engine.exportBegin({ format, sampleRate: 44100 }), { ok: true });
    const { step, seen } = finishExport(engine);
    assert.equal(step.state, 'done');
    assert.ok(seen.length > 0);
    assert.ok(
      seen.every((p, i) => p >= 0 && p <= 0.85 && (i === 0 || p >= seen[i - 1])),
      'progress only goes forward',
    );

    const { bytes, seconds, lufs, truePeak, missingAudio } = engine.exportResult();
    const info = wavInfo(bytes);
    assert.equal(info.riff, 'RIFF');
    assert.equal(info.wave, 'WAVE');
    assert.equal(info.audioFormat, audioFormat);
    assert.equal(info.channels, 2);
    assert.equal(info.rate, 44100);
    assert.equal(info.bits, bits);
    assert.equal(info.size, 44 + info.dataBytes);
    assert.ok(Math.abs(info.dataBytes / (2 * (bits / 8) * 44100) - seconds) < 1e-6);
    assert.ok(
      seconds > 7.9 && seconds < 20,
      `a four-bar starter beat, with its tail (${seconds} s)`,
    );
    assert.ok(lufs < -5 && lufs > -50, `loudness ${lufs}`);
    assert.ok(truePeak < 0.5 && truePeak > -40);
    assert.equal(missingAudio, 0);
    engine.exportRelease();
    assert.equal(engine.exportResult().bytes.length, 0);
  }
});

test('the export is what playing the song produces, at the engine rate', async () => {
  const { engine: exporter, track } = await freshWithAudioTrack();
  const samples = tone(1, 0.5);
  exporter.loadAudio({
    path: 'audio/eeeeeeee.wav',
    sampleRate: RATE,
    channels: [samples],
    name: 'riff.wav',
    use: { kind: 'clip', track, ticks: 0 },
  });
  assert.deepEqual(exporter.exportBegin({ format: 2, sampleRate: RATE }), { ok: true });
  assert.equal(finishExport(exporter).step.state, 'done');
  const { bytes } = exporter.exportResult();
  const info = wavInfo(bytes);
  const exported = new Float32Array(info.dataBytes / 4);
  for (let i = 0; i < exported.length; i++) exported[i] = info.view.getFloat32(44 + i * 4, true);
  const left = exported.filter((_, i) => i % 2 === 0);

  // The same song, played live: the engine's own latency (a few dozen samples) is at the front of
  // the live output and dropped from the export, and from there on they are the same samples.
  const played = sounds(exporter, 1.2)[0];
  const same = (delay) => {
    for (let i = 4800; i < 40000; i += 997) if (left[i] !== played[i + delay]) return false;
    return true;
  };
  const delay = Array.from({ length: 400 }, (_, d) => d).find(same);
  assert.notEqual(delay, undefined, 'the export is the live output, minus the latency');
  assert.ok(delay > 0 && delay < 400);
  assert.ok(Math.abs(rms(left.subarray(4800, 40000)) - 0.5 / Math.SQRT2) < 0.03);
});

test('a song at another sample rate is resampled, and a longer file is not a different song', async () => {
  const lengths = [];
  for (const sampleRate of [44100, 48000, 96000]) {
    const { engine, track } = await freshWithAudioTrack();
    engine.loadAudio({
      path: 'audio/ffffffff.wav',
      sampleRate: RATE,
      channels: [tone(1, 0.5)],
      name: 'riff.wav',
      use: { kind: 'clip', track, ticks: 0 },
    });
    assert.ok(engine.exportBegin({ format: 1, sampleRate }).ok);
    finishExport(engine);
    const result = engine.exportResult();
    assert.equal(wavInfo(result.bytes).rate, sampleRate);
    lengths.push(result.seconds);
  }
  assert.ok(Math.abs(lengths[0] - lengths[1]) < 0.01 && Math.abs(lengths[1] - lengths[2]) < 0.01);
});

test('an export that cannot go ahead says why, and can be abandoned part-way', async () => {
  const engine = await create();
  send(engine, 'app.ready');
  send(engine, 'project.new');
  assert.match(engine.exportBegin({ format: 1, sampleRate: 48000 }).error, /nothing to export/);

  send(engine, 'app.ready');
  engine.importProject(
    JSON.stringify({ ...JSON.parse(engine.exportProject('2026-10-07T12:00:00Z')) }),
    {},
  );
  assert.match(engine.exportBegin({ format: 9, sampleRate: 48000 }).error, /not supported/);
  assert.match(engine.exportBegin({ format: 1, sampleRate: 100 }).error, /not supported/);
  assert.equal(engine.exportStep().state, 'failed'); // nothing is running

  const starter = await create();
  send(starter, 'app.ready');
  assert.ok(starter.exportBegin({ format: 1, sampleRate: 48000 }).ok);
  assert.equal(starter.exportStep().state, 'running');
  starter.exportCancel();
  assert.equal(starter.exportStep().state, 'failed');
  assert.ok(starter.exportBegin({ format: 1, sampleRate: 48000 }).ok); // and it can start again
  assert.equal(finishExport(starter).step.state, 'done');
});

test('audio the song lists but the page could not provide is counted, and silent in the file', async () => {
  const { engine: first, track } = await freshWithAudioTrack();
  first.loadAudio({
    path: 'audio/99999991.wav',
    sampleRate: RATE,
    channels: [tone()],
    name: 'riff.wav',
    use: { kind: 'clip', track, ticks: 0 },
  });
  const text = first.exportProject('2026-10-07T12:00:00Z');
  const engine = await create();
  send(engine, 'app.ready');
  engine.importProject(text, {});
  engine.audioMissing('audio/99999991.wav');
  assert.ok(engine.exportBegin({ format: 1, sampleRate: 48000 }).ok);
  assert.equal(finishExport(engine).step.state, 'done');
  const result = engine.exportResult();
  assert.equal(result.missingAudio, 1);
  const info = wavInfo(result.bytes);
  // The song is only that clip, so with its audio missing the file is digital silence.
  assert.ok(result.bytes.subarray(44).every((byte) => byte === 0));
});

test('the sampler and the pads are in the export too', async () => {
  const engine = await create();
  send(engine, 'app.ready');
  send(engine, 'project.new');
  engine.loadAudio({
    path: 'audio/aaaa1111.wav',
    sampleRate: RATE,
    channels: [tone(0.5, 0.5, 330)],
    name: 'thump.wav',
    use: { kind: 'pad', pad: 0 },
  });
  send(engine, 'drums.setStep', { clip: 0, pad: 0, step: 0, on: true, gesture: 0 });
  assert.ok(engine.exportBegin({ format: 2, sampleRate: RATE }).ok);
  assert.equal(finishExport(engine).step.state, 'done');
  const info = wavInfo(engine.exportResult().bytes);
  let peak = 0;
  for (let i = 0; i < 12000; i++)
    peak = Math.max(peak, Math.abs(info.view.getFloat32(44 + i * 8, true)));
  assert.ok(peak > 0.1, `the pad's own sample is in the file (${peak})`);
});
