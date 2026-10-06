import data from '../../../presets/effects.json';
import { EFFECTS } from '../params/generated';
import type { EffectId } from '../params/generated';

/** A built-in effect preset (guide §17), resolved to a full value list in parameter order. */
export interface EffectPreset {
  readonly id: string;
  readonly effect: number; // index into EFFECTS
  readonly name: string;
  readonly values: readonly number[];
}

/**
 * Turns the preset file into value lists. Presets that name an unknown effect or parameter, or a
 * value out of range, are left out (a test keeps the shipped file free of them).
 */
export function parsePresets(source: unknown): { presets: EffectPreset[]; rejected: string[] } {
  const presets: EffectPreset[] = [];
  const rejected: string[] = [];
  const list =
    typeof source === 'object' && source !== null && 'presets' in source ? source.presets : null;
  if (!Array.isArray(list)) return { presets, rejected: ['(file)'] };

  for (const entry of list as unknown[]) {
    const p = entry as {
      id?: unknown;
      processor?: unknown;
      name?: unknown;
      version?: unknown;
      parameters?: unknown;
    };
    const id = typeof p.id === 'string' ? p.id : '(no id)';
    const effect = EFFECTS.findIndex((e) => e.id === (p.processor as EffectId));
    const descriptor = EFFECTS[effect];
    const parameters = p.parameters;
    if (
      !descriptor ||
      typeof p.name !== 'string' ||
      p.version !== 1 ||
      typeof parameters !== 'object' ||
      parameters === null
    ) {
      rejected.push(id);
      continue;
    }
    const values = descriptor.parameters.map((d) => d.defaultValue);
    let valid = true;
    for (const [key, value] of Object.entries(parameters)) {
      const index = descriptor.parameters.findIndex((d) => d.id === `${descriptor.id}.${key}`);
      const d = descriptor.parameters[index];
      if (!d || typeof value !== 'number' || !(value >= d.min && value <= d.max)) valid = false;
      else values[index] = value;
    }
    if (valid) presets.push({ id, effect, name: p.name, values });
    else rejected.push(id);
  }
  return { presets, rejected };
}

export const PRESETS: readonly EffectPreset[] = parsePresets(data).presets;
