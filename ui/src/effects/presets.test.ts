import { describe, expect, it } from 'vitest';

import data from '../../../presets/effects.json';
import { EFFECTS } from '../params/generated';
import { parsePresets, PRESETS } from './presets';

describe('effect presets', () => {
  it('ships only valid presets, with unique ids, for every effect', () => {
    const { presets, rejected } = parsePresets(data);
    expect(rejected).toEqual([]);
    expect(new Set(presets.map((p) => p.id)).size).toBe(presets.length);
    for (let effect = 0; effect < EFFECTS.length; effect++)
      expect(PRESETS.some((p) => p.effect === effect)).toBe(true);
  });

  it('fills missing values with defaults and refuses unknown or out-of-range ones', () => {
    const { presets, rejected } = parsePresets({
      presets: [
        { id: 'a', processor: 'delay', name: 'A', version: 1, parameters: { time: 100 } },
        { id: 'b', processor: 'delay', name: 'B', version: 1, parameters: { time: 99999 } },
        { id: 'c', processor: 'delay', name: 'C', version: 1, parameters: { sparkle: 1 } },
        { id: 'd', processor: 'chorus', name: 'D', version: 1, parameters: {} },
        { id: 'e', processor: 'delay', name: 'E', version: 2, parameters: {} },
      ],
    });
    expect(presets.map((p) => p.id)).toEqual(['a']);
    expect(presets[0]?.values).toEqual([100, 35, 30, 0]);
    expect(rejected).toEqual(['b', 'c', 'd', 'e']);
  });
});
