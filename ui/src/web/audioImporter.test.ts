import { describe, expect, it, vi } from 'vitest';

import { audioPath, audioPathsIn, decodeAudio, ImportError } from './audioFiles';
import type { DecodedAudio } from './audioFiles';
import { AudioImporter } from './audioImporter';
import { createMemoryStore } from './projectStore';

const decodedTone: DecodedAudio = {
  channels: [new Float32Array(480)],
  sampleRate: 48000,
  seconds: 0.01,
};
const file = (name: string, bytes = 8) =>
  new File([new Uint8Array(bytes)], name, { type: 'audio/wav' });

function setup(
  options: { decode?: () => Promise<DecodedAudio>; reply?: Record<string, unknown> } = {},
) {
  const store = createMemoryStore();
  const commands: Record<string, unknown>[] = [];
  const notices: [number, string][] = [];
  let ids = 0;
  const importer = new AudioImporter({
    store,
    decode: options.decode ?? (() => Promise.resolve(decodedTone)),
    send: (command) => {
      commands.push(command);
      return Promise.resolve(options.reply ?? { ok: true });
    },
    notice: (level, message) => notices.push([level, message]),
    newId: () => `id${String(++ids)}`,
    choose: () => Promise.resolve(file('picked.wav')),
  });
  return { importer, store, commands, notices };
}

describe('audio file names and song files', () => {
  it('names a stored file after a random id and its extension, never after the file', () => {
    expect(audioPath('My Riff (final).WAV', 'ab12-cd34')).toBe('audio/ab12-cd34.wav');
    expect(audioPath('../../etc/passwd', 'x')).toBe('audio/x');
    expect(audioPath('song.toolongext', 'x')).toBe('audio/x');
    expect(audioPath('a.mp3', '../bad id!')).toBe('audio/badid.mp3');
    expect(audioPath('a.mp3', '')).toBe('audio/audio.mp3');
  });

  it('lists the audio a song file uses, and nothing for anything else', () => {
    expect(
      audioPathsIn(
        JSON.stringify({
          assets: [
            { id: 1, path: 'audio/a.wav', name: 'a' },
            { id: 2, path: 'audio/b.mp3' },
          ],
        }),
      ),
    ).toEqual(['audio/a.wav', 'audio/b.mp3']);
    expect(audioPathsIn(JSON.stringify({ assets: [{ id: 1 }, null, 'x', { path: 7 }] }))).toEqual(
      [],
    );
    expect(audioPathsIn('not json')).toEqual([]);
    expect(audioPathsIn('[]')).toEqual([]);
    expect(audioPathsIn('null')).toEqual([]);
  });
});

describe('decoding', () => {
  const buffer = (channels: number, seconds: number, length = 100) =>
    ({
      duration: seconds,
      length,
      numberOfChannels: channels,
      sampleRate: 48000,
      getChannelData: (index: number) => new Float32Array(length).fill(index + 1),
    }) as unknown as AudioBuffer;

  it('keeps two channels at most and copies them', async () => {
    const context = { decodeAudioData: vi.fn(() => Promise.resolve(buffer(6, 2))) };
    const bytes = new ArrayBuffer(16);
    const result = await decodeAudio(context, bytes);
    expect(result.channels).toHaveLength(2);
    expect(result.channels[1]?.[0]).toBe(2);
    expect(result.seconds).toBe(2);
    expect(bytes.byteLength).toBe(16); // the caller's copy of the file is untouched
  });

  it('refuses silence-length and over-long files with a reason', async () => {
    await expect(
      decodeAudio({ decodeAudioData: () => Promise.resolve(buffer(1, 0, 0)) }, new ArrayBuffer(1)),
    ).rejects.toThrow(ImportError);
    await expect(
      decodeAudio({ decodeAudioData: () => Promise.resolve(buffer(1, 601)) }, new ArrayBuffer(1)),
    ).rejects.toThrow(/10 minutes/);
  });
});

