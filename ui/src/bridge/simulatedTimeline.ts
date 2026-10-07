import { EFFECTS } from '../params/generated';
import type { Intent } from './generated';

/** Mutable mirrors of the generated (read-only) timeline records. */
interface SimNote {
  start: number;
  length: number;
  pitch: number;
  velocity: number;
}
interface SimClip {
  id: number;
  start: number;
  length: number;
  asset: number;
  sourceOffsetSeconds: number;
  contentOffset: number;
  loopLength: number;
  notes: SimNote[];
}
interface SimTrack {
  id: number;
  kind: number;
  name: string;
  volumeDb: number;
  pan: number;
  muted: boolean;
  soloed: boolean;
  clips: SimClip[];
  effects: { enabled: boolean; values: number[] }[];
  sends: { bus: number; levelDb: number }[];
}
interface SimBus {
  id: number;
  name: string;
  volumeDb: number;
  pan: number;
  muted: boolean;
  effects: { enabled: boolean; values: number[] }[];
}

export interface SimulatedEdit {
  key: string;
  gesture: number;
  label: string;
  before: unknown;
  after: unknown;
  set: (value: unknown) => void;
}

interface Host {
  dispatch: (message: unknown) => void;
  record: (edit: SimulatedEdit) => void;
  bpm: () => number;
  playhead: () => number;
}

const PPQ = 960;
const MIN_CLIP = PPQ / 32;
const BAR = 4 * PPQ;
const PATTERN = 16 * (PPQ / 4);
const KIND_NAMES = ['Audio', 'Synth', 'Sampler', 'Drums'];
const FAKE_ASSET = {
  id: 1,
  name: 'Simulated loop.wav',
  loaded: true,
  missing: false,
  loading: false,
  durationSeconds: 4,
  overview: Array.from(
    { length: 256 },
    (_, i) => Math.abs(Math.sin(i / 3)) * (0.4 + 0.5 * ((i % 32) / 32)),
  ),
};

// Detailed peaks (200 per second): a decaying hit on every eighth note at 120 BPM.
const FAKE_PEAKS = btoa(
  String.fromCharCode(
    ...Array.from({ length: 800 }, (_, i) => {
      const sinceHit = i % 50;
      const accent = Math.floor(i / 50) % 2 === 0 ? 1 : 0.6;
      return Math.round(255 * accent * Math.exp(-sinceHit / 12) * (0.8 + 0.2 * Math.sin(i)));
    }),
  ),
);

const clone = <T>(value: T): T => structuredClone(value);
const defaultEffects = () =>
  EFFECTS.map((e) => ({ enabled: false, values: e.parameters.map((p) => p.defaultValue) }));
const mod = (value: number, divisor: number) => ((value % divisor) + divisor) % divisor;
const byStart = (a: SimClip, b: SimClip) => a.start - b.start || a.id - b.id;
const byNote = (a: SimNote, b: SimNote) => a.start - b.start || a.pitch - b.pitch;

/**
 * Enough of the native timeline (ADR-007) for developing and testing the UI in a browser:
 * the same intents, the same events, snapshot-based undo. No audio.
 */
