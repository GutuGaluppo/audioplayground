import { useEffect } from 'react';

import { useBridge } from './bridge/BridgeContext';
import type { EngineStatus } from './bridge/generated';
import { Notices } from './components/Notices';
import { QuickStart } from './components/QuickStart';
import { ParamSlider } from './components/ParamSlider';
import { LimiterLight, PeakMeter } from './components/PeakMeter';
import { TransportBar } from './components/TransportBar';
import { PianoRoll } from './editor/PianoRoll';
import { EffectsPanel } from './effects/EffectsPanel';
import { Keyboard } from './instrument/Keyboard';
import { InstrumentArea } from './instrument/InstrumentArea';
import { useLatest } from './state/latestEvent';
import { useStores } from './state/StoresContext';
import { findClip, isAudioClip, TrackKind } from './timeline/model';
import { useSelection } from './timeline/selection';
import { Timeline } from './timeline/Timeline';

function formatDevice(status: EngineStatus['payload']): string {
  if (!status.deviceName) return 'No audio output';
  return [
    status.deviceName,
    `${(status.sampleRate / 1000).toFixed(1)} kHz`,
    `${String(status.bufferSize)} samples`,
    `${status.outputLatencyMs.toFixed(1)} ms`,
  ].join(' · ');
}

/** The note editor for the selected synth or sampler clip (drum clips use the step grid). */
function ClipEditor() {
  const stores = useStores();
  const timeline = useLatest(stores.timeline);
  const selection = useSelection(stores.selection);
  const found = selection.clip !== null ? findClip(timeline?.tracks ?? [], selection.clip) : null;
  if (!found || isAudioClip(found.clip) || found.track.kind === TrackKind.drums) return null;
  return <PianoRoll track={found.track} clip={found.clip} />;
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

      <main className="workspace">
        <Timeline />
        <div className="workspace__bottom">
          <QuickStart />
          <InstrumentArea />
          <ClipEditor />
          <EffectsPanel />
        </div>
      </main>

      <Keyboard />

      <Notices />

      <footer className="statusbar">
        <span>{status ? formatDevice(status) : 'Connecting to audio engine…'}</span>
        <div className="statusbar__tone" role="group" aria-label="Test tone">
          <button
            type="button"
            className="toggle"
            aria-pressed={toneEnabled}
            disabled={!status}
            onClick={() => {
              bridge.send({ type: 'tone.setEnabled', payload: { enabled: !toneEnabled } });
            }}
          >
            {toneEnabled ? 'Stop test tone' : 'Play test tone'}
          </button>
          <ParamSlider id="tone.level" label="Level" />
          <PeakMeter />
          <LimiterLight />
        </div>
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
