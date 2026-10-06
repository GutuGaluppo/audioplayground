import type { EFFECTS } from '../params/generated';

const W = 240;
const H = 110;
const F_MIN = 20;
const F_MAX = 20000;
const SAMPLE_RATE = 48000;
const POINTS = 96;

type Biquad = readonly [number, number, number, number, number, number]; // b0 b1 b2 a0 a1 a2

function magnitudeDb(filters: readonly Biquad[], freq: number): number {
  const w = (2 * Math.PI * freq) / SAMPLE_RATE;
  const cos1 = Math.cos(w);
  const sin1 = Math.sin(w);
  const cos2 = Math.cos(2 * w);
  const sin2 = Math.sin(2 * w);
  let total = 0;
  for (const [b0, b1, b2, a0, a1, a2] of filters) {
    const nr = b0 + b1 * cos1 + b2 * cos2;
    const ni = -(b1 * sin1 + b2 * sin2);
    const dr = a0 + a1 * cos1 + a2 * cos2;
    const di = -(a1 * sin1 + a2 * sin2);
    total += 10 * Math.log10((nr * nr + ni * ni) / (dr * dr + di * di));
  }
  return total;
}

// RBJ cookbook shelves/peak/low-pass/high-pass/band-pass.
function shelf(kind: 'low' | 'high', freq: number, gainDb: number): Biquad {
  const A = 10 ** (gainDb / 40);
  const w = (2 * Math.PI * freq) / SAMPLE_RATE;
  const cos = Math.cos(w);
  const alpha = Math.sin(w) / Math.SQRT2;
  const k = 2 * Math.sqrt(A) * alpha;
  const s = kind === 'low' ? 1 : -1;
  return [
    A * (A + 1 - s * (A - 1) * cos + k),
    s * 2 * A * (A - 1 - s * (A + 1) * cos),
    A * (A + 1 - s * (A - 1) * cos - k),
    A + 1 + s * (A - 1) * cos + k,
    -s * 2 * (A - 1 + s * (A + 1) * cos),
    A + 1 + s * (A - 1) * cos - k,
  ];
}

function peak(freq: number, gainDb: number, q: number): Biquad {
  const A = 10 ** (gainDb / 40);
  const w = (2 * Math.PI * freq) / SAMPLE_RATE;
  const alpha = Math.sin(w) / (2 * q);
  const cos = Math.cos(w);
  return [1 + alpha * A, -2 * cos, 1 - alpha * A, 1 + alpha / A, -2 * cos, 1 - alpha / A];
}

function pass(mode: number, freq: number, q: number): Biquad {
  const w = (2 * Math.PI * freq) / SAMPLE_RATE;
  const alpha = Math.sin(w) / (2 * q);
  const cos = Math.cos(w);
  const den: [number, number, number] = [1 + alpha, -2 * cos, 1 - alpha];
  if (mode === 1) return [(1 + cos) / 2, -(1 + cos), (1 + cos) / 2, ...den];
  if (mode === 2) return [alpha, 0, -alpha, ...den];
  return [(1 - cos) / 2, 1 - cos, (1 - cos) / 2, ...den];
}

const x = (freq: number) => (Math.log(freq / F_MIN) / Math.log(F_MAX / F_MIN)) * W;

function Frame({ rows, children }: { rows: readonly number[]; children: React.ReactNode }) {
  return (
    <svg className="curve" viewBox={`0 0 ${String(W)} ${String(H)}`} aria-hidden="true">
      {[100, 1000, 10000].map((f) => (
        <line key={f} className="curve__grid" x1={x(f)} x2={x(f)} y1="0" y2={H} />
      ))}
      {rows.map((y) => (
        <line key={y} className="curve__grid" x1="0" x2={W} y1={y} y2={y} />
      ))}
      {children}
    </svg>
  );
}

function path(dbAt: (f: number) => number, range: number): string {
  return Array.from({ length: POINTS }, (_, i) => {
    const f = F_MIN * (F_MAX / F_MIN) ** (i / (POINTS - 1));
    const y = H / 2 - (Math.max(-range, Math.min(range, dbAt(f))) / range) * (H / 2 - 4);
    return `${i === 0 ? 'M' : 'L'}${((i / (POINTS - 1)) * W).toFixed(1)} ${y.toFixed(1)}`;
  }).join('');
}

/** Frequency response of the EQ's three bands, drawn from the current values. */
export function EqCurve({ values }: { values: readonly number[] }) {
  const [lowF = 120, lowG = 0, midF = 1000, midQ = 1, midG = 0, highF = 8000, highG = 0] = values;
  const filters = [shelf('low', lowF, lowG), peak(midF, midG, midQ), shelf('high', highF, highG)];
  const d = path((f) => magnitudeDb(filters, f), 18);
  return (
    <Frame rows={[H / 2]}>
      <path className="curve__fill" d={`${d}L${String(W)} ${String(H / 2)}L0 ${String(H / 2)}Z`} />
      <path className="curve__line" d={d} />
    </Frame>
  );
}

/** Frequency response of the filter. */
export function FilterCurve({ values }: { values: readonly number[] }) {
  const [mode = 0, cutoff = 1000, resonance = 0.71] = values;
  const filters = [pass(Math.round(mode), cutoff, resonance)];
  const d = path((f) => magnitudeDb(filters, f), 24);
  return (
    <Frame rows={[H / 2]}>
      <path className="curve__fill" d={`${d}L${String(W)} ${String(H)}L0 ${String(H)}Z`} />
      <path className="curve__line" d={d} />
    </Frame>
  );
}

/** Static transfer curve of the compressor (input level against output level). */
export function CompressorCurve({ values }: { values: readonly number[] }) {
  const [threshold = -18, ratio = 4, , , makeup = 0] = values;
  const out = (inDb: number) =>
    (inDb <= threshold ? inDb : threshold + (inDb - threshold) / ratio) + makeup;
  const span = 60;
  const sx = (db: number) => ((db + span) / span) * W;
  const sy = (db: number) => H - ((Math.max(-span, Math.min(0, db)) + span) / span) * (H - 4) - 2;
  const d = Array.from({ length: 61 }, (_, i) => {
    const inDb = -span + i;
    return `${i === 0 ? 'M' : 'L'}${sx(inDb).toFixed(1)} ${sy(out(inDb)).toFixed(1)}`;
  }).join('');
  return (
    <svg className="curve" viewBox={`0 0 ${String(W)} ${String(H)}`} aria-hidden="true">
      <line className="curve__grid" x1="0" y1={H} x2={W} y2="0" />
      <line className="curve__grid" x1={sx(threshold)} x2={sx(threshold)} y1="0" y2={H} />
      <path className="curve__line" d={d} />
    </svg>
  );
}

export const CURVES: Partial<
  Record<(typeof EFFECTS)[number]['id'], (props: { values: readonly number[] }) => React.ReactNode>
> = {
  eq: EqCurve,
  filter: FilterCurve,
  compressor: CompressorCurve,
};
