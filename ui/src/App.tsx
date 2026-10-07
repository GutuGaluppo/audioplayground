import { useEffect } from 'react';

import { useBridge } from './bridge/BridgeContext';
import type { EngineStatus } from './bridge/generated';
import { AudioGate } from './components/AudioGate';
import { ProjectDialogs } from './components/ProjectDialogs';
import { AccompanimentBar } from './components/AccompanimentBar';
import { AudioDevicesMenu } from './components/AudioDevicesMenu';
import { Notices } from './components/Notices';
import { QuickStart } from './components/QuickStart';
import { ParamSlider } from './components/ParamSlider';
import { LimiterLight, PeakMeter } from './components/PeakMeter';
import { TransportBar } from './components/TransportBar';
import { PianoRoll } from './editor/PianoRoll';
import { EffectsPanel } from './effects/EffectsPanel';
import { Keyboard } from './instrument/Keyboard';
import { InstrumentArea } from './instrument/InstrumentArea';
import { Splitter } from './components/Splitter';
import { resetLayoutPrefs, useLayoutPrefs } from './state/layoutPrefs';
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

  const layout = useLayoutPrefs();
  const toneEnabled = status?.toneEnabled ?? false;
  const openSettings = () => {
    bridge.send({ type: 'audio.openSettings', payload: {} });
  };

  return (
    <div
      className="app"
      style={
        {
          '--bottom-height': `${String(layout.bottomHeight)}px`,
          '--instrument-width': `${String(layout.instrumentWidth)}px`,
          '--effects-width': `${String(layout.effectsWidth)}px`,
          '--keyboard-height': `${String(layout.keyboardHeight)}px`,
        } as React.CSSProperties
      }
    >
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
      <AccompanimentBar />

      <main className="workspace">
        <Timeline />
        <Splitter
          pref="bottomHeight"
          orientation="horizontal"
          grow={-1}
          label="Editor panel height"
        />
        <div className="workspace__bottom">
          <QuickStart />
          <InstrumentArea />
          <Splitter
            pref="instrumentWidth"
            orientation="vertical"
            grow={1}
            label="Instrument panel width"
          />
          <ClipEditor />
          <EffectsPanel />
        </div>
      </main>

      <Keyboard />

      <Notices />
      <AudioGate />
      <ProjectDialogs />

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
        <button
          type="button"
          className="button button--quiet"
          title="Restore the default size of every panel"
          onClick={resetLayoutPrefs}
        >
          Reset layout
        </button>
        {bridge.kind === 'native' ? (
          <AudioDevicesMenu />
        ) : (
          <span className="statusbar__badge">
            {bridge.kind === 'wasm' ? 'Browser engine' : 'Simulated engine'}
          </span>
        )}
      </footer>
    </div>
  );
}
