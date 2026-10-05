import { useEffect, useId } from 'react';

import { useBridge } from './bridge/BridgeContext';
import { PeakMeter } from './components/PeakMeter';
import type { EngineStatusStore } from './state/engineStatus';
import { useEngineStatus } from './state/engineStatus';

const MIN_LEVEL_DB = -60;
const MAX_LEVEL_DB = -6;

function isTextEntry(target: EventTarget | null): boolean {
  return (
    target instanceof HTMLElement &&
    (target.isContentEditable || ['INPUT', 'TEXTAREA', 'SELECT', 'BUTTON'].includes(target.tagName))
  );
}

function formatDevice(status: NonNullable<ReturnType<typeof useEngineStatus>>): string {
  if (!status.deviceName) return 'No audio output';
  return [
    status.deviceName,
    `${(status.sampleRate / 1000).toFixed(1)} kHz`,
    `${String(status.bufferSize)} samples`,
    `${status.outputLatencyMs.toFixed(1)} ms`,
  ].join(' · ');
}

export function App({ statusStore }: { statusStore: EngineStatusStore }) {
  const bridge = useBridge();
  const status = useEngineStatus(statusStore);
  const levelId = useId();

  useEffect(() => {
    bridge.send({ type: 'app.ready', payload: {} });
  }, [bridge]);

  const toneEnabled = status?.toneEnabled ?? false;

  useEffect(() => {
    const onKeyDown = (event: KeyboardEvent) => {
      if (event.code !== 'Space' || event.repeat || isTextEntry(event.target)) return;
      event.preventDefault();
      bridge.send({ type: 'tone.setEnabled', payload: { enabled: !toneEnabled } });
    };
    window.addEventListener('keydown', onKeyDown);
    return () => {
      window.removeEventListener('keydown', onKeyDown);
    };
  }, [bridge, toneEnabled]);

  return (
    <div className="app">
      {status?.error ? (
        <div className="banner" role="alert">
          <span>{status.error}</span>
          <button
            type="button"
            className="button button--quiet"
            onClick={() => {
              bridge.send({ type: 'audio.openSettings', payload: {} });
            }}
          >
            Choose device
          </button>
        </div>
      ) : null}

      <main className="stage">
        <h1 className="stage__title">Audio Playground</h1>

        <section className="card" aria-label="Test tone">
          <button
            type="button"
            className="play"
            aria-pressed={toneEnabled}
            disabled={!status}
            onClick={() => {
              bridge.send({ type: 'tone.setEnabled', payload: { enabled: !toneEnabled } });
            }}
          >
            <span className="play__icon" aria-hidden="true">
              {toneEnabled ? '■' : '▶'}
            </span>
            <span>{toneEnabled ? 'Stop test tone' : 'Play test tone'}</span>
          </button>

          <div className="field">
            <label htmlFor={levelId}>Level</label>
            <input
              id={levelId}
              type="range"
              min={MIN_LEVEL_DB}
              max={MAX_LEVEL_DB}
              step={1}
              value={status?.toneLevelDb ?? -18}
              disabled={!status}
              aria-valuetext={`${String(status?.toneLevelDb ?? -18)} dB`}
              onChange={(event) => {
                bridge.send({
                  type: 'tone.setLevel',
                  payload: { db: Number(event.currentTarget.value) },
                });
              }}
            />
            <output htmlFor={levelId}>{status?.toneLevelDb ?? -18} dB</output>
          </div>

          <PeakMeter />
          <p className="hint">
            Press <kbd>Space</kbd> to toggle
          </p>
        </section>
      </main>

      <footer className="statusbar">
        <span>{status ? formatDevice(status) : 'Connecting to audio engine…'}</span>
        {bridge.isNative ? (
          <button
            type="button"
            className="button button--quiet"
            onClick={() => {
              bridge.send({ type: 'audio.openSettings', payload: {} });
            }}
          >
            Audio settings
          </button>
        ) : (
          <span className="statusbar__badge">Simulated engine</span>
        )}
      </footer>
    </div>
  );
}
