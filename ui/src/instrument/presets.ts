import drumData from '../../../presets/drums.json';
import synthData from '../../../presets/synth.json';
import { PARAMETERS } from '../params/generated';
import type { ParamId } from '../params/generated';
import { SYNTH_PRESET_IDS } from '../params/synthPreset';

export interface SynthPreset {
  readonly id: string;
  readonly name: string;
  /** Values in the order of SYNTH_PRESET_IDS (what `synth.setPreset` sends). */
  readonly values: readonly number[];
}

export interface DrumPreset {
  readonly id: string;
  readonly name: string;
  /** One bitmask per pad (bit n = step n), 16 pads. What `drums.setPattern` sends. */
  readonly pads: readonly number[];
}

/** The pad slots of the factory kit, in order; pattern files name pads by these. */
export const PAD_KEYS = [
  'kick',
  'snare',
  'closedHat',
  'openHat',
  'clap',
  'lowTom',
  'midTom',
  'highTom',
  'rim',
  'cowbell',
  'shaker',
  'crash',
  'percLow',
  'percHigh',
  'bass',
  'zap',
] as const;

interface Rejections {
  rejected: string[];
}

function listOf(source: unknown): unknown[] | null {
  const list =
    typeof source === 'object' && source !== null && 'presets' in source ? source.presets : null;
  return Array.isArray(list) ? (list as unknown[]) : null;
}

/** Presets with an unknown parameter or an out-of-range value are left out (a test keeps the shipped file clean). */
export function parseSynthPresets(source: unknown): { presets: SynthPreset[] } & Rejections {
  const presets: SynthPreset[] = [];
  const rejected: string[] = [];
  const list = listOf(source);
  if (!list) return { presets, rejected: ['(file)'] };

  for (const entry of list) {
    const p = entry as Record<string, unknown>;
    const id = typeof p['id'] === 'string' ? p['id'] : '(no id)';
    const parameters = p['parameters'];
    if (
      p['processor'] !== 'synth' ||
      typeof p['name'] !== 'string' ||
      p['version'] !== 1 ||
      typeof parameters !== 'object' ||
      parameters === null
    ) {
      rejected.push(id);
      continue;
    }
    const values: number[] = SYNTH_PRESET_IDS.map((pid) => PARAMETERS[pid].defaultValue);
    let valid = true;
    for (const [key, value] of Object.entries(parameters)) {
      const index = SYNTH_PRESET_IDS.findIndex((pid) => pid === `synth.${key}`);
      const pid: ParamId | undefined = SYNTH_PRESET_IDS[index];
      if (
        pid === undefined ||
        typeof value !== 'number' ||
        !(value >= PARAMETERS[pid].min && value <= PARAMETERS[pid].max)
      )
        valid = false;
      else values[index] = value;
    }
    if (valid) presets.push({ id, name: p['name'], values });
    else rejected.push(id);
  }
  return { presets, rejected };
}

export function parseDrumPresets(source: unknown): { presets: DrumPreset[] } & Rejections {
  const presets: DrumPreset[] = [];
  const rejected: string[] = [];
  const list = listOf(source);
  if (!list) return { presets, rejected: ['(file)'] };

  for (const entry of list) {
    const p = entry as Record<string, unknown>;
    const id = typeof p['id'] === 'string' ? p['id'] : '(no id)';
    const pattern = p['pattern'];
    if (
      p['processor'] !== 'drums' ||
      typeof p['name'] !== 'string' ||
      p['version'] !== 1 ||
      typeof pattern !== 'object' ||
      pattern === null
    ) {
      rejected.push(id);
      continue;
    }
    const pads = PAD_KEYS.map(() => 0);
    let valid = true;
    for (const [key, steps] of Object.entries(pattern)) {
      const pad = PAD_KEYS.findIndex((name) => name === key);
      if (pad < 0 || typeof steps !== 'string' || !/^[x.]{16}$/.test(steps)) {
        valid = false;
        continue;
      }
      let mask = 0;
      for (let step = 0; step < 16; step++) if (steps.charAt(step) === 'x') mask |= 1 << step;
      pads[pad] = mask;
    }
    if (valid) presets.push({ id, name: p['name'], pads });
    else rejected.push(id);
  }
  return { presets, rejected };
}

export const SYNTH_PRESETS: readonly SynthPreset[] = parseSynthPresets(synthData).presets;
export const DRUM_PRESETS: readonly DrumPreset[] = parseDrumPresets(drumData).presets;
