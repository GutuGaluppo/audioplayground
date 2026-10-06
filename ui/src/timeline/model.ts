import type { TimelineClip, TimelineTrack } from '../bridge/generated';

/** Ticks per quarter note, as in the native engine (ADR-004). */
export const PPQ = 960;
export const MAX_TICKS = 38_400_000;
export const MIN_CLIP_TICKS = PPQ / 32;
export const STEP_TICKS = PPQ / 4; // drum steps are sixteenths
export const PATTERN_TICKS = 16 * STEP_TICKS;
export const FIRST_PAD_NOTE = 36;

/** Track kinds as sent by native code: 0 audio, then one per instrument. */
export const TrackKind = { audio: 0, synth: 1, sampler: 2, drums: 3 } as const;
export type TrackKindValue = (typeof TrackKind)[keyof typeof TrackKind];

/** Instrument index (as used by instrument.select) for a track kind, or null for audio. */
export function instrumentOf(kind: number): number | null {
  return kind >= 1 && kind <= 3 ? kind - 1 : null;
}

export function trackKindOfInstrument(instrument: number): number {
  return instrument + 1;
}

export function kindLabel(kind: number): string {
  return ['Audio', 'Synth', 'Sampler', 'Drums'][kind] ?? 'Track';
}

export function ticksPerBar(numerator: number, denominator: number): number {
  return (PPQ * 4 * numerator) / denominator;
}

export function ticksPerBeat(denominator: number): number {
  return (PPQ * 4) / denominator;
}

export function clamp(value: number, min: number, max: number): number {
  return Math.min(max, Math.max(min, value));
}

/** Rounds to the nearest multiple of grid (grid <= 1: unchanged, rounded to a tick). */
export function snap(ticks: number, grid: number): number {
  return grid > 1 ? Math.round(ticks / grid) * grid : Math.round(ticks);
}

export function clipEnd(clip: TimelineClip): number {
  return clip.start + clip.length;
}

export function isAudioClip(clip: TimelineClip): boolean {
  return clip.asset !== 0;
}

function positiveModulo(value: number, divisor: number): number {
  return ((value % divisor) + divisor) % divisor;
}

/** Content time (after looping) at a timeline position inside a note clip. */
export function contentTimeAt(clip: TimelineClip, ticks: number): number {
  const content = clip.contentOffset + (ticks - clip.start);
  return clip.loopLength > 0 ? positiveModulo(content, clip.loopLength) : content;
}

/** The note clip's content length to show in an editor: one loop, or what the clip reveals. */
export function contentLength(clip: TimelineClip): number {
  if (clip.loopLength > 0) return clip.loopLength;
  const lastNote = clip.notes.reduce((end, n) => Math.max(end, n.start + n.length), 0);
  return Math.max(clip.contentOffset + clip.length, lastNote);
}

/** Every place a note sounds inside the clip, as timeline ticks [start, end). */
export function noteSpans(clip: TimelineClip): { start: number; end: number; pitch: number }[] {
  const spans: { start: number; end: number; pitch: number }[] = [];
  const end = clipEnd(clip);
  const loop = clip.loopLength;
  if (loop <= 0) {
    for (const note of clip.notes) {
      const start = clip.start + note.start - clip.contentOffset;
      const stop = Math.min(start + note.length, end);
      if (stop > clip.start && start < end)
        spans.push({ start: Math.max(start, clip.start), end: stop, pitch: note.pitch });
    }
    return spans;
  }
  const first = Math.floor(clip.contentOffset / loop);
  const last = Math.floor((clip.contentOffset + clip.length - 1) / loop);
  for (let k = first; k <= last && spans.length < 4000; k++) {
    for (const note of clip.notes) {
      if (note.start >= loop) continue;
      const start = clip.start + k * loop + note.start - clip.contentOffset;
      const stop = Math.min(start + Math.min(note.length, loop - note.start), end);
      if (stop > clip.start && start < end)
        spans.push({ start: Math.max(start, clip.start), end: stop, pitch: note.pitch });
    }
  }
  return spans;
}

export function findClip(
  tracks: readonly TimelineTrack[],
  id: number,
): { track: TimelineTrack; clip: TimelineClip } | null {
  for (const track of tracks) {
    const clip = track.clips.find((c) => c.id === id);
    if (clip) return { track, clip };
  }
  return null;
}

/** Last tick used by any clip. */
export function timelineEnd(tracks: readonly TimelineTrack[]): number {
  let end = 0;
  for (const track of tracks) for (const clip of track.clips) end = Math.max(end, clipEnd(clip));
  return end;
}

/** The drum pattern the step grid edits: the selected drum clip, else the one at the playhead. */
export function activePatternClip(
  tracks: readonly TimelineTrack[],
  selectedClip: number | null,
  playhead: number,
): TimelineClip | null {
  const drums = tracks.find((t) => t.kind === TrackKind.drums);
  if (!drums) return null;
  const selected = drums.clips.find((c) => c.id === selectedClip);
  if (selected) return selected;
  return drums.clips.find((c) => c.start <= playhead && playhead < clipEnd(c)) ?? null;
}

export function hasDrumStep(clip: TimelineClip, pad: number, step: number): boolean {
  return clip.notes.some((n) => n.start === step * STEP_TICKS && n.pitch === FIRST_PAD_NOTE + pad);
}

export function noteName(pitch: number): string {
  const names = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
  return `${names[pitch % 12] ?? ''}${String(Math.floor(pitch / 12) - 1)}`;
}
