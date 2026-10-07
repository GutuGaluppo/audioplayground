import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

import type * as AudioFiles from '../web/audioFiles';
import { createMemoryStore } from '../web/projectStore';
import { createWasmBridge } from './wasmBridge';

// The browser engine's audio import, with a stand-in for the browser's audio: the worklet answers
// every command the way a healthy engine would, and the "decoder" returns a short stereo sound.
const chosen: { file: File | null } = { file: null };
vi.mock('../web/audioFiles', async (importOriginal) => ({
  ...(await importOriginal<typeof AudioFiles>()),
  chooseAudioFile: () => Promise.resolve(chosen.file),
}));

interface Command {
  id: number;
  kind: string;
  [key: string]: unknown;
}

class FakePort {
  commands: Command[] = [];
  replies: Record<string, Record<string, unknown>> = {};
  onmessage: ((message: { data: unknown }) => void) | null = null;
  postMessage(message: { command?: Command }) {
    const command = message.command;
    if (!command) return;
    this.commands.push(command);
    queueMicrotask(() => {
      this.onmessage?.({
        data: { reply: { id: command.id, ok: true, ...this.replies[command.kind] } },
      });
    });
  }
}

const nodes: FakeNode[] = [];
class FakeNode {
  port = new FakePort();
  connect = vi.fn();
  constructor() {
    nodes.push(this);
  }
}
class FakeContext {
  state = 'running';
  sampleRate = 48000;
  baseLatency = 0;
  destination = {};
  onstatechange: (() => void) | null = null;
  audioWorklet = { addModule: () => Promise.resolve() };
  decodeAudioData = vi.fn(() =>
    Promise.resolve({
      duration: 0.5,
      length: 24000,
      numberOfChannels: 2,
      sampleRate: 48000,
      getChannelData: () => new Float32Array(24000),
    } as unknown as AudioBuffer),
  );
  resume() {
    return Promise.resolve();
  }
}

const settle = async (turns = 30) => {
  for (let i = 0; i < turns; i++) await Promise.resolve();
};

const wav = (name: string) => new File([new Uint8Array(16)], name, { type: 'audio/wav' });

async function setup() {
  const store = createMemoryStore();
  const bridge = createWasmBridge('./engine/', () => Promise.resolve(store));
  await settle();
  const port = nodes.at(-1)?.port;
  if (!port) throw new Error('the engine did not start');
  port.onmessage?.({ data: { ready: true } });
  await settle();
  const notices: string[] = [];
  bridge.on('app.notice', (notice) => notices.push(notice.message));
  const deliver = (events: unknown[]) => port.onmessage?.({ data: { events } });
  return { bridge, store, port, notices, deliver };
}

beforeEach(() => {
  nodes.length = 0;
  chosen.file = null;
  vi.stubGlobal('AudioContext', FakeContext);
  vi.stubGlobal('AudioWorkletNode', FakeNode);
  vi.stubGlobal(
    'fetch',
    vi.fn(() =>
      Promise.resolve({ ok: true, arrayBuffer: () => Promise.resolve(new ArrayBuffer(8)) }),
    ),
  );
  const empty = new WebAssembly.Module(new Uint8Array([0, 97, 115, 109, 1, 0, 0, 0]));
  vi.spyOn(WebAssembly, 'compile').mockResolvedValue(empty);
});

afterEach(() => {
  vi.unstubAllGlobals();
  vi.restoreAllMocks();
});

describe('browser engine: audio files', () => {
  it('a dropped file is decoded, kept, and handed to the engine for the track it was dropped on', async () => {
    const t = await setup();
    await t.bridge.importFiles?.([wav('Riff.WAV')], { kind: 'clip', track: 3, ticks: 3840 });
    const command = t.port.commands.find((c) => c.kind === 'audio');
    expect(command).toMatchObject({
      kind: 'audio',
      sampleRate: 48000,
      name: 'Riff.WAV',
      use: { kind: 'clip', track: 3, ticks: 3840 },
    });
    expect(command?.['path']).toMatch(/^audio\/[A-Za-z0-9-]+\.wav$/);
    expect(command?.['channels']).toHaveLength(2);
    const kept = await t.store.listAudio();
    expect(kept.map((f) => f.path)).toEqual([command?.['path']]);
  });

  it('takes one file at a time and says so', async () => {
    const t = await setup();
    await t.bridge.importFiles?.([wav('a.wav'), wav('b.wav')], { kind: 'sampler' });
    expect(t.port.commands.filter((c) => c.kind === 'audio')).toHaveLength(1);
    expect(t.notices.some((m) => m.includes('One file at a time'))).toBe(true);
  });

  it('the file dialog opens for the intents that need a file, each for its own target', async () => {
    const t = await setup();
    const targets: [Parameters<typeof t.bridge.send>[0], unknown][] = [
      [
        { type: 'track.importAudio', payload: { track: 2, ticks: 960 } },
        { kind: 'clip', track: 2, ticks: 960 },
      ],
      [
        { type: 'asset.locate', payload: { asset: 5 } },
        { kind: 'relink', asset: 5 },
      ],
      [{ type: 'sampler.load', payload: {} }, { kind: 'sampler' }],
      [
        { type: 'drums.loadPad', payload: { pad: 7 } },
        { kind: 'pad', pad: 7 },
      ],
    ];
    for (const [intent, use] of targets) {
      t.port.commands.length = 0;
      chosen.file = wav('pick.wav');
      t.bridge.send(intent);
      await settle(60);
      expect(t.port.commands.find((c) => c.kind === 'audio')?.['use']).toEqual(use);
    }
    // Cancelling the dialog does nothing.
    t.port.commands.length = 0;
    chosen.file = null;
    t.bridge.send({ type: 'sampler.load', payload: {} });
    await settle(60);
    expect(t.port.commands).toEqual([]);
  });

  it('the engine refusing the audio is shown, and the kept copy is let go', async () => {
    const t = await setup();
    t.port.replies['audio'] = { ok: false, error: 'There is not enough memory for more audio.' };
    await t.bridge.importFiles?.([wav('big.wav')], { kind: 'sampler' });
    expect(t.notices).toContain('There is not enough memory for more audio.');
    expect(await t.store.listAudio()).toEqual([]);
  });

  it('opening a saved song loads the audio it lists, and marks what is gone', async () => {
    const t = await setup();
    const song = JSON.stringify({
      assets: [
        { id: 1, path: 'audio/kept.wav' },
        { id: 2, path: 'audio/lost.wav' },
      ],
    });
    await t.store.write({ id: 's1', name: 'With audio', updatedAt: 1, bytes: song.length }, song);
    await t.store.writeAudio('audio/kept.wav', new ArrayBuffer(8));
    t.deliver([
      { type: 'project.state', payload: { name: 'First song', dirty: false, hasLocation: false } },
    ]);

    await t.bridge.projects?.openProject('s1');
    await settle(80);
    expect(
      t.port.commands.map((c) => [
        c.kind,
        c['path'] ?? '',
        (c['use'] as { kind?: string } | undefined)?.kind ?? '',
      ]),
    ).toEqual([
      ['import', '', ''],
      ['audio', 'audio/kept.wav', 'existing'],
      ['audioMissing', 'audio/lost.wav', ''],
    ]);
    expect(t.notices.some((m) => m.includes('One audio file'))).toBe(true);
  });
});
