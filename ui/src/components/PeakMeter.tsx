import { useEffect, useRef } from 'react';

import { useBridge } from '../bridge/BridgeContext';

const DECAY_PER_FRAME = 0.85;

/**
 * Output (or input) peak meter. Updated through a ref at display rate, never through React state.
 * onSilence, if given, is told whether the source has been exactly silent for silenceMs (a
 * microphone the system refused to open delivers digital silence).
 */
export function PeakMeter({
  source = 'output',
  label = 'Output level meter',
  className = 'meter',
  silenceMs = 3000,
  onSilence,
}: {
  source?: 'output' | 'input';
  label?: string;
  className?: string;
  silenceMs?: number;
  onSilence?: (silent: boolean) => void;
}) {
  const bridge = useBridge();
  const fillRef = useRef<HTMLDivElement>(null);
  const onSilenceRef = useRef(onSilence);
  useEffect(() => {
    onSilenceRef.current = onSilence;
  }, [onSilence]);

  useEffect(() => {
    let target = 0;
    let shown = 0;
    let frame = 0;
    let lastSignal = performance.now();
    let silent = false;

    const unsubscribe = bridge.on('engine.meters', ({ peak, inputPeak }) => {
      const level = source === 'input' ? inputPeak : peak;
      target = Math.max(target, level);
      if (level > 0) lastSignal = performance.now();
    });

    const draw = () => {
      shown = Math.max(target, shown * DECAY_PER_FRAME);
      target = 0;
      const fill = fillRef.current;
      if (fill) {
        fill.style.transform = `scaleX(${String(Math.min(shown, 1))})`;
        fill.dataset['hot'] = shown > 0.7 ? 'true' : 'false';
      }
      const nowSilent = performance.now() - lastSignal > silenceMs;
      if (nowSilent !== silent) {
        silent = nowSilent;
        onSilenceRef.current?.(silent);
      }
      frame = requestAnimationFrame(draw);
    };
    frame = requestAnimationFrame(draw);

    return () => {
      cancelAnimationFrame(frame);
      unsubscribe();
    };
  }, [bridge, source, silenceMs]);

  return (
    <div className={className} role="img" aria-label={label}>
      <div ref={fillRef} className="meter__fill" />
    </div>
  );
}
