import { useEffect, useRef, useSyncExternalStore } from 'react';

import { useBridge } from '../bridge/BridgeContext';

const never = () => () => undefined;

/**
 * Browsers play nothing until the person has clicked on the page. In the browser build this asks
 * for that click, up front and plainly; it is invisible in the desktop app, where audio just works.
 */
export function AudioGate() {
  const gate = useBridge().audio;
  const state = useSyncExternalStore(
    (listener) => (gate ? gate.subscribe(listener) : never()),
    () => gate?.state() ?? 'running',
  );
  const button = useRef<HTMLButtonElement>(null);

  useEffect(() => {
    if (state === 'blocked') button.current?.focus();
  }, [state]);

  if (!gate || state === 'running') return null;

  return (
    <div className="audio-gate" role="dialog" aria-modal="true" aria-label="Start audio">
      <div className="audio-gate__card">
        <h2>Audio Playground</h2>
        {state === 'failed' ? (
          <p role="alert">{gate.error() || 'The audio engine could not start.'}</p>
        ) : (
          <p role="status">
            {state === 'loading'
              ? 'Loading the audio engine…'
              : 'Your browser needs a click before it plays sound.'}
          </p>
        )}
        <button
          ref={button}
          type="button"
          className="button"
          disabled={state !== 'blocked'}
          onClick={() => {
            void gate.start();
          }}
        >
          Start audio
        </button>
      </div>
    </div>
  );
}
