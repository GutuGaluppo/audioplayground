/**
 * Computer keys for the 4x4 pads, laid out like the pads themselves (MPC style: pad 1 at the
 * bottom left). By physical position, so it works on any keyboard layout.
 */
const PAD_KEYS: readonly (readonly string[])[] = [
  ['KeyZ', 'KeyX', 'KeyC', 'KeyV'], // pads 0-3 (bottom row)
  ['KeyA', 'KeyS', 'KeyD', 'KeyF'], // pads 4-7
  ['KeyQ', 'KeyW', 'KeyE', 'KeyR'], // pads 8-11
  ['Digit1', 'Digit2', 'Digit3', 'Digit4'], // pads 12-15 (top row)
];

export function padForKey(code: string): number | null {
  for (let row = 0; row < PAD_KEYS.length; row++) {
    const column = PAD_KEYS[row]?.indexOf(code) ?? -1;
    if (column >= 0) return row * 4 + column;
  }
  return null;
}

export function keyLabelForPad(pad: number): string {
  const code = PAD_KEYS[Math.floor(pad / 4)]?.[pad % 4] ?? '';
  return code.replace('Key', '').replace('Digit', '');
}
