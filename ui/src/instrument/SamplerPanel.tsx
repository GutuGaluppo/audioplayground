import { useEffect, useRef, type KeyboardEvent, type PointerEvent } from 'react';

import { useBridge } from '../bridge/BridgeContext';
import { ParamKnob } from '../components/Knob';
import { beginGesture } from '../params/gestures';
import { useParameter } from '../params/parameterStore';
import { useLatest } from '../state/latestEvent';
import { useStores } from '../state/StoresContext';

function Waveform({
  overview,
  start,
  end,
}: {
  overview: readonly number[];
  start: number;
  end: number;
}) {
  const canvasRef = useRef<HTMLCanvasElement>(null);

  useEffect(() => {
    const canvas = canvasRef.current;
    const context = canvas?.getContext('2d');
    if (!canvas || !context) return;

    const ratio = window.devicePixelRatio || 1;
    const width = canvas.clientWidth;
    const height = canvas.clientHeight;
    canvas.width = Math.round(width * ratio);
    canvas.height = Math.round(height * ratio);
    context.scale(ratio, ratio);

    const styles = getComputedStyle(canvas);
    const accent = styles.getPropertyValue('--ap-accent').trim() || '#7c9cff';
    const muted = styles.getPropertyValue('--ap-text-muted').trim() || '#8a8b90';

    context.clearRect(0, 0, width, height);
    const middle = height / 2;
    const step = width / Math.max(1, overview.length);
    overview.forEach((peak, index) => {
      const x = index * step;
      const inRange = x / width >= start / 100 && x / width <= end / 100;
      context.fillStyle = inRange ? accent : muted;
      context.globalAlpha = inRange ? 1 : 0.35;
      const h = Math.max(1, peak * (height - 4));
      context.fillRect(x, middle - h / 2, Math.max(1, step - 0.5), h);
    });
    context.globalAlpha = 1;
  }, [overview, start, end]);

  return <canvas ref={canvasRef} className="waveform" aria-hidden="true" />;
}

const MIN_GAP = 0.5; // percent kept between the two markers

/** Where a marker may go: start stays left of end and vice versa. */
export function clampMarker(which: 'start' | 'end', value: number, start: number, end: number) {
  const bounded = Math.min(100, Math.max(0, value));
  return which === 'start' ? Math.min(bounded, end - MIN_GAP) : Math.max(bounded, start + MIN_GAP);
}

/** Draggable (and arrow-key) start/end markers over the waveform. */
export function TrimMarkers({
  start,
  end,
  onChange,
}: {
  start: number;
  end: number;
  onChange: (which: 'start' | 'end', value: number, gesture: number) => void;
}) {
  const gesture = useRef(0);

  const move = (which: 'start' | 'end', event: PointerEvent<HTMLElement>) => {
    const track = event.currentTarget.parentElement;
    if (!track || gesture.current === 0) return;
    const box = track.getBoundingClientRect();
    if (box.width <= 0) return;
    const percent = ((event.clientX - box.left) / box.width) * 100;
    onChange(which, Math.round(clampMarker(which, percent, start, end) * 10) / 10, gesture.current);
  };

  const keys = (which: 'start' | 'end', event: KeyboardEvent<HTMLElement>) => {
    const direction = event.key === 'ArrowRight' ? 1 : event.key === 'ArrowLeft' ? -1 : 0;
    if (direction === 0) return;
    event.preventDefault();
    const step = event.shiftKey ? 5 : 1;
    const current = which === 'start' ? start : end;
    onChange(which, clampMarker(which, current + direction * step, start, end), 0);
  };

  return (
    <div className="waveform-trim">
      {(['start', 'end'] as const).map((which) => (
        <div
          key={which}
          role="slider"
          tabIndex={0}
          className="waveform-trim__marker"
          data-marker={which}
          aria-label={which === 'start' ? 'Sample start' : 'Sample end'}
          aria-valuemin={0}
          aria-valuemax={100}
          aria-valuenow={which === 'start' ? start : end}
          aria-valuetext={`${(which === 'start' ? start : end).toFixed(1)} percent`}
          style={{ left: `${String(which === 'start' ? start : end)}%` }}
          onPointerDown={(event) => {
            event.currentTarget.setPointerCapture(event.pointerId);
            gesture.current = beginGesture();
          }}
          onPointerMove={(event) => {
            move(which, event);
          }}
          onPointerUp={() => {
            gesture.current = 0;
          }}
          onPointerCancel={() => {
            gesture.current = 0;
          }}
          onKeyDown={(event) => {
            keys(which, event);
          }}
        />
      ))}
    </div>
  );
}

export function SamplerPanel() {
  const bridge = useBridge();
  const stores = useStores();
  const sampler = useLatest(stores.sampler);
  const start = useParameter(stores.parameters, 'sampler.start') ?? 0;
  const end = useParameter(stores.parameters, 'sampler.end') ?? 100;
  const mode = useParameter(stores.parameters, 'sampler.mode');

  const status = !sampler
    ? ''
    : sampler.loading
      ? 'Loading…'
      : sampler.missing
        ? 'Missing — load it again'
        : sampler.loaded
          ? `${sampler.durationSeconds.toFixed(2)} s`
          : 'No sample loaded';

  return (
    <section className="card synth" aria-label="Sampler">
      <div className="sampler__header">
        <h2 className="card__title">Sampler</h2>
        <button
          type="button"
          className="button button--quiet"
          onClick={() => {
            bridge.send({ type: 'sampler.load', payload: {} });
          }}
        >
          Load sample…
        </button>
      </div>

      <div className="sampler__sample" data-missing={sampler?.missing ?? false}>
        <span className="sampler__name">
          {sampler?.name || 'Drop in a sound to play it on the keyboard'}
        </span>
        <span className="sampler__status" role="status">
          {status}
        </span>
      </div>

      {sampler?.loaded ? (
        <div className="waveform-box">
          <Waveform overview={sampler.overview} start={start} end={end} />
          <TrimMarkers
            start={start}
            end={end}
            onChange={(which, value, gesture) => {
              bridge.send({
                type: 'param.set',
                payload: { id: `sampler.${which}`, value, gesture },
              });
            }}
          />
        </div>
      ) : null}

      <div className="segmented" role="radiogroup" aria-label="Playback mode">
        {(['One-shot', 'Gate'] as const).map((name, index) => (
          <button
            key={name}
            type="button"
            role="radio"
            aria-checked={mode === index}
            disabled={mode === undefined}
            onClick={() => {
              bridge.send({
                type: 'param.set',
                payload: { id: 'sampler.mode', value: index, gesture: 0 },
              });
            }}
          >
            {name}
          </button>
        ))}
      </div>

      <div className="knob-grid">
        <ParamKnob id="sampler.start" />
        <ParamKnob id="sampler.end" />
        <ParamKnob id="sampler.pitch" />
        <ParamKnob id="sampler.gain" />
      </div>
    </section>
  );
}
