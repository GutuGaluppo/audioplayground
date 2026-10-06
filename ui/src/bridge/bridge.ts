import { isParamId, PARAMETERS } from '../params/generated';
import type { ParamId } from '../params/generated';
import { parseNativeEvent, PROTOCOL_VERSION } from './generated';
import type { Intent, NativeEventPayloads, NativeEventType } from './generated';
import { createSimulatedTimeline } from './simulatedTimeline';
import type { SimulatedEdit } from './simulatedTimeline';

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
  const params = new Map<ParamId, number>(
    Object.values(PARAMETERS).map((d) => [d.id, d.defaultValue] as const),
  );
  const sendParam = (id: ParamId) => {
    router.dispatch({
      type: 'param.value',
      payload: { id, value: params.get(id) ?? PARAMETERS[id].defaultValue },
    });
  };
  const setParam = (id: ParamId) => (value: unknown) => {
    params.set(id, value as number);
    sendParam(id);
  };
  const setTempo = (value: unknown) => {
    bpm = value as number;
    sendTransportState();
  };
  let playing = false;
  let bpm = 120;
  let countInBars = 0;
  let metronomeEnabled = false;
  let beatsElapsed = 0;
  let countInBeatsLeft = 0;
  let recording = false;
  let armedTrack = 0; // the simulated "microphone" is gentle noise on the input meter
  let loop = { enabled: false, start: 0, end: 0 };

  // Minimal stand-in for the native undo history (same merge rule: same gesture and target).
  type Edit = SimulatedEdit;
  let projectName = 'Untitled';
  let dirty = false;
  let hasLocation = false;
  const sendProject = () => {
    router.dispatch({ type: 'project.state', payload: { name: projectName, dirty, hasLocation } });
  };
  const notice = (message: string) => {
    router.dispatch({ type: 'app.notice', payload: { level: 0, message } });
  };

  const FACTORY = [
    'Kick',
    'Snare',
    'Closed Hat',
    'Open Hat',
    'Clap',
    'Low Tom',
    'Mid Tom',
    'High Tom',
    'Rim',
    'Cowbell',
    'Shaker',
    'Crash',
    'Perc Low',
    'Perc High',
    'Bass',
    'Zap',
  ];
  const drumPads = FACTORY.map((name) => ({ name, volumeDb: 0, pitch: 0, muted: false }));
  const sendPad = (pad: number) => {
    const info = drumPads[pad];
    if (!info) return;
    router.dispatch({
      type: 'drums.pad',
      payload: { pad, ...info, custom: false, missing: false },
    });
  };

  const undoStack: Edit[] = [];
  const redoStack: Edit[] = [];

  const sendHistory = () => {
    router.dispatch({
      type: 'history.state',
      payload: {
        canUndo: undoStack.length > 0,
        canRedo: redoStack.length > 0,
        undoLabel: undoStack.at(-1)?.label ?? '',
        redoLabel: redoStack.at(-1)?.label ?? '',
      },
    });
  };

  const timeline = createSimulatedTimeline({
    dispatch: (message) => {
      router.dispatch(message);
    },
    record: (edit) => {
      record(edit);
    },
    bpm: () => bpm,
    playhead: () => beatsElapsed * 960,
  });

  const record = (edit: Edit) => {
    redoStack.length = 0;
    const last = undoStack.at(-1);
    if (edit.gesture !== 0 && last && last.gesture === edit.gesture && last.key === edit.key)
      last.after = edit.after;
    else undoStack.push(edit);
    edit.set(edit.after);
    dirty = true;
    sendHistory();
    sendProject();
  };

  const sendStatus = () => {
    router.dispatch({
      type: 'engine.status',
      payload: {
        deviceName: 'Simulated output',
        sampleRate: 48000,
        bufferSize: 256,
        outputLatencyMs: 5.3,
        inputName: armedTrack ? 'Simulated microphone' : '',
        inputChannels: armedTrack ? 1 : 0,
        roundTripLatencyMs: armedTrack ? 10.6 : 0,
        error: '',
        toneEnabled,
      },
    });
  };

  const sendTransportState = () => {
    router.dispatch({
      type: 'transport.state',
      payload: {
        playing,
        bpm,
        numerator: 4,
        denominator: 4,
        countInBars,
        metronomeEnabled,
        recording,
        armedTrack,
        loopEnabled: loop.enabled,
        loopStart: loop.start,
        loopEnd: loop.end,
      },
    });
  };

  const sendPosition = () => {
    const countingIn = countInBeatsLeft > 0;
    const beats = countingIn ? -countInBeatsLeft : beatsElapsed;
    const bar = Math.floor(beats / 4) + 1;
    const beat = (((beats % 4) + 4) % 4) + 1;
    router.dispatch({
      type: 'transport.position',
      payload: { bar, beat, countingIn, ticks: beats * 960 },
    });
  };

  if (typeof window !== 'undefined') {
    window.setInterval(() => {
      const level = toneEnabled ? Math.pow(10, (params.get('tone.level') ?? -18) / 20) : 0;
      const inputPeak = armedTrack ? 0.05 + Math.random() * 0.1 : 0;
      router.dispatch({ type: 'engine.meters', payload: { peak: level, inputPeak } });
    }, 1000 / 30);

    const tick = () => {
      if (playing) {
        if (countInBeatsLeft > 0) countInBeatsLeft -= 1;
        else beatsElapsed += 1;
        if (loop.enabled && beatsElapsed * 960 >= loop.end)
          beatsElapsed = Math.floor(loop.start / 960);
        sendPosition();
      }
      window.setTimeout(tick, 60000 / bpm);
    };
    window.setTimeout(tick, 60000 / bpm);
  }

  return {
    isNative: false,
    send: (intent) => {
      if (timeline.handle(intent)) return;
      switch (intent.type) {
        case 'app.ready':
          queueMicrotask(() => {
            sendStatus();
            sendTransportState();
            sendPosition();
            for (const id of params.keys()) sendParam(id);
            sendHistory();
            sendProject();
            router.dispatch({ type: 'instrument.state', payload: { instrument: 0 } });
            timeline.ready();
            drumPads.forEach((_, pad) => {
              sendPad(pad);
            });
            router.dispatch({
              type: 'sampler.state',
              payload: {
                name: '',
                loaded: false,
                missing: false,
                loading: false,
                durationSeconds: 0,
                overview: [],
              },
            });
          });
          return;
        case 'audio.openSettings':
          console.info('Audio settings are only available in the desktop app');
          return;
        case 'tone.setEnabled':
          toneEnabled = intent.payload.enabled;
          break;
        case 'param.set': {
          const { id, value, gesture } = intent.payload;
          if (!isParamId(id) || value < PARAMETERS[id].min || value > PARAMETERS[id].max) return;
          const before = params.get(id) ?? PARAMETERS[id].defaultValue;
          queueMicrotask(() => {
            if (value === before) sendParam(id);
            else
              record({
                key: id,
                gesture,
                label: PARAMETERS[id].name,
                before,
                after: value,
                set: setParam(id),
              });
          });
          return;
        }
        case 'transport.play':
          if (!playing) countInBeatsLeft = countInBars * 4;
          playing = true;
          queueMicrotask(sendTransportState);
          return;
        case 'transport.stop':
          playing = false;
          recording = false;
          countInBeatsLeft = 0;
          queueMicrotask(sendTransportState);
          return;
        case 'transport.record':
          if (recording) {
            recording = false;
            playing = false;
          } else {
            recording = true;
            if (!playing) countInBeatsLeft = countInBars * 4;
            playing = true;
          }
          queueMicrotask(sendTransportState);
          return;
        case 'track.setArmed':
          armedTrack = intent.payload.armed ? intent.payload.track : 0;
          queueMicrotask(() => {
            sendStatus();
            sendTransportState();
          });
          return;
        case 'transport.seek':
          beatsElapsed = Math.floor(intent.payload.ticks / 960);
          queueMicrotask(sendPosition);
          return;
        case 'transport.setLoop': {
          const { enabled, start, end } = intent.payload;
          loop = { enabled: enabled && end > start, start, end: end > start ? end : start };
          queueMicrotask(sendTransportState);
          return;
        }
        case 'transport.returnToStart':
          beatsElapsed = 0;
          queueMicrotask(sendPosition);
          return;
        case 'transport.setTempo': {
          const after = intent.payload.bpm;
          if (after === bpm) {
            queueMicrotask(sendTransportState);
            return;
          }
          const before = bpm;
          queueMicrotask(() => {
            record({
              key: 'tempo',
              gesture: 0,
              label: 'Change tempo',
              before,
              after,
              set: setTempo,
            });
          });
          return;
        }
        case 'project.new':
        case 'project.open':
          queueMicrotask(() => {
            notice('Opening and creating projects needs the desktop app.');
          });
          return;
        case 'project.save':
        case 'project.saveAs':
          queueMicrotask(() => {
            dirty = false;
            hasLocation = true;
            sendProject();
            notice('Saved (simulated).');
          });
          return;
        case 'project.rename':
          queueMicrotask(() => {
            const name = intent.payload.name.trim();
            if (name) {
              projectName = name;
              dirty = true;
            }
            sendProject();
          });
          return;
        case 'instrument.select': {
          const instrument = intent.payload.instrument;
          queueMicrotask(() => {
            router.dispatch({ type: 'instrument.state', payload: { instrument } });
          });
          return;
        }
        case 'sampler.load':
          queueMicrotask(() => {
            router.dispatch({
              type: 'sampler.state',
              payload: {
                name: 'Simulated sample.wav',
                loaded: true,
                missing: false,
                loading: false,
                durationSeconds: 1.5,
                overview: Array.from(
                  { length: 128 },
                  (_, i) => Math.abs(Math.sin(i / 6)) * Math.exp(-i / 60),
                ),
              },
            });
          });
          return;
        case 'drums.setPad':
          queueMicrotask(() => {
            const { pad, volumeDb, pitch, muted } = intent.payload;
            const info = drumPads[pad];
            if (info) Object.assign(info, { volumeDb, pitch, muted });
            sendPad(pad);
          });
          return;
        case 'drums.trigger':
        case 'drums.resetPad':
          return;
        case 'drums.loadPad':
          queueMicrotask(() => {
            notice('Loading pad samples needs the desktop app.');
          });
          return;
        case 'note.on':
        case 'note.off':
        case 'note.allOff':
          return; // no sound in the browser simulation
        case 'edit.undo':
          queueMicrotask(() => {
            const edit = undoStack.pop();
            if (!edit) return;
            edit.set(edit.before);
            redoStack.push(edit);
            dirty = true;
            sendHistory();
            sendProject();
          });
          return;
        case 'edit.redo':
          queueMicrotask(() => {
            const edit = redoStack.pop();
            if (!edit) return;
            edit.set(edit.after);
            undoStack.push(edit);
            dirty = true;
            sendHistory();
            sendProject();
          });
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
