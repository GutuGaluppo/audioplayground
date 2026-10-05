let lastGesture = 0;

/**
 * Identifies one continuous edit (e.g. a slider drag) so the native undo history records it as a
 * single step. 0 means "not part of a gesture".
 */
export function beginGesture(): number {
  lastGesture = lastGesture >= 2_147_483_646 ? 1 : lastGesture + 1;
  return lastGesture;
}
