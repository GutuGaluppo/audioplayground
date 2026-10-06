import { useEffect, useRef } from 'react';

import { useBridge } from '../bridge/BridgeContext';
import { ParamKnob } from '../components/Knob';
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

      {sampler?.loaded ? <Waveform overview={sampler.overview} start={start} end={end} /> : null}

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
