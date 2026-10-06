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

const clone = <T>(value: T): T => structuredClone(value);
const mod = (value: number, divisor: number) => ((value % divisor) + divisor) % divisor;
const byStart = (a: SimClip, b: SimClip) => a.start - b.start || a.id - b.id;
const byNote = (a: SimNote, b: SimNote) => a.start - b.start || a.pitch - b.pitch;

/**
 * Enough of the native timeline (ADR-007) for developing and testing the UI in a browser:
 * the same intents, the same events, snapshot-based undo. No audio.
 */
export function createSimulatedTimeline(host: Host) {
  let tracks: SimTrack[] = [];
  let nextTrack = 1;
  let nextClip = 1;
  let assetsUsed = false;

  const send = () => {
    host.dispatch({ type: 'timeline.state', payload: { tracks: clone(tracks) } });
  };
  const sendAssets = () => {
    host.dispatch({ type: 'timeline.assets', payload: { assets: assetsUsed ? [FAKE_ASSET] : [] } });
  };

  // Applies change to a copy; records it for undo if it changed anything.
  const edit = (
    label: string,
    key: string,
    gesture: number,
    change: (draft: SimTrack[]) => boolean,
  ) => {
    const draft = clone(tracks);
    if (!change(draft) || JSON.stringify(draft) === JSON.stringify(tracks)) {
      send();
      return;
    }
    host.record({
      key,
      gesture,
      label,
      before: clone(tracks),
      after: draft,
      set: (value) => {
        tracks = clone(value as SimTrack[]);
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
            });
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
