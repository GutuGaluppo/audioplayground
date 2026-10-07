import type { KeyboardEvent, PointerEvent } from 'react';

import { LAYOUT_LIMITS, setLayoutPref, useLayoutPrefs } from '../state/layoutPrefs';
import type { LayoutPrefs } from '../state/layoutPrefs';

const KEY_STEP = 16;

/**
 * Drag handle between two zones. It resizes one preference: `grow` says which way the drag makes
 * the zone bigger (+1: right/down, -1: left/up). Arrow keys resize too; Home/End jump to the limits.
 */
export function Splitter({
  pref,
  orientation,
  grow,
  label,
}: {
  pref: keyof LayoutPrefs;
  /** 'horizontal' is a bar you drag up and down; 'vertical' one you drag left and right. */
  orientation: 'horizontal' | 'vertical';
  grow: 1 | -1;
  label: string;
}) {
  const value = useLayoutPrefs()[pref];
  const [min, max] = LAYOUT_LIMITS[pref];
  const horizontal = orientation === 'horizontal';

  const onPointerDown = (event: PointerEvent<HTMLDivElement>) => {
    event.preventDefault();
    const start = horizontal ? event.clientY : event.clientX;
    const startValue = value;
    const move = (e: globalThis.PointerEvent) => {
      const delta = (horizontal ? e.clientY : e.clientX) - start;
      setLayoutPref(pref, startValue + delta * grow);
    };
    const end = () => {
      window.removeEventListener('pointermove', move);
      window.removeEventListener('pointerup', end);
      window.removeEventListener('pointercancel', end);
    };
    window.addEventListener('pointermove', move);
    window.addEventListener('pointerup', end);
    window.addEventListener('pointercancel', end);
  };

  const onKeyDown = (event: KeyboardEvent<HTMLDivElement>) => {
    const towards = horizontal
      ? { ArrowDown: 1, ArrowUp: -1 }[event.key]
      : { ArrowRight: 1, ArrowLeft: -1 }[event.key];
    if (towards !== undefined) {
      event.preventDefault();
      setLayoutPref(pref, value + towards * grow * (event.shiftKey ? 4 : 1) * KEY_STEP);
    } else if (event.key === 'Home' || event.key === 'End') {
      event.preventDefault();
      setLayoutPref(pref, event.key === 'Home' ? min : max);
    }
  };

  return (
    <div
      role="separator"
      tabIndex={0}
      className="splitter"
      data-orientation={orientation}
      aria-orientation={orientation}
      aria-label={label}
      aria-valuemin={min}
      aria-valuemax={max}
      aria-valuenow={value}
      title="Drag to resize (arrow keys too, Shift for bigger steps)"
      onPointerDown={onPointerDown}
      onKeyDown={onKeyDown}
    />
  );
}