export function createSimulatedTimeline(host: Host) {
  let tracks: SimTrack[] = [];
  let buses: SimBus[] = [];
  let nextBus = 1;
  let nextTrack = 1;
  let nextClip = 1;
  let assetsUsed = false;

  const send = () => {
    host.dispatch({
      type: 'timeline.state',
      payload: { tracks: clone(tracks), buses: clone(buses) },
    });
  };
  const sendAssets = () => {
    host.dispatch({ type: 'timeline.assets', payload: { assets: assetsUsed ? [FAKE_ASSET] : [] } });
    if (assetsUsed)
      host.dispatch({
        type: 'timeline.peaks',
        payload: { asset: FAKE_ASSET.id, peaksPerSecond: 200, data: FAKE_PEAKS },
      });
  };

  // Applies change to a copy; records it for undo if it changed anything.
  const edit = (
    label: string,
    key: string,
    gesture: number,
    change: (draft: SimTrack[], draftBuses: SimBus[]) => boolean,
  ) => {
    const draft = clone(tracks);
    const draftBuses = clone(buses);
    if (
      !change(draft, draftBuses) ||
      JSON.stringify([draft, draftBuses]) === JSON.stringify([tracks, buses])
    ) {
      send();
      return;
    }
    host.record({
      key,
      gesture,
      label,
      before: clone({ tracks, buses }),
      after: { tracks: draft, buses: draftBuses },
      set: (value) => {
        const snapshot = clone(value as { tracks: SimTrack[]; buses: SimBus[] });
        tracks = snapshot.tracks;
        buses = snapshot.buses;
        send();
      },
    });
  };

  const locate = (draft: SimTrack[], id: number) => {
    for (const track of draft) {
      const index = track.clips.findIndex((c) => c.id === id);
      if (index >= 0) return { track, index, clip: track.clips[index] as SimClip };
    }
    return null;
  };

  const trimStart = (clip: SimClip, newStart: number): SimClip => {
    const end = clip.start + clip.length;
    const earliest =
      clip.asset !== 0
        ? clip.start - Math.floor((clip.sourceOffsetSeconds * host.bpm() * PPQ) / 60)
        : clip.loopLength > 0
          ? 0
          : clip.start - clip.contentOffset;
    const start = Math.min(end - MIN_CLIP, Math.max(Math.max(0, earliest), newStart));
    const delta = start - clip.start;
    const trimmed = { ...clip, start, length: end - start };
    if (clip.asset !== 0)
      trimmed.sourceOffsetSeconds = Math.max(
        0,
        clip.sourceOffsetSeconds + (delta * 60) / (host.bpm() * PPQ),
      );
    else {
      trimmed.contentOffset = clip.contentOffset + delta;
      if (clip.loopLength > 0) trimmed.contentOffset = mod(trimmed.contentOffset, clip.loopLength);
    }
    return trimmed;
  };

  const editNotes = (gesture: number, id: number, change: (notes: SimNote[]) => SimNote[]) => {
    edit('Edit notes', `notes:${String(id)}`, gesture, (draft) => {
      const found = locate(draft, id);
      if (!found) return false;
      found.clip.notes = change(found.clip.notes).sort(byNote);
      return true;
    });
  };

  const addClip = (draft: SimTrack[], track: SimTrack, clip: Omit<SimClip, 'id'>) => {
    track.clips.push({ ...clip, id: nextClip++ });
    track.clips.sort(byStart);
    return draft;
  };

  return {
    ready: () => {
      send();
      sendAssets();
    },

    /** Returns true if the intent belonged to the timeline. */
    handle: (intent: Intent): boolean => {
      switch (intent.type) {
        case 'track.add': {
          const { kind } = intent.payload;
          edit('Add track', 'track.add', 0, (draft) => {
            if (kind > 0 && draft.some((t) => t.kind === kind)) return false;
            const count = draft.filter((t) => t.kind === 0).length;
            draft.push({
              id: nextTrack++,
              kind,
              name: kind === 0 ? `Audio ${String(count + 1)}` : (KIND_NAMES[kind] ?? 'Track'),
              volumeDb: 0,
              pan: 0,
              muted: false,
              soloed: false,
              clips: [],
              effects: defaultEffects(),
              sends: [],
            });
            return true;
          });
          return true;
        }
        case 'track.setEffect': {
          const { track: id, effect, enabled, values, gesture } = intent.payload;
          const descriptor = EFFECTS[effect];
          edit(
            descriptor?.name ?? 'Effect',
            `track.setEffect:${String(id)}:${String(effect)}`,
            gesture,
            (draft) => {
              const track = draft.find((t) => t.id === id);
              const slot = track?.effects[effect];
              if (!descriptor || !slot || values.length !== descriptor.parameters.length)
                return false;
              slot.enabled = enabled;
              slot.values = values.map((v, i) => {
                const d = descriptor.parameters[i];
                return d ? Math.min(d.max, Math.max(d.min, v)) : v;
              });
              return true;
            },
          );
          return true;
        }
        case 'bus.add':
          edit('Add bus', 'bus.add', 0, (_, draftBuses) => {
            if (draftBuses.length >= 8) return false;
            draftBuses.push({
              id: nextBus++,
              name: `Bus ${String(draftBuses.length + 1)}`,
              volumeDb: 0,
              pan: 0,
              muted: false,
              effects: defaultEffects(),
            });
            return true;
          });
          return true;
        case 'bus.remove':
          edit('Delete bus', 'bus.remove', 0, (draft, draftBuses) => {
            const index = draftBuses.findIndex((b) => b.id === intent.payload.bus);
            if (index < 0) return false;
            draftBuses.splice(index, 1);
            for (const t of draft) t.sends = t.sends.filter((s) => s.bus !== intent.payload.bus);
            return true;
          });
          return true;
        case 'bus.rename':
        case 'bus.setVolume':
        case 'bus.setPan':
        case 'bus.setMute': {
          const labels = {
            'bus.rename': 'Rename bus',
            'bus.setVolume': 'Change bus volume',
            'bus.setPan': 'Change bus pan',
            'bus.setMute': 'Mute bus',
          } as const;
          const payload = intent.payload as { bus: number; gesture?: number };
          edit(
            labels[intent.type],
            `${intent.type}:${String(payload.bus)}`,
            payload.gesture ?? 0,
            (_, draftBuses) => {
              const bus = draftBuses.find((b) => b.id === payload.bus);
              if (!bus) return false;
              if (intent.type === 'bus.rename') bus.name = intent.payload.name.trim() || bus.name;
              if (intent.type === 'bus.setVolume') bus.volumeDb = intent.payload.volumeDb;
              if (intent.type === 'bus.setPan') bus.pan = intent.payload.pan;
              if (intent.type === 'bus.setMute') bus.muted = intent.payload.muted;
              return true;
            },
          );
          return true;
        }
        case 'bus.setEffect': {
          const { bus: id, effect, enabled, values, gesture } = intent.payload;
          const descriptor = EFFECTS[effect];
          edit(
            descriptor?.name ?? 'Effect',
            `bus.setEffect:${String(id)}:${String(effect)}`,
            gesture,
            (_, draftBuses) => {
              const slot = draftBuses.find((b) => b.id === id)?.effects[effect];
              if (!descriptor || !slot || values.length !== descriptor.parameters.length)
                return false;
              slot.enabled = enabled;
              slot.values = values.map((v, i) => {
                const d = descriptor.parameters[i];
                return d ? Math.min(d.max, Math.max(d.min, v)) : v;
              });
              return true;
            },
          );
          return true;
        }
        case 'track.setSend': {
          const { track: id, bus, levelDb, gesture } = intent.payload;
          edit('Change send', `track.setSend:${String(id)}:${String(bus)}`, gesture, (draft, b) => {
            const track = draft.find((t) => t.id === id);
            if (!track || !b.some((x) => x.id === bus)) return false;
            const level = Math.min(6, Math.max(-60, levelDb));
            const existing = track.sends.find((s) => s.bus === bus);
            if (level <= -60) track.sends = track.sends.filter((s) => s.bus !== bus);
            else if (existing) existing.levelDb = level;
            else track.sends.push({ bus, levelDb: level });
            return true;
          });
          return true;
        }
        case 'track.remove':
          edit('Delete track', 'track.remove', 0, (draft) => {
            const index = draft.findIndex((t) => t.id === intent.payload.track);
            if (index < 0) return false;
            draft.splice(index, 1);
            return true;
          });
          return true;
        case 'track.rename':
        case 'track.setVolume':
        case 'track.setPan':
        case 'track.setMute':
        case 'track.setSolo': {
          const labels = {
            'track.rename': 'Rename track',
            'track.setVolume': 'Change volume',
            'track.setPan': 'Change pan',
            'track.setMute': 'Mute track',
            'track.setSolo': 'Solo track',
          } as const;
          const payload = intent.payload as Record<string, unknown> & {
            track: number;
            gesture?: number;
          };
          edit(
            labels[intent.type],
            `${intent.type}:${String(payload.track)}`,
            payload.gesture ?? 0,
            (draft) => {
              const track = draft.find((t) => t.id === payload.track);
              if (!track) return false;
              if (intent.type === 'track.rename')
                track.name = intent.payload.name.trim() || track.name;
              if (intent.type === 'track.setVolume') track.volumeDb = intent.payload.volumeDb;
              if (intent.type === 'track.setPan') track.pan = intent.payload.pan;
              if (intent.type === 'track.setMute') track.muted = intent.payload.muted;
              if (intent.type === 'track.setSolo') track.soloed = intent.payload.soloed;
              return true;
            },
          );
          return true;
        }
        case 'track.importAudio': {
          const { track, ticks } = intent.payload;
          assetsUsed = true;
          sendAssets();
          edit('Import audio', 'import', 0, (draft) => {
            const target = draft.find((t) => t.id === track && t.kind === 0);
            if (!target) return false;
            addClip(draft, target, {
              start: ticks,
              length: Math.ceil((FAKE_ASSET.durationSeconds * host.bpm() * PPQ) / 60),
              asset: FAKE_ASSET.id,
              sourceOffsetSeconds: 0,
              contentOffset: 0,
              loopLength: 0,
              notes: [],
            });
            return true;
          });
          return true;
        }
        case 'clip.create': {
          const { track, start, length } = intent.payload;
          edit('Add clip', 'clip.create', 0, (draft) => {
            const target = draft.find((t) => t.id === track && t.kind > 0);
            if (!target) return false;
            addClip(draft, target, {
              start,
              length: Math.max(MIN_CLIP, length),
              asset: 0,
              sourceOffsetSeconds: 0,
              contentOffset: 0,
              loopLength: target.kind === 3 ? PATTERN : 0,
              notes: [],
            });
            return true;
          });
          return true;
        }
        case 'clip.move': {
          const { clip, track, start, gesture } = intent.payload;
          edit('Move clip', `move:${String(clip)}`, gesture, (draft) => {
            const found = locate(draft, clip);
            const target = draft.find((t) => t.id === track);
            if (!found || !target || (target.kind === 0) !== (found.track.kind === 0)) return false;
            found.track.clips.splice(found.index, 1);
            target.clips.push({ ...found.clip, start: Math.max(0, start) });
            target.clips.sort(byStart);
            return true;
          });
          return true;
        }
        case 'clip.resize': {
          const { clip, edge, ticks, gesture } = intent.payload;
          edit('Resize clip', `resize:${String(clip)}`, gesture, (draft) => {
            const found = locate(draft, clip);
            if (!found) return false;
            found.track.clips[found.index] =
              edge === 0
                ? trimStart(found.clip, ticks)
                : { ...found.clip, length: Math.max(MIN_CLIP, ticks - found.clip.start) };
            found.track.clips.sort(byStart);
            return true;
          });
          return true;
        }
        case 'clip.split': {
          const { clip, ticks } = intent.payload;
          edit('Split clip', 'split', 0, (draft) => {
            const found = locate(draft, clip);
            if (!found) return false;
            const original = found.clip;
            if (
              ticks < original.start + MIN_CLIP ||
              ticks > original.start + original.length - MIN_CLIP
            )
              return false;
            found.track.clips[found.index] = { ...original, length: ticks - original.start };
            addClip(draft, found.track, trimStart(original, ticks));
            return true;
          });
          return true;
        }
        case 'clip.remove':
          edit('Delete clip', 'remove', 0, (draft) => {
            const found = locate(draft, intent.payload.clip);
            if (!found) return false;
            found.track.clips.splice(found.index, 1);
            return true;
          });
          return true;
        case 'clip.duplicate':
          edit('Add clip', 'duplicate', 0, (draft) => {
            const found = locate(draft, intent.payload.clip);
            if (!found) return false;
            addClip(draft, found.track, {
              ...clone(found.clip),
              start: found.clip.start + found.clip.length,
            });
            return true;
          });
          return true;
        case 'clip.setLoop':
          edit('Loop clip', 'loop', 0, (draft) => {
            const found = locate(draft, intent.payload.clip);
            if (!found || found.clip.asset !== 0) return false;
            found.clip.loopLength = intent.payload.enabled
              ? found.track.kind === 3
                ? PATTERN
                : found.clip.length
              : 0;
            if (found.clip.loopLength > 0) found.clip.contentOffset %= found.clip.loopLength;
            return true;
          });
          return true;
        case 'clip.addNote': {
          const { clip, start, length, pitch, velocity } = intent.payload;
          editNotes(0, clip, (notes) => [
            ...notes.filter((n) => !(n.start === start && n.pitch === pitch)),
            { start, length, pitch, velocity },
          ]);
          return true;
        }
        case 'clip.removeNote': {
          const { clip, start, pitch } = intent.payload;
          editNotes(0, clip, (notes) =>
            notes.filter((n) => !(n.start === start && n.pitch === pitch)),
          );
          return true;
        }
        case 'clip.editNote': {
          const { clip, fromStart, fromPitch, start, length, pitch, gesture } = intent.payload;
          editNotes(gesture, clip, (notes) => {
            const old = notes.find((n) => n.start === fromStart && n.pitch === fromPitch);
            if (!old) return notes;
            return [
              ...notes.filter((n) => n !== old && !(n.start === start && n.pitch === pitch)),
              { start, length, pitch, velocity: old.velocity },
            ];
          });
          return true;
        }
        case 'drums.setStep': {
          const { clip, pad, step, on, gesture } = intent.payload;
          const pitch = 36 + pad;
          const start = step * (PPQ / 4);
          const toggle = (notes: SimNote[]) => {
            const rest = notes.filter((n) => !(n.start === start && n.pitch === pitch));
            return on ? [...rest, { start, length: PPQ / 4, pitch, velocity: 1 }] : rest;
          };
          if (clip !== 0) {
            editNotes(gesture, clip, toggle);
            return true;
          }
          edit('Add pattern', `notes:pattern`, gesture, (draft) => {
            const barStart = Math.floor(host.playhead() / BAR) * BAR;
            let drums = draft.find((t) => t.kind === 3);
            const existing = drums?.clips.find(
              (c) => c.start <= barStart && barStart < c.start + c.length,
            );
            if (existing) {
              existing.notes = toggle(existing.notes).sort(byNote);
              return true;
            }
            if (!on) return false;
            if (!drums) {
              drums = {
                id: nextTrack++,
                kind: 3,
                name: 'Drums',
                volumeDb: 0,
                pan: 0,
                muted: false,
                soloed: false,
                clips: [],
                effects: defaultEffects(),
                sends: [],
              };
              draft.push(drums);
            }
            addClip(draft, drums, {
              start: barStart,
              length: 4 * BAR,
              asset: 0,
              sourceOffsetSeconds: 0,
              contentOffset: 0,
              loopLength: PATTERN,
              notes: toggle([]),
            });
            return true;
          });
          return true;
        }
        case 'drums.clear':
          editNotes(0, intent.payload.clip, () => []);
          return true;
        default:
          return false;
      }
    },
  };
}