describe('importing', () => {
  it('keeps the file, hands the samples to the engine, and remembers the song uses it', async () => {
    const t = setup();
    await t.importer.importFile(file('Riff.WAV'), { kind: 'clip', track: 3, ticks: 960 });
    expect(await t.store.listAudio()).toHaveLength(1);
    expect(t.commands).toHaveLength(1);
    expect(t.commands[0]).toMatchObject({
      kind: 'audio',
      path: 'audio/id1.wav',
      sampleRate: 48000,
      name: 'Riff.WAV',
      use: { kind: 'clip', track: 3, ticks: 960 },
    });
    expect(t.importer.inUse.has('audio/id1.wav')).toBe(true);
    expect(t.notices).toEqual([]);
  });

  it('refuses a file that is too large, without reading it', async () => {
    const t = setup();
    const big = file('huge.wav');
    Object.defineProperty(big, 'size', { value: 201 * 1024 * 1024 });
    await t.importer.importFile(big, { kind: 'sampler' });
    expect(t.notices[0]).toEqual([2, expect.stringContaining('too large')]);
    expect(t.commands).toEqual([]);
  });

  it('says so when the browser cannot decode it, and stores nothing', async () => {
    const t = setup({ decode: () => Promise.reject(new Error('EncodingError')) });
    await t.importer.importFile(file('notes.txt'), { kind: 'sampler' });
    expect(t.notices[0]).toEqual([2, expect.stringContaining('could not be read')]);
    expect(await t.store.listAudio()).toEqual([]);
    expect(t.commands).toEqual([]);
  });

  it('passes on the reason when the file is too long', async () => {
    const t = setup({
      decode: () => Promise.reject(new ImportError('The file is longer than 10 minutes.')),
    });
    await t.importer.importFile(file('long.wav'), { kind: 'sampler' });
    expect(t.notices[0]?.[1]).toBe('“long.wav”: The file is longer than 10 minutes.');
  });

  it('does not use a file it could not keep', async () => {
    const t = setup();
    t.store.failNextWrite(new DOMException('full', 'QuotaExceededError'));
    await t.importer.importFile(file('a.wav'), { kind: 'sampler' });
    expect(t.notices[0]?.[1]).toContain('no room left');
    expect(t.commands).toEqual([]);
    expect(t.importer.inUse.size).toBe(0);
  });

  it('lets go of the kept file when the engine refuses the audio', async () => {
    const t = setup({ reply: { ok: false, error: 'There is not enough memory for more audio.' } });
    await t.importer.importFile(file('a.wav'), { kind: 'sampler' });
    expect(t.notices[0]).toEqual([2, 'There is not enough memory for more audio.']);
    expect(await t.store.listAudio()).toEqual([]);
    expect(t.importer.inUse.size).toBe(0);
  });

  it('asks for a file with the dialog, then imports it for the target', async () => {
    const t = setup();
    await t.importer.choose({ kind: 'relink', asset: 4 });
    expect(t.commands[0]).toMatchObject({ name: 'picked.wav', use: { kind: 'relink', asset: 4 } });
  });

  it('imports one file after another', async () => {
    const t = setup();
    const first = t.importer.importFile(file('a.wav'), { kind: 'clip', track: 1, ticks: 0 });
    const second = t.importer.importFile(file('b.wav'), { kind: 'clip', track: 1, ticks: 960 });
    await Promise.all([first, second]);
    expect(t.commands.map((c) => c['name'])).toEqual(['a.wav', 'b.wav']);
  });
});

describe('opening a song', () => {
  const song = (...paths: string[]) =>
    JSON.stringify({ assets: paths.map((path, id) => ({ id: id + 1, path, name: path })) });

  it('loads each file the song lists, and says which cannot be found', async () => {
    const t = setup();
    await t.store.writeAudio('audio/here.wav', new ArrayBuffer(4));
    await t.importer.loadSong(song('audio/here.wav', 'audio/gone.wav', 'audio/gone2.wav'));
    expect(t.commands.map((c) => [c['kind'], c['path']])).toEqual([
      ['audio', 'audio/here.wav'],
      ['audioMissing', 'audio/gone.wav'],
      ['audioMissing', 'audio/gone2.wav'],
    ]);
    expect(t.commands[0]).toMatchObject({ use: { kind: 'existing' } });
    expect(t.notices).toEqual([[1, expect.stringContaining('2 audio files')]]);
    expect([...t.importer.inUse].sort()).toEqual([
      'audio/gone.wav',
      'audio/gone2.wav',
      'audio/here.wav',
    ]);
  });

  it('shows a file the browser can no longer decode as missing', async () => {
    const t = setup({ decode: () => Promise.reject(new Error('broken')) });
    await t.store.writeAudio('audio/old.ogg', new ArrayBuffer(4));
    await t.importer.loadSong(song('audio/old.ogg'));
    expect(t.commands.map((c) => c['kind'])).toEqual(['audioMissing']);
    expect(t.notices[0]?.[1]).toContain('One audio file');
  });

  it('a song with no audio loads nothing and says nothing', async () => {
    const t = setup();
    t.importer.inUse.add('audio/stale.wav');
    await t.importer.loadSong(song());
    expect(t.commands).toEqual([]);
    expect(t.notices).toEqual([]);
    expect(t.importer.inUse.size).toBe(0);
  });

  it('a new song uses no audio', () => {
    const t = setup();
    t.importer.inUse.add('audio/x.wav');
    t.importer.newSong();
    expect(t.importer.inUse.size).toBe(0);
  });
});
