/**
 * Computer keyboard as a piano, by physical key position (KeyboardEvent.code), so it works the
 * same on QWERTY, AZERTY, ABNT2... Bottom row of letters = white keys, row above = black keys,
 * the layout most music apps use.
 */
const KEY_TO_SEMITONE: Readonly<Record<string, number>> = {
  KeyA: 0,
  KeyW: 1,
  KeyS: 2,
  KeyE: 3,
  KeyD: 4,
  KeyF: 5,
  KeyT: 6,
  KeyG: 7,
  KeyY: 8,
  KeyH: 9,
  KeyU: 10,
  KeyJ: 11,
  KeyK: 12,
  KeyO: 13,
  KeyL: 14,
  KeyP: 15,
  Semicolon: 16,
};

export const OCTAVE_DOWN_CODE = 'KeyZ';
export const OCTAVE_UP_CODE = 'KeyX';
export const VELOCITY_DOWN_CODE = 'KeyC';
export const VELOCITY_UP_CODE = 'KeyV';

export const MIN_BASE_NOTE = 12; // C0
export const MAX_BASE_NOTE = 96; // C7

export function noteForKey(code: string, baseNote: number): number | null {
  const semitone = KEY_TO_SEMITONE[code];
  if (semitone === undefined) return null;
  const note = baseNote + semitone;
  return note >= 0 && note <= 127 ? note : null;
}

export function semitoneLabelForCode(semitone: number): string | undefined {
  const entry = Object.entries(KEY_TO_SEMITONE).find(([, value]) => value === semitone);
  if (!entry) return undefined;
  const code = entry[0];
  return code === 'Semicolon' ? ';' : code.replace('Key', '');
}

const NAMES = ['C', 'C♯', 'D', 'D♯', 'E', 'F', 'F♯', 'G', 'G♯', 'A', 'A♯', 'B'] as const;

export function noteName(note: number): string {
  return `${NAMES[((note % 12) + 12) % 12] ?? ''}${String(Math.floor(note / 12) - 1)}`;
}

export function isBlackKey(note: number): boolean {
  return [1, 3, 6, 8, 10].includes(((note % 12) + 12) % 12);
}
