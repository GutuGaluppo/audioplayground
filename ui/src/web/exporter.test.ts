import { describe, expect, it, vi } from 'vitest';

import type { DecodedAudio } from './audioFiles';
import { clock, describeExport, exportFileName, WebExporter } from './exporter';
import type { ExportResult, ExporterDeps, RenderWorker } from './exporter';

class FakeWorker implements RenderWorker {
  onmessage: ((event: { data: Record<string, unknown> }) => void) | null = null;
  onerror: ((event: { message?: string }) => void) | null = null;
  posted: { message: Record<string, unknown>; transfer: Transferable[] }[] = [];
  terminated = false;
  postMessage(message: unknown, transfer: Transferable[] = []) {
    this.posted.push({ message: message as Record<string, unknown>, transfer });
  }
  terminate() {
    this.terminated = true;
  }
  say(data: Record<string, unknown>) {
    this.onmessage?.({ data });
  }
}

const decoded: DecodedAudio = { channels: [new Float32Array(8)], sampleRate: 48000, seconds: 1 };
const finishedFile = {
  bytes: new Uint8Array([82, 73, 70, 70]),
  seconds: 187,
  lufs: -14.2,
  truePeak: -1,
  missingAudio: 0,
};

function setup(over: Partial<ExporterDeps> = {}) {
  const worker = new FakeWorker();
  const states: [boolean, number][] = [];
  const notices: [number, string][] = [];
  const saved: [string, Uint8Array][] = [];
  const deps: ExporterDeps = {
    exportProject: () =>
      Promise.resolve(
        JSON.stringify({ assets: [{ path: 'audio/a.wav' }, { path: 'audio/b.wav' }] }),
      ),
    collectAudio: () =>
      Promise.resolve({ audio: [{ path: 'audio/a.wav', decoded }], missing: ['audio/b.wav'] }),
    createWorker: () => worker,
    module: () =>
      Promise.resolve(new WebAssembly.Module(new Uint8Array([0, 97, 115, 109, 1, 0, 0, 0]))),
    sampleRate: () => 48000,
    projectName: () => 'Late night',
    state: (running, progress) => states.push([running, progress]),
    notice: (level, message) => notices.push([level, message]),
    save: (name, bytes) => saved.push([name, bytes]),
    ...over,
  };
  return { exporter: new WebExporter(deps), worker, states, notices, saved };
}

const settle = async () => {
  for (let i = 0; i < 30; i++) await Promise.resolve();
};

describe('file name and wording', () => {
  it('makes a name every system accepts', () => {
    expect(exportFileName('Late night')).toBe('Late night.wav');
    expect(exportFileName('a/b\\c:d*e?f"g<h>i|j')).toBe('a-b-c-d-e-f-g-h-i-j.wav');
    expect(exportFileName('  ..hidden  ')).toBe('hidden.wav');
    expect(exportFileName('')).toBe('Song.wav');
    expect(exportFileName('\u0000\u001f')).toBe('--.wav');
    expect(exportFileName('x'.repeat(300))).toBe(`${'x'.repeat(100)}.wav`);
  });

  it('says length, loudness and peak the way the desktop does', () => {
    expect(clock(187)).toBe('3:07');
    expect(clock(0)).toBe('0:00');
    expect(clock(59.6)).toBe('1:00');
    expect(describeExport({ ...finishedFile, fileName: 'x.wav' })).toBe(
      '3:07 · -14.2 LUFS · peak -1.0 dBTP',
    );
  });
});

