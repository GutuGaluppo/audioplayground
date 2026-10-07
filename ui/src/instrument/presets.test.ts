import { describe, expect, it } from 'vitest';

import drumData from '../../../presets/drums.json';
import synthData from '../../../presets/synth.json';
import { parseDrumPresets, parseSynthPresets, PAD_KEYS } from './presets';

describe('synth presets', () => {
  it('ships a file with no rejected preset, unique ids and sensible values', () => {
    const { presets, rejected } = parseSynthPresets(synthData);
    expect(rejected).toEqual([]);
    expect(presets.length).toBeGreaterThanOrEqual(6);
    expect(new Set(presets.map((p) => p.id)).size).toBe(presets.length);
    for (const p of presets) {
      expect(p.id.startsWith('synth.')).toBe(true);
      expect(p.values).toHaveLength(10);
    }
  });

  it('fills missing values with defaults and rejects bad ones', () => {
    const { presets, rejected } = parseSynthPresets({
      presets: [
        { id: 'a', processor: 'synth', name: 'A', version: 1, parameters: { cutoff: 1000 } },
        { id: 'b', processor: 'synth', name: 'B', version: 1, parameters: { cutoff: 1 } },
        { id: 'c', processor: 'synth', name: 'C', version: 1, parameters: { sparkle: 1 } },
        { id: 'd', processor: 'delay', name: 'D', version: 1, parameters: {} },
        { id: 'e', processor: 'synth', name: 'E', version: 2, parameters: {} },
      ],
    });
    expect(presets.map((p) => p.id)).toEqual(['a']);
    expect(presets[0]?.values[3]).toBe(1000);
    expect(presets[0]?.values[0]).toBe(2); // waveform: default saw
    expect(rejected).toEqual(['b', 'c', 'd', 'e']);
  });
});

describe('drum presets', () => {
  it('ships a file with no rejected pattern, each with something to play', () => {
    const { presets, rejected } = parseDrumPresets(drumData);
    expect(rejected).toEqual([]);
    expect(new Set(presets.map((p) => p.id)).size).toBe(presets.length);
    for (const p of presets) {
      expect(p.pads).toHaveLength(PAD_KEYS.length);
      expect(p.pads.some((mask) => mask > 0)).toBe(true);
      expect(p.pads.every((mask) => mask >= 0 && mask <= 0xffff)).toBe(true);
    }
  });

  it('turns x and . into step bits, step 0 being bit 0', () => {
    const { presets } = parseDrumPresets({
      presets: [
        {
          id: 'a',
          processor: 'drums',
          name: 'A',
          version: 1,
          pattern: { kick: 'x...x...x...x...', snare: '...............x' },
        },
      ],
    });
    expect(presets[0]?.pads[0]).toBe(0b0001000100010001);
    expect(presets[0]?.pads[1]).toBe(1 << 15);
    expect(presets[0]?.pads[2]).toBe(0);
  });

  it('rejects unknown pads, wrong lengths and other characters', () => {
    const base = { processor: 'drums', name: 'N', version: 1 };
    const { presets, rejected } = parseDrumPresets({
      presets: [
        { ...base, id: 'a', pattern: { cowbel: 'x...............' } },
        { ...base, id: 'b', pattern: { kick: 'x...' } },
        { ...base, id: 'c', pattern: { kick: 'X...............' } },
        { ...base, id: 'd', pattern: { kick: 'x...............' } },
      ],
    });
    expect(presets.map((p) => p.id)).toEqual(['d']);
    expect(rejected).toEqual(['a', 'b', 'c']);
  });
});
