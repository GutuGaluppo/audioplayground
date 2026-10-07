import type { ParamId } from './generated';

/** The synth's parameters in the order `synth.setPreset` lists them (the host uses the same). */
export const SYNTH_PRESET_IDS = [
  'synth.waveform',
  'synth.pitch',
  'synth.detune',
  'synth.cutoff',
  'synth.resonance',
  'synth.attack',
  'synth.decay',
  'synth.sustain',
  'synth.release',
  'synth.volume',
] as const satisfies readonly ParamId[];
