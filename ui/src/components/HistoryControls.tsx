import { useEffect } from 'react';

import { useBridge } from '../bridge/BridgeContext';
import { useLatest } from '../state/latestEvent';
import { useStores } from '../state/StoresContext';

const isMac = typeof navigator !== 'undefined' && /Mac|iPhone|iPad/.test(navigator.userAgent);

function isTextEntry(target: EventTarget | null): boolean {
  return (
    target instanceof HTMLElement &&
    (target.isContentEditable ||
      target.tagName === 'TEXTAREA' ||
      (target instanceof HTMLInputElement && target.type !== 'range'))
  );
}

/** Undo/redo buttons and the platform shortcuts (Cmd/Ctrl+Z, Shift+Cmd/Ctrl+Z, Ctrl+Y). */
export function HistoryControls() {
  const bridge = useBridge();
  const history = useLatest(useStores().history);

  useEffect(() => {
    const onKeyDown = (event: KeyboardEvent) => {
      const mod = isMac ? event.metaKey : event.ctrlKey;
      if (!mod || event.altKey || isTextEntry(event.target)) return; // text fields keep native undo

      const key = event.key.toLowerCase();
      if (key === 'z') {
        event.preventDefault();
        bridge.send({ type: event.shiftKey ? 'edit.redo' : 'edit.undo', payload: {} });
      } else if (key === 'y' && !isMac) {
        event.preventDefault();
        bridge.send({ type: 'edit.redo', payload: {} });
      }
    };
    window.addEventListener('keydown', onKeyDown);
    return () => {
      window.removeEventListener('keydown', onKeyDown);
    };
  }, [bridge]);

  const shortcut = isMac ? '⌘' : 'Ctrl+';

  return (
    <div className="transport__group" role="group" aria-label="History">
      <button
        type="button"
        className="transport__button"
        aria-label={history?.canUndo ? `Undo ${history.undoLabel}` : 'Undo'}
        title={history?.canUndo ? `Undo ${history.undoLabel} (${shortcut}Z)` : 'Nothing to undo'}
        disabled={!history?.canUndo}
        onClick={() => {
          bridge.send({ type: 'edit.undo', payload: {} });
        }}
      >
        <span aria-hidden="true">↶</span>
      </button>
      <button
        type="button"
        className="transport__button"
        aria-label={history?.canRedo ? `Redo ${history.redoLabel}` : 'Redo'}
        title={history?.canRedo ? `Redo ${history.redoLabel}` : 'Nothing to redo'}
        disabled={!history?.canRedo}
        onClick={() => {
          bridge.send({ type: 'edit.redo', payload: {} });
        }}
      >
        <span aria-hidden="true">↷</span>
      </button>
    </div>
  );
}
