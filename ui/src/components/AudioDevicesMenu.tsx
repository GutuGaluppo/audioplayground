import { useEffect, useRef, useState } from 'react';

import { useBridge } from '../bridge/BridgeContext';
import { useLatest } from '../state/latestEvent';
import { useStores } from '../state/StoresContext';

/** "48 kHz", "44.1 kHz". */
export function formatRate(rate: number): string {
  return `${String(Math.round(rate / 100) / 10)} kHz`;
}

/** What a buffer size costs in time at a sample rate: "2.7 ms". */
export function bufferMs(size: number, rate: number): string {
  return rate > 0 ? `${((1000 * size) / rate).toFixed(1)} ms` : '';
}

/**
 * Audio output, input, sample rate and buffer size, chosen in the app. The input only says which
 * microphone a recording will use: it stays closed until a track is armed (guide §25).
 */
export function AudioDevicesMenu() {
  const bridge = useBridge();
  const stores = useStores();
  const devices = useLatest(stores.audioDevices);
  const status = useLatest(stores.engineStatus);
  const [open, setOpen] = useState(false);
  const panel = useRef<HTMLDivElement>(null);

  useEffect(() => {
    if (!open) return;
    const close = (event: PointerEvent) => {
      if (panel.current && !panel.current.contains(event.target as Node)) setOpen(false);
    };
    window.addEventListener('pointerdown', close);
    return () => {
      window.removeEventListener('pointerdown', close);
    };
  }, [open]);

  const rate = devices?.sampleRate ?? 0;
  return (
    <div className="menu menu--up" ref={panel}>
      <button
        type="button"
        className="button button--quiet"
        aria-expanded={open}
        onClick={() => {
          setOpen(!open);
        }}
      >
        Audio settings
      </button>
      {open ? (
        <div className="menu__list audio-devices" role="dialog" aria-label="Audio settings">
          <label>
            Output
            <select
              value={devices?.output ?? ''}
              disabled={!devices || devices.outputs.length === 0}
              onChange={(event) => {
                bridge.send({
                  type: 'audio.setOutput',
                  payload: { name: event.currentTarget.value },
                });
              }}
            >
              {devices?.output === '' || !devices ? <option value="">No output</option> : null}
              {devices?.outputs.map((d) => (
                <option key={d.name} value={d.name}>
                  {d.name}
                </option>
              ))}
            </select>
          </label>
          <label>
            Input (for recording)
            <select
              value={devices?.input ?? ''}
              disabled={!devices || devices.inputs.length === 0}
              onChange={(event) => {
                bridge.send({
                  type: 'audio.setInput',
                  payload: { name: event.currentTarget.value },
                });
              }}
            >
              <option value="">Default input</option>
              {devices?.inputs.map((d) => (
                <option key={d.name} value={d.name}>
                  {d.name}
                </option>
              ))}
            </select>
          </label>
          <label>
            Sample rate
            <select
              value={rate}
              disabled={!devices || devices.sampleRates.length === 0}
              onChange={(event) => {
                bridge.send({
                  type: 'audio.setSampleRate',
                  payload: { rate: Number(event.currentTarget.value) },
                });
              }}
            >
              {devices?.sampleRates.map((r) => (
                <option key={r} value={r}>
                  {formatRate(r)}
                </option>
              ))}
            </select>
          </label>
          <label>
            Buffer size
            <select
              value={devices?.bufferSize ?? 0}
              disabled={!devices || devices.bufferSizes.length === 0}
              onChange={(event) => {
                bridge.send({
                  type: 'audio.setBufferSize',
                  payload: { size: Number(event.currentTarget.value) },
                });
              }}
            >
              {devices?.bufferSizes.map((size) => (
                <option key={size} value={size}>
                  {String(size)} samples ({bufferMs(size, rate)})
                </option>
              ))}
            </select>
          </label>
          <p className="audio-devices__note" role="status">
            {status?.deviceName
              ? `Output latency ${status.outputLatencyMs.toFixed(1)} ms. A smaller buffer lowers it, but may crackle on a slow computer.`
              : 'No audio output is open. Choose one above.'}
          </p>
          <button
            type="button"
            className="button button--quiet"
            onClick={() => {
              bridge.send({ type: 'audio.openSettings', payload: {} });
            }}
          >
            Advanced…
          </button>
        </div>
      ) : null}
    </div>
  );
}
