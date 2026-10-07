import { EventRouter } from './bridge';
import type { AudioGate, AudioGateState, Bridge } from './bridge';
import type { Intent } from './generated';

// Where the engine's files are served, relative to the page.
const ENGINE_DIRECTORY = './engine/';
const MAX_QUEUED_INTENTS = 1000;

/**
 * The browser's engine (ADR-016): the core, compiled to WebAssembly, running inside an
 * AudioWorklet. The page sends intents over the worklet's message port and receives the same
 * events the desktop app sends. Browsers only allow sound after a user gesture, so the bridge
 * exposes an `audio` gate the interface uses to ask for one click.
 */
export function createWasmBridge(directory = ENGINE_DIRECTORY): Bridge {
  const router = new EventRouter();
  const listeners = new Set<() => void>();
  const queued: Intent[] = [];
  let state: AudioGateState = 'loading';
  let failure = '';
  let context: AudioContext | null = null;
  let node: AudioWorkletNode | null = null;
  let booting: Promise<void> | null = null;

  const set = (next: AudioGateState, message = '') => {
    if (state === next && failure === message) return;
    state = next;
    failure = message;
    listeners.forEach((listener) => {
      listener();
    });
  };
  const fail = (error: unknown) => {
    set('failed', error instanceof Error ? error.message : String(error));
  };
  const follow = () => {
    if (state !== 'failed') set(context?.state === 'running' ? 'running' : 'blocked');
  };

  const boot = async () => {
    if (typeof AudioContext === 'undefined' || typeof AudioWorkletNode === 'undefined')
      throw new Error('This browser cannot run the audio engine (no AudioWorklet).');
    const wasm = await fetch(new URL(`${directory}ap_web.wasm`, document.baseURI));
    if (!wasm.ok) throw new Error('The audio engine could not be downloaded.');
    const module = await WebAssembly.compile(await wasm.arrayBuffer());

    context = new AudioContext({ latencyHint: 'interactive' });
    context.onstatechange = follow;
    await context.audioWorklet.addModule(new URL(`${directory}worklet.js`, document.baseURI));
    node = new AudioWorkletNode(context, 'ap-engine', {
      numberOfInputs: 0,
      numberOfOutputs: 1,
      outputChannelCount: [2],
      processorOptions: { module },
    });
    node.port.onmessage = (message: MessageEvent<{ events?: unknown[]; failed?: string }>) => {
      const { events, failed } = message.data;
      if (failed) fail(new Error(failed));
      else if (events) for (const event of events) router.dispatch(event);
    };
    node.connect(context.destination);

    for (const intent of queued.splice(0)) node.port.postMessage({ intent });
    const latency =
      (context.baseLatency || 0) + ((context as { outputLatency?: number }).outputLatency || 0);
    node.port.postMessage({ latencyMs: 1000 * latency });
    follow();
  };

  const ensureBooted = () => {
    booting ??= boot().catch(fail);
    return booting;
  };
  void ensureBooted();

  const audio: AudioGate = {
    state: () => state,
    error: () => failure,
    subscribe: (listener) => {
      listeners.add(listener);
      return () => {
        listeners.delete(listener);
      };
    },
    start: async () => {
      await ensureBooted();
      if (!context || state === 'failed') return;
      try {
        await context.resume();
      } catch (error) {
        fail(error);
        return;
      }
      follow();
    },
  };

  return {
    kind: 'wasm',
    isNative: false,
    audio,
    send: (intent) => {
      if (node) node.port.postMessage({ intent });
      else if (queued.length < MAX_QUEUED_INTENTS) queued.push(intent);
    },
    on: (type, handler) => router.on(type, handler),
  };
}
