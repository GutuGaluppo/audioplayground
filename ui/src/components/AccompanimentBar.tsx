import { useEffect, useState } from 'react';

import { useBridge } from '../bridge/BridgeContext';
import { useLatest } from '../state/latestEvent';
import { useStores } from '../state/StoresContext';

/** AccompanimentState values from the bridge schema. */
export const AccompanimentStates = {
  idle: 0,
  analyzing: 1,
  ready: 2,
  previewing: 3,
  accepted: 4,
  dismissed: 5,
  unavailable: 6,
} as const;

const CONFIRMATION_MS = 6000;

/**
 * Smart Accompaniment (ADR-011): a quiet strip under the transport. After a take it offers "Try a
 * beat"; the musician previews it with the take, adds it or moves on. Nothing opens over the
 * music, and nothing is added without a click.
 */
export function AccompanimentBar() {
  const bridge = useBridge();
  const state = useLatest(useStores().accompaniment);
  // The update whose confirmation has been shown long enough (a new update shows again).
  const [confirmed, setConfirmed] = useState<unknown>(null);
  const code = state?.state ?? AccompanimentStates.idle;
  const send = (type: 'preview' | 'stopPreview' | 'add' | 'next' | 'dismiss') => {
    bridge.send({ type: `accompaniment.${type}`, payload: {} });
  };

  // "Added" confirms for a few seconds, then gets out of the way.
  useEffect(() => {
    if (code !== AccompanimentStates.accepted) return;
    const timer = window.setTimeout(() => {
      setConfirmed(state);
    }, CONFIRMATION_MS);
    return () => {
      window.clearTimeout(timer);
    };
  }, [code, state]);

  if (
    !state ||
    code === AccompanimentStates.idle ||
    code === AccompanimentStates.dismissed ||
    (code === AccompanimentStates.accepted && confirmed === state)
  )
    return null;

  const previewing = code === AccompanimentStates.previewing;
  return (
    <section className="accompaniment" aria-label="Beat suggestion" data-state={code}>
      <p className="accompaniment__text" role="status">
        {code === AccompanimentStates.analyzing ? (
          'Listening to your take…'
        ) : code === AccompanimentStates.ready || previewing ? (
          <>
            <strong>{previewing ? 'Playing' : state.message}</strong>
            <span className="accompaniment__name"> {state.grooveName}</span>
            {state.detail ? <span className="accompaniment__detail"> · {state.detail}</span> : null}
          </>
        ) : (
          state.message
        )}
      </p>
      <div className="accompaniment__actions">
        {code === AccompanimentStates.ready ? (
          <button
            type="button"
            className="button"
            onClick={() => {
              send('preview');
            }}
          >
            Preview
          </button>
        ) : null}
        {previewing ? (
          <>
            <button
              type="button"
              className="button"
              onClick={() => {
                send('add');
              }}
            >
              Add
            </button>
            <button
              type="button"
              className="button button--quiet"
              onClick={() => {
                send('stopPreview');
              }}
            >
              Stop
            </button>
          </>
        ) : null}
        {(code === AccompanimentStates.ready || previewing) && state.canTryAnother ? (
          <button
            type="button"
            className="button button--quiet"
            onClick={() => {
              send('next');
            }}
          >
            Try another
          </button>
        ) : null}
        {code !== AccompanimentStates.analyzing ? (
          <button
            type="button"
            className="button button--quiet"
            aria-label="Dismiss suggestion"
            onClick={() => {
              send('dismiss');
            }}
          >
            {code === AccompanimentStates.accepted ? 'OK' : 'Dismiss'}
          </button>
        ) : null}
      </div>
    </section>
  );
}
