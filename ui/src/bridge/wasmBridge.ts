import { WebProjectLibrary } from '../web/projectLibrary';
import type { ProjectHost } from '../web/projectLibrary';
import { openIndexedDbStore } from '../web/projectStore';
import type { ProjectStore } from '../web/projectStore';
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
export function createWasmBridge(
  directory = ENGINE_DIRECTORY,
  openStore: () => Promise<ProjectStore> = () => openIndexedDbStore(),
): Bridge {
  const router = new EventRouter();
  const listeners = new Set<() => void>();
  const queued: Intent[] = [];
  let state: AudioGateState = 'loading';
  let failure = '';
  let context: AudioContext | null = null;
  let node: AudioWorkletNode | null = null;
  let booting: Promise<void> | null = null;

  // Requests to the engine that wait for an answer (saving and opening), and the engine being up.
  const waiting = new Map<number, (reply: Record<string, unknown>) => void>();
  let nextRequest = 1;
  let engineUp: () => void = () => undefined;
  const engineReady = new Promise<void>((resolve) => {
    engineUp = resolve;
  });

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
    node.port.onmessage = (
      message: MessageEvent<{
        events?: unknown[];
        failed?: string;
        ready?: boolean;
        reply?: { id: number } & Record<string, unknown>;
      }>,
    ) => {
      const { events, failed, ready, reply } = message.data;
      if (failed) fail(new Error(failed));
      else if (ready) engineUp();
      else if (reply) {
        waiting.get(reply.id)?.(reply);
        waiting.delete(reply.id);
      } else if (events) for (const event of events) router.dispatch(event);
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

  const call = async (command: Record<string, unknown>): Promise<Record<string, unknown>> => {
    await engineReady;
    return new Promise((resolve) => {
      const id = nextRequest++;
      waiting.set(id, resolve);
      node?.port.postMessage({ command: { id, ...command } });
    });
  };
  const notice = (level: 0 | 1 | 2, message: string) => {
    router.dispatch({ type: 'app.notice', payload: { level, message } });
  };
  const forward = (intent: Intent) => {
    if (node) node.port.postMessage({ intent });
    else if (queued.length < MAX_QUEUED_INTENTS) queued.push(intent);
  };

  // Songs are kept in the browser. The store opens in the background; if it cannot (some private
  // modes), saving says so instead of failing silently.
  const storeOpening = openStore();
  void storeOpening.catch(() => undefined);
  const lazyStore: ProjectStore = {
    list: async () => (await storeOpening).list(),
    read: async (id) => (await storeOpening).read(id),
    write: async (info, json) => (await storeOpening).write(info, json),
    remove: async (id) => (await storeOpening).remove(id),
    readAutosave: async () => (await storeOpening).readAutosave(),
    writeAutosave: async (autosave) => (await storeOpening).writeAutosave(autosave),
    clearAutosave: async () => (await storeOpening).clearAutosave(),
  };
  const host: ProjectHost = {
    exportProject: async (timestamp) => {
      const reply = await call({ kind: 'export', timestamp });
      if (reply['ok'] !== true || typeof reply['json'] !== 'string')
        throw new Error('The song could not be read from the engine.');
      return reply['json'];
    },
    importProject: async (json, options) => {
      const reply = await call({ kind: 'import', json, ...options });
      return reply['ok'] === true
        ? { ok: true }
        : {
            ok: false,
            error: typeof reply['error'] === 'string' ? reply['error'] : 'It is not a valid song.',
          };
    },
    markSaved: async (hasLocation) => {
      await call({ kind: 'saved', hasLocation });
    },
    markUnsaved: async () => {
      await call({ kind: 'unsaved' });
    },
    send: forward,
    notice,
  };
  const projects = new WebProjectLibrary({ store: lazyStore, host });
  router.on('project.state', (state) => {
    projects.projectState(state);
  });
  void engineReady.then(() => projects.start());

  // Do not lose work: keep it when the page is hidden, and warn before it is closed unsaved.
  if (typeof document !== 'undefined') {
    document.addEventListener('visibilitychange', () => {
      if (document.visibilityState === 'hidden') void projects.flush();
    });
    window.addEventListener('pagehide', () => {
      void projects.flush();
    });
    window.addEventListener('beforeunload', (event) => {
      if (!projects.isDirty()) return;
      event.preventDefault(); // the browser asks whether to leave
    });
  }

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
    projects,
    send: (intent) => {
      // New, Open, Save and Save As are questions about where songs are kept: the page answers.
      switch (intent.type) {
        case 'project.new':
          void projects.request('new');
          return;
        case 'project.open':
          void projects.request('open');
          return;
        case 'project.save':
          void projects.request('save');
          return;
        case 'project.saveAs':
          void projects.request('saveAs');
          return;
        default:
          forward(intent);
      }
    },
    on: (type, handler) => router.on(type, handler),
  };
}
