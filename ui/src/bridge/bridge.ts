import { parseNativeEvent, PROTOCOL_VERSION } from './generated';
import type { Intent, NativeEventPayloads, NativeEventType } from './generated';

const INTENT_EVENT_ID = 'ap.intent';
const NATIVE_EVENT_ID = 'ap.event';

export type PayloadOf<T extends NativeEventType> = NativeEventPayloads[T];
type Handler<T extends NativeEventType> = (payload: PayloadOf<T>) => void;

/** Transport between the UI and the native host. The UI never processes audio (ADR-002). */
export interface Bridge {
  readonly isNative: boolean;
  send(intent: Intent): void;
  on<T extends NativeEventType>(type: T, handler: Handler<T>): () => void;
}

class EventRouter {
  private readonly handlers = new Map<NativeEventType, Set<(payload: unknown) => void>>();

  on<T extends NativeEventType>(type: T, handler: Handler<T>): () => void {
    let set = this.handlers.get(type);
    if (!set) {
      set = new Set();
      this.handlers.set(type, set);
    }
    const untyped = handler as (payload: unknown) => void;
    set.add(untyped);
    return () => set.delete(untyped);
  }

  dispatch(message: unknown): void {
    const event = parseNativeEvent(message);
    if (!event) {
      console.warn('Dropped malformed native event');
      return;
    }
    this.handlers.get(event.type)?.forEach((handler) => {
      handler(event.payload);
    });
  }
}

function createNativeBridge(juce: JuceGlobal): Bridge {
  const nativeVersion = juce.initialisationData['apProtocolVersion'];
  const version = Array.isArray(nativeVersion) ? (nativeVersion[0] as unknown) : nativeVersion;
  if (version !== PROTOCOL_VERSION) {
    console.error(
      `Bridge protocol mismatch: UI ${String(PROTOCOL_VERSION)}, native ${String(version)}`,
    );
  }

  const router = new EventRouter();
  juce.backend.addEventListener(NATIVE_EVENT_ID, (payload) => {
    router.dispatch(payload);
  });

  return {
    isNative: true,
    send: (intent) => {
      juce.backend.emitEvent(INTENT_EVENT_ID, intent);
    },
    on: (type, handler) => router.on(type, handler),
  };
}

/**
 * Stand-in used when the UI runs in a plain browser (pnpm ui:dev, tests). Simulates just enough
 * of the engine for the interface to be developed without the desktop app.
 */
export function createSimulatedBridge(): Bridge {
  const router = new EventRouter();
  let toneEnabled = false;
  let toneLevelDb = -18;
  let playing = false;
  let bpm = 120;
  let countInBars = 0;
  let metronomeEnabled = false;
  let beatsElapsed = 0;
  let countInBeatsLeft = 0;

  const sendStatus = () => {
    router.dispatch({
      type: 'engine.status',
      payload: {
        deviceName: 'Simulated output',
        sampleRate: 48000,
        bufferSize: 256,
        outputLatencyMs: 5.3,
        error: '',
        toneEnabled,
        toneLevelDb,
      },
    });
  };

  const sendTransportState = () => {
    router.dispatch({
      type: 'transport.state',
      payload: { playing, bpm, numerator: 4, denominator: 4, countInBars, metronomeEnabled },
    });
  };

  const sendPosition = () => {
    const countingIn = countInBeatsLeft > 0;
    const beats = countingIn ? -countInBeatsLeft : beatsElapsed;
    const bar = Math.floor(beats / 4) + 1;
    const beat = (((beats % 4) + 4) % 4) + 1;
    router.dispatch({ type: 'transport.position', payload: { bar, beat, countingIn } });
  };

  if (typeof window !== 'undefined') {
    window.setInterval(() => {
      const level = toneEnabled ? Math.pow(10, toneLevelDb / 20) : 0;
      router.dispatch({ type: 'engine.meters', payload: { peak: level } });
    }, 1000 / 30);

    const tick = () => {
      if (playing) {
        if (countInBeatsLeft > 0) countInBeatsLeft -= 1;
        else beatsElapsed += 1;
        sendPosition();
      }
      window.setTimeout(tick, 60000 / bpm);
    };
    window.setTimeout(tick, 60000 / bpm);
  }

  return {
    isNative: false,
    send: (intent) => {
      switch (intent.type) {
        case 'app.ready':
          queueMicrotask(() => {
            sendStatus();
            sendTransportState();
            sendPosition();
          });
          return;
        case 'audio.openSettings':
          console.info('Audio settings are only available in the desktop app');
          return;
        case 'tone.setEnabled':
          toneEnabled = intent.payload.enabled;
          break;
        case 'tone.setLevel':
          toneLevelDb = intent.payload.db;
          break;
        case 'transport.play':
          if (!playing) countInBeatsLeft = countInBars * 4;
          playing = true;
          queueMicrotask(sendTransportState);
          return;
        case 'transport.stop':
          playing = false;
          countInBeatsLeft = 0;
          queueMicrotask(sendTransportState);
          return;
        case 'transport.returnToStart':
          beatsElapsed = 0;
          queueMicrotask(sendPosition);
          return;
        case 'transport.setTempo':
          bpm = intent.payload.bpm;
          queueMicrotask(sendTransportState);
          return;
        case 'transport.setCountIn':
          countInBars = intent.payload.bars;
          queueMicrotask(sendTransportState);
          return;
        case 'metronome.setEnabled':
          metronomeEnabled = intent.payload.enabled;
          queueMicrotask(sendTransportState);
          return;
      }
      queueMicrotask(sendStatus);
    },
    on: (type, handler) => router.on(type, handler),
  };
}

export function createBridge(): Bridge {
  return window.__JUCE__ ? createNativeBridge(window.__JUCE__) : createSimulatedBridge();
}
