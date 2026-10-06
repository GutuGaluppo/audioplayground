import type { TimelinePeaks } from '../bridge/generated';

type PeaksPayload = TimelinePeaks['payload'];

/** Detailed waveform peaks of one asset: 0..255 per slice of 1 / perSecond seconds. */
export interface Peaks {
  readonly perSecond: number;
  readonly data: Uint8Array;
}

const decoded = new WeakMap<PeaksPayload, Peaks | null>();

/** Decodes (once per event) the base64 peaks the native side sends; null if malformed. */
export function decodePeaks(event: PeaksPayload): Peaks | null {
  const cached = decoded.get(event);
  if (cached !== undefined) return cached;
  let peaks: Peaks | null;
  try {
    const binary = atob(event.data);
    const data = new Uint8Array(binary.length);
    for (let i = 0; i < binary.length; i++) data[i] = binary.charCodeAt(i);
    peaks = event.peaksPerSecond > 0 ? { perSecond: event.peaksPerSecond, data } : null;
  } catch {
    peaks = null; // not base64
  }
  decoded.set(event, peaks);
  return peaks;
}

/** Loudest peak (0..1) in the source time range [from, to) seconds; 0 outside the audio. */
export function peakIn(peaks: Peaks, from: number, to: number): number {
  const first = Math.max(0, Math.floor(from * peaks.perSecond));
  const last = Math.min(peaks.data.length, Math.max(first + 1, Math.ceil(to * peaks.perSecond)));
  let peak = 0;
  for (let i = first; i < last; i++) peak = Math.max(peak, peaks.data[i] ?? 0);
  return peak / 255;
}
