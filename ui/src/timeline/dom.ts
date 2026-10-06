export const isMac =
  typeof navigator !== 'undefined' && /Mac|iPhone|iPad/.test(navigator.userAgent);

/** True while the user types into a field, where keys must keep their usual meaning. */
export function isTextEntry(target: EventTarget | null): boolean {
  return (
    target instanceof HTMLElement &&
    (target.isContentEditable ||
      target.tagName === 'TEXTAREA' ||
      (target instanceof HTMLInputElement && target.type !== 'range'))
  );
}

/**
 * Follows a pointer drag with window listeners, so the drag survives the dragged element being
 * re-rendered elsewhere (e.g. a clip moving to another track). onMove gets the offset from the
 * starting point; onEnd runs once.
 */
export function trackDrag(
  start: { clientX: number; clientY: number },
  onMove: (dx: number, dy: number, event: PointerEvent) => void,
  onEnd?: (moved: boolean) => void,
): void {
  let moved = false;
  const move = (event: PointerEvent) => {
    const dx = event.clientX - start.clientX;
    const dy = event.clientY - start.clientY;
    if (!moved && Math.abs(dx) < 3 && Math.abs(dy) < 3) return;
    moved = true;
    onMove(dx, dy, event);
  };
  const end = () => {
    window.removeEventListener('pointermove', move);
    window.removeEventListener('pointerup', end);
    window.removeEventListener('pointercancel', end);
    onEnd?.(moved);
  };
  window.addEventListener('pointermove', move);
  window.addEventListener('pointerup', end);
  window.addEventListener('pointercancel', end);
}
