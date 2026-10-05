import type { ParameterDescriptor } from './generated';

const clamp = (value: number, min: number, max: number) => Math.min(max, Math.max(min, value));

/** Plain value -> [0, 1]. Mirrors ap::params::toNormalized in C++. */
export function toNormalized(d: ParameterDescriptor, value: number): number {
  const v = clamp(value, d.min, d.max);
  if (d.curve === 'logarithmic') return Math.log(v / d.min) / Math.log(d.max / d.min);
  return (v - d.min) / (d.max - d.min);
}

/** [0, 1] -> plain value. Mirrors ap::params::fromNormalized in C++. */
export function fromNormalized(d: ParameterDescriptor, normalized: number): number {
  const n = clamp(normalized, 0, 1);
  const value =
    d.curve === 'logarithmic' ? d.min * Math.pow(d.max / d.min, n) : d.min + n * (d.max - d.min);
  return clamp(value, d.min, d.max);
}

/** Rounds to the parameter's step, anchored at min, and keeps the result in range. */
export function snapToStep(d: ParameterDescriptor, value: number): number {
  const snapped = d.min + Math.round((value - d.min) / d.step) * d.step;
  return clamp(Number(snapped.toFixed(6)), d.min, d.max);
}

export function formatValue(d: ParameterDescriptor, value: number): string {
  const decimals = d.step >= 1 ? 0 : d.step >= 0.1 ? 1 : 2;
  const text = value.toFixed(decimals);
  return d.unit ? `${text} ${d.unit}` : text;
}
