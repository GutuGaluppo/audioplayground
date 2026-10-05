import { useEffect } from 'react';

import { useBridge } from './bridge/BridgeContext';
import type { EngineStatus } from './bridge/generated';
import { Notices } from './components/Notices';
import { ParamSlider } from './components/ParamSlider';
import { PeakMeter } from './components/PeakMeter';
import { TransportBar } from './components/TransportBar';
import { useLatest } from './state/latestEvent';
import { useStores } from './state/StoresContext';

function formatDevice(status: EngineStatus['payload']): string {
  if (!status.deviceName) return 'No audio output';
  return [
    status.deviceName,
    `${(status.sampleRate / 1000).toFixed(1)} kHz`,
    `${String(status.bufferSize)} samples`,
    `${status.outputLatencyMs.toFixed(1)} ms`,
  ].join(' · ');
}

export function App() {
  const bridge = useBridge();
  const status = useLatest(useStores().engineStatus);

  useEffect(() => {
    bridge.send({ type: 'app.ready', payload: {} });
  }, [bridge]);

  const toneEnabled = status?.toneEnabled ?? false;
  const openSettings = () => {
    bridge.send({ type: 'audio.openSettings', payload: {} });
  };

  return (
    <div className="app">
      <h1 className="visually-hidden">Audio Playground</h1>
      {status?.error ? (
        <div className="banner" role="alert">
          <span>{status.error}</span>
          <button type="button" className="button button--quiet" onClick={openSettings}>
            Choose device
          </button>
        </div>
      ) : null}

      <TransportBar />

      <main className="stage">
        <section className="card" aria-label="Test tone">
          <h2 className="card__title">Test tone</h2>
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

          <ParamSlider id="tone.level" label="Level" />

          <PeakMeter />
        </section>
      </main>

      <Notices />

      <footer className="statusbar">
        <span>{status ? formatDevice(status) : 'Connecting to audio engine…'}</span>
        {bridge.isNative ? (
          <button type="button" className="button button--quiet" onClick={openSettings}>
            Audio settings
          </button>
        ) : (
          <span className="statusbar__badge">Simulated engine</span>
        )}
      </footer>
    </div>
  );
}
