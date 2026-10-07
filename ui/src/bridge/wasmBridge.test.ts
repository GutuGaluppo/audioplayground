import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

import { createWasmBridge } from './wasmBridge';

// A stand-in for the browser's audio: just enough AudioContext / AudioWorkletNode to watch what
// the bridge does with them.
class FakePort {
  sent: unknown[] = [];
  onmessage: ((message: { data: unknown }) => void) | null = null;
  postMessage(message: unknown) {
    this.sent.push(message);
  }
}

const contexts: FakeContext[] = [];
const nodes: FakeNode[] = [];
const context = () => contexts.at(-1);
const node = () => nodes.at(-1);
let fetchOk = true;

class FakeContext {
  state: 'suspended' | 'running' = 'suspended';
  baseLatency = 0.005;
  outputLatency = 0.01;
  destination = {};
  onstatechange: (() => void) | null = null;
  audioWorklet = { addModule: vi.fn(() => Promise.resolve()) };
  constructor() {
    contexts.push(this);
  }
  resume() {
    this.state = 'running';
    this.onstatechange?.();
    return Promise.resolve();
  }
}

class FakeNode {
  port = new FakePort();
  connect = vi.fn();
  constructor() {
    nodes.push(this);
  }
}

const settle = async () => {
  for (let i = 0; i < 10; i++) await Promise.resolve();
};

beforeEach(() => {
  contexts.length = 0;
  nodes.length = 0;
  fetchOk = true;
  vi.stubGlobal('AudioContext', FakeContext);
  vi.stubGlobal('AudioWorkletNode', FakeNode);
  vi.stubGlobal(
    'fetch',
    vi.fn(() =>
      Promise.resolve({ ok: fetchOk, arrayBuffer: () => Promise.resolve(new ArrayBuffer(8)) }),
    ),
  );
  // The smallest valid module: the bridge only passes it on to the worklet.
  const empty = new WebAssembly.Module(new Uint8Array([0, 97, 115, 109, 1, 0, 0, 0]));
  vi.spyOn(WebAssembly, 'compile').mockResolvedValue(empty);
});

afterEach(() => {
  vi.unstubAllGlobals();
  vi.restoreAllMocks();
});

describe('browser engine bridge', () => {
  it('holds intents until the engine is up, then delivers them in order', async () => {
    const bridge = createWasmBridge('./engine/');
    expect(bridge.kind).toBe('wasm');
    expect(bridge.isNative).toBe(false);
    expect(bridge.audio?.state()).toBe('loading');

    bridge.send({ type: 'app.ready', payload: {} });
    bridge.send({ type: 'transport.play', payload: {} });
    await settle();

    expect(node()).not.toBeNull();
    const sent = node()?.port.sent ?? [];
    expect(sent.slice(0, 2)).toEqual([
      { intent: { type: 'app.ready', payload: {} } },
      { intent: { type: 'transport.play', payload: {} } },
    ]);
    expect(sent[2]).toEqual({ latencyMs: 15 });

    bridge.send({ type: 'transport.stop', payload: {} });
    expect(node()?.port.sent.at(-1)).toEqual({ intent: { type: 'transport.stop', payload: {} } });
    expect(node()?.connect).toHaveBeenCalledWith(context()?.destination);
  });

  it('asks for a click, and runs once the browser allows sound', async () => {
    const bridge = createWasmBridge();
    const changes = vi.fn();
    bridge.audio?.subscribe(changes);
    await settle();
    expect(bridge.audio?.state()).toBe('blocked'); // the context starts suspended

    await bridge.audio?.start();
    expect(context()?.state).toBe('running');
    expect(bridge.audio?.state()).toBe('running');
    expect(changes).toHaveBeenCalled();
  });

  it("passes the core's events on, and drops anything that is not a valid event", async () => {
    const bridge = createWasmBridge();
    const seen: unknown[] = [];
    bridge.on('param.value', (payload) => seen.push(payload));
    await settle();

    const deliver = (events: unknown[]) => node()?.port.onmessage?.({ data: { events } });
    deliver([
      { type: 'param.value', payload: { id: 'synth.cutoff', value: 1200 } },
      { type: 'param.value', payload: { id: 'synth.cutoff', value: 'loud' } }, // wrong type
      { type: 'no.such.event', payload: {} },
      { type: 'param.value', payload: { id: 'synth.cutoff', value: 900, extra: 1 } }, // unknown field
    ]);
    expect(seen).toEqual([{ id: 'synth.cutoff', value: 1200 }]);
  });

  it('reports why it cannot start', async () => {
    fetchOk = false;
    const missing = createWasmBridge();
    await settle();
    expect(missing.audio?.state()).toBe('failed');
    expect(missing.audio?.error()).toMatch(/could not be downloaded/);

    fetchOk = true;
    vi.stubGlobal('AudioWorkletNode', undefined);
    const unsupported = createWasmBridge();
    await settle();
    expect(unsupported.audio?.state()).toBe('failed');
    expect(unsupported.audio?.error()).toMatch(/no AudioWorklet/);

    vi.stubGlobal('AudioWorkletNode', FakeNode);
    const bridge = createWasmBridge();
    await settle();
    node()?.port.onmessage?.({ data: { failed: 'The engine refused its settings' } });
    expect(bridge.audio?.state()).toBe('failed');
    expect(bridge.audio?.error()).toBe('The engine refused its settings');
  });
});
