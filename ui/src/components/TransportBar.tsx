import { useEffect, useState } from 'react';

import { useBridge } from '../bridge/BridgeContext';
import { useLatest } from '../state/latestEvent';
import { useStores } from '../state/StoresContext';

const MIN_BPM = 20;
const MAX_BPM = 300;

export function TransportBar() {
  const bridge = useBridge();
  const stores = useStores();
  const state = useLatest(stores.transportState);
  const position = useLatest(stores.transportPosition);

  const playing = state?.playing ?? false;
  const [tempoDraft, setTempoDraft] = useState<string | null>(null);

  useEffect(() => {
    const onKeyDown = (event: KeyboardEvent) => {
      if (event.code !== 'Space' || event.repeat || isInteractive(event.target)) return;
      event.preventDefault();
      bridge.send({ type: playing ? 'transport.stop' : 'transport.play', payload: {} });
    };
    window.addEventListener('keydown', onKeyDown);
    return () => {
      window.removeEventListener('keydown', onKeyDown);
    };
  }, [bridge, playing]);

  const commitTempo = () => {
    if (tempoDraft === null) return;
    const bpm = Number(tempoDraft);
    if (Number.isFinite(bpm)) {
      bridge.send({
        type: 'transport.setTempo',
        payload: { bpm: Math.min(MAX_BPM, Math.max(MIN_BPM, Math.round(bpm * 10) / 10)) },
      });
    }
    setTempoDraft(null);
  };

  return (
    <header className="transport" aria-label="Transport">
      <div className="transport__group">
        <button
          type="button"
          className="transport__button"
          aria-label="Return to start"
          title="Return to start"
          disabled={!state}
          onClick={() => {
            bridge.send({ type: 'transport.returnToStart', payload: {} });
          }}
        >
          <span aria-hidden="true">⏮</span>
        </button>
        <button
          type="button"
          className="transport__button transport__button--primary"
          aria-pressed={playing}
          aria-label={playing ? 'Stop' : 'Play'}
          title={playing ? 'Stop (Space)' : 'Play (Space)'}
          disabled={!state}
          onClick={() => {
            bridge.send({ type: playing ? 'transport.stop' : 'transport.play', payload: {} });
          }}
        >
          <span aria-hidden="true">{playing ? '■' : '▶'}</span>
        </button>
      </div>

      <output
        className="transport__position"
        aria-label="Position"
        data-counting-in={position?.countingIn ?? false}
      >
        {position ? `${String(position.bar)}.${String(position.beat)}` : '–'}
      </output>

      <div className="transport__group">
        <label className="transport__tempo">
          <input
            type="number"
            inputMode="decimal"
            min={MIN_BPM}
            max={MAX_BPM}
            step={1}
            aria-label="Tempo in beats per minute"
            disabled={!state}
            value={tempoDraft ?? (state ? String(state.bpm) : '')}
            onChange={(event) => {
              setTempoDraft(event.currentTarget.value);
            }}
            onBlur={commitTempo}
            onKeyDown={(event) => {
              if (event.key === 'Enter') event.currentTarget.blur();
              if (event.key === 'Escape') {
                setTempoDraft(null);
                event.currentTarget.blur();
              }
            }}
          />
          <span>BPM</span>
        </label>

        <span className="transport__meter">
          {state ? `${String(state.numerator)}/${String(state.denominator)}` : ''}
        </span>

        <button
          type="button"
          className="toggle"
          aria-pressed={state?.metronomeEnabled ?? false}
          disabled={!state}
          onClick={() => {
            bridge.send({
              type: 'metronome.setEnabled',
              payload: { enabled: !(state?.metronomeEnabled ?? false) },
            });
          }}
        >
          Click
        </button>

        <button
          type="button"
          className="toggle"
          aria-pressed={(state?.countInBars ?? 0) > 0}
          title="Count in one bar before playing"
          disabled={!state}
          onClick={() => {
            bridge.send({
              type: 'transport.setCountIn',
              payload: { bars: (state?.countInBars ?? 0) > 0 ? 0 : 1 },
            });
          }}
        >
          Count-in
        </button>
      </div>
    </header>
  );
}

function isInteractive(target: EventTarget | null): boolean {
  return (
    target instanceof HTMLElement &&
    (target.isContentEditable || ['INPUT', 'TEXTAREA', 'SELECT', 'BUTTON'].includes(target.tagName))
  );
}
