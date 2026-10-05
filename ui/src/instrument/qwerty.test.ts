import { describe, expect, it } from 'vitest';

import { isBlackKey, noteForKey, noteName } from './qwerty';

describe('computer keyboard mapping', () => {
  it('maps the home row to white keys from the base note', () => {
    expect(noteForKey('KeyA', 60)).toBe(60);
    expect(noteForKey('KeyS', 60)).toBe(62);
    expect(noteForKey('KeyK', 60)).toBe(72);
  });

  it('maps the row above to black keys', () => {
    expect(noteForKey('KeyW', 60)).toBe(61);
    expect(isBlackKey(61)).toBe(true);
  });

  it('ignores unmapped keys and out-of-range notes', () => {
    expect(noteForKey('KeyQ', 60)).toBeNull();
    expect(noteForKey('Semicolon', 120)).toBeNull();
  });

  it('names notes with octave numbers (C4 = 60)', () => {
    expect(noteName(60)).toBe('C4');
    expect(noteName(69)).toBe('A4');
    expect(noteName(61)).toBe('C♯4');
  });
});
