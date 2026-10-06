import { useEffect, useRef, useState } from 'react';

import { useBridge } from '../bridge/BridgeContext';
import { useLatest } from '../state/latestEvent';
import { useStores } from '../state/StoresContext';

const FORMATS = ['16-bit (CD, dithered)', '24-bit', '32-bit float'] as const;
const RATES = [44100, 48000, 96000] as const;

/** Export as WAV (Task 025): format and rate, then the native save dialog; progress and cancel. */
export function ExportButton() {
  const bridge = useBridge();
  const state = useLatest(useStores().exportState);
  const [open, setOpen] = useState(false);
  const [format, setFormat] = useState(1);
  const [rate, setRate] = useState<number>(48000);
  const panel = useRef<HTMLDivElement>(null);
  const running = state?.running ?? false;

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

  if (running)
    return (
      <div className="export-progress" role="status" aria-label="Exporting">
        <progress max={1} value={state?.progress ?? 0} aria-label="Export progress" />
        <button
          type="button"
          className="button button--quiet"
          onClick={() => {
            bridge.send({ type: 'project.cancelExport', payload: {} });
          }}
        >
          Cancel
        </button>
      </div>
    );

  return (
    <div className="menu" ref={panel}>
      <button
        type="button"
        className="button button--quiet"
        aria-expanded={open}
        title="Export the song as a WAV file"
        onClick={() => {
          setOpen(!open);
        }}
      >
        Export…
      </button>
      {open ? (
        <div className="menu__list export-options" role="dialog" aria-label="Export options">
          <label>
            Format
            <select
              value={format}
              onChange={(event) => {
                setFormat(Number(event.currentTarget.value));
              }}
            >
              {FORMATS.map((name, index) => (
                <option key={name} value={index}>
                  {name}
                </option>
              ))}
            </select>
          </label>
          <label>
            Sample rate
            <select
              value={rate}
              onChange={(event) => {
                setRate(Number(event.currentTarget.value));
              }}
            >
              {RATES.map((r) => (
                <option key={r} value={r}>
                  {(r / 1000).toFixed(1)} kHz
                </option>
              ))}
            </select>
          </label>
          <button
            type="button"
            className="button"
            onClick={() => {
              setOpen(false);
              bridge.send({ type: 'project.export', payload: { format, sampleRate: rate } });
            }}
          >
            Export WAV…
          </button>
        </div>
      ) : null}
    </div>
  );
}
