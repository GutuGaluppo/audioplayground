import { describe, expect, it } from 'vitest';

import type { ParameterDescriptor } from './generated';
import { formatValue, fromNormalized, snapToStep, toNormalized } from './mapping';

const linear: ParameterDescriptor = {
  id: 'tone.level',
  name: 'Linear',
  unit: 'dB',
  min: -60,
  max: 0,
  defaultValue: -12,
  step: 0.5,
  curve: 'linear',
};

const log: ParameterDescriptor = {
  ...linear,
  unit: 'Hz',
  min: 20,
  max: 20480,
  step: 1,
  curve: 'logarithmic',
};

describe('parameter mapping (mirrors the C++ tests)', () => {
  it('maps linear ranges onto [0, 1]', () => {
    expect(toNormalized(linear, -60)).toBe(0);
    expect(toNormalized(linear, 0)).toBe(1);
    expect(toNormalized(linear, -30)).toBeCloseTo(0.5, 6);
    expect(toNormalized(linear, 100)).toBe(1);
    expect(fromNormalized(linear, 2)).toBe(0);
  });

  it('gives equal travel per octave on logarithmic ranges', () => {
    expect(fromNormalized(log, 0)).toBeCloseTo(20, 4);
    expect(fromNormalized(log, 1)).toBeCloseTo(20480, 2);
    expect(fromNormalized(log, 0.5)).toBeCloseTo(640, 2);
    expect(toNormalized(log, 40)).toBeCloseTo(0.1, 4);
  });

  it('round-trips through normalised values', () => {
    for (let n = 0; n <= 1; n += 0.05) {
      expect(toNormalized(log, fromNormalized(log, n))).toBeCloseTo(n, 6);
      expect(toNormalized(linear, fromNormalized(linear, n))).toBeCloseTo(n, 6);
    }
  });

  it('snaps to the step and stays in range', () => {
    expect(snapToStep(linear, -12.26)).toBe(-12.5);
    expect(snapToStep(linear, -12.24)).toBe(-12);
    expect(snapToStep(linear, 5)).toBe(0);
  });

  it('formats with the unit and a precision matching the step', () => {
    expect(formatValue(linear, -12)).toBe('-12.0 dB');
    expect(formatValue(log, 440.4)).toBe('440 Hz');
  });
});