describe('exporting', () => {
  it('sends the song and its audio to a worker, shows progress and keeps the result for a click', async () => {
    const t = setup();
    const done = t.exporter.start({ format: 1, sampleRate: 44100 });
    await settle();
    expect(t.states[0]).toEqual([true, 0]);

    expect(t.worker.posted).toHaveLength(1);
    const job = t.worker.posted[0]?.message;
    expect(job).toMatchObject({
      sampleRate: 48000,
      missing: ['audio/b.wav'],
      options: { format: 1, sampleRate: 44100 },
    });
    expect(job?.['audio']).toEqual([
      { path: 'audio/a.wav', sampleRate: 48000, channels: decoded.channels },
    ]);
    expect(t.worker.posted[0]?.transfer).toHaveLength(1); // the samples move to the worker, not copy

    t.worker.say({ progress: 0.2 });
    t.worker.say({ progress: 0.85 });
    expect(t.states.at(-1)?.[1]).toBeCloseTo(0.95);
    expect(t.states.every(([, p], i) => i === 0 || p >= (t.states[i - 1]?.[1] ?? 0))).toBe(true);

    t.worker.say({ done: finishedFile });
    await done;
    expect(t.exporter.isRunning()).toBe(false);
    expect(t.states.at(-1)).toEqual([false, 1]);
    expect(t.exporter.result()).toMatchObject({ fileName: 'Late night.wav', seconds: 187 });

    t.exporter.download();
    expect(t.saved).toEqual([['Late night.wav', finishedFile.bytes]]);
    t.exporter.dismiss();
    expect(t.exporter.result()).toBeNull();
  });

  it('a second export is ignored while one is running', async () => {
    const t = setup();
    const first = t.exporter.start({ format: 1, sampleRate: 48000 });
    await settle();
    await t.exporter.start({ format: 0, sampleRate: 44100 });
    expect(t.worker.posted).toHaveLength(1);
    t.worker.say({ done: finishedFile });
    await first;
  });

  it('Cancel stops the render at once and ends the export without a file', async () => {
    const t = setup();
    const done = t.exporter.start({ format: 1, sampleRate: 48000 });
    await settle();
    t.worker.say({ progress: 0.3 });
    t.exporter.cancel();
    await done; // does not hang: a terminated worker says nothing more
    expect(t.worker.terminated).toBe(true);
    expect(t.exporter.isRunning()).toBe(false);
    expect(t.exporter.result()).toBeNull();
    expect(t.states.at(-1)).toEqual([false, 0]);
    expect(t.notices).toEqual([[0, 'Export cancelled.']]);
    t.worker.say({ done: finishedFile }); // a late message from a worker that was told to stop
  });

  it('Cancel while the audio is still being prepared', async () => {
    let release: () => void = () => undefined;
    const t = setup({
      collectAudio: () =>
        new Promise((resolve) => {
          release = () => {
            resolve({ audio: [], missing: [] });
          };
        }),
    });
    const done = t.exporter.start({ format: 1, sampleRate: 48000 });
    await settle();
    t.exporter.cancel();
    release();
    await done;
    expect(t.worker.posted).toEqual([]); // never got as far as a worker
    expect(t.exporter.result()).toBeNull();
  });

  it('says what went wrong', async () => {
    const failing = setup();
    const a = failing.exporter.start({ format: 1, sampleRate: 48000 });
    await settle();
    failing.worker.say({ failed: 'There is nothing to export yet: add a clip to the timeline.' });
    await a;
    expect(failing.notices).toEqual([
      [2, 'Export failed. There is nothing to export yet: add a clip to the timeline.'],
    ]);
    expect(failing.exporter.result()).toBeNull();
    expect(failing.exporter.isRunning()).toBe(false);

    const crashed = setup();
    const b = crashed.exporter.start({ format: 1, sampleRate: 48000 });
    await settle();
    crashed.worker.onerror?.({});
    await b;
    expect(crashed.notices[0]?.[1]).toContain('could not be started');

    const noEngine = setup({
      exportProject: () => Promise.reject(new Error('The song could not be read from the engine.')),
    });
    await noEngine.exporter.start({ format: 1, sampleRate: 48000 });
    expect(noEngine.notices[0]).toEqual([
      2,
      'Export failed. The song could not be read from the engine.',
    ]);
    expect(noEngine.states.at(-1)?.[0]).toBe(false);
  });

  it('can export again after one finished', async () => {
    const t = setup();
    const first = t.exporter.start({ format: 1, sampleRate: 48000 });
    await settle();
    t.worker.say({ done: finishedFile });
    await first;
    const second = t.exporter.start({ format: 1, sampleRate: 48000 });
    await settle();
    expect(t.exporter.result()).toBeNull(); // the last file is dropped when a new export begins
    t.worker.say({ done: { ...finishedFile, seconds: 5 } });
    await second;
    expect(t.exporter.result()?.seconds).toBe(5);
  });

  it('tells the interface whenever the result appears or goes', async () => {
    const t = setup();
    const changes = vi.fn();
    t.exporter.subscribe(changes);
    const done = t.exporter.start({ format: 1, sampleRate: 48000 });
    await settle();
    t.worker.say({ done: finishedFile });
    await done;
    const afterDone = changes.mock.calls.length;
    expect(afterDone).toBeGreaterThan(0);
    t.exporter.dismiss();
    expect(changes.mock.calls.length).toBe(afterDone + 1);
  });
});

describe('what the result says', () => {
  it('is shown as given', () => {
    const result: ExportResult = { ...finishedFile, fileName: 'x.wav' };
    expect(describeExport(result)).toContain('LUFS');
  });
});
