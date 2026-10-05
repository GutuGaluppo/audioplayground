import { useEffect, useRef } from 'react';

import { useBridge } from '../bridge/BridgeContext';

const DECAY_PER_FRAME = 0.85;

/** Output peak meter. Updated through a ref at display rate, never through React state. */
export function PeakMeter() {
  const bridge = useBridge();
  const fillRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    let target = 0;
    let shown = 0;
    let frame = 0;

    const unsubscribe = bridge.on('engine.meters', ({ peak }) => {
      target = Math.max(target, peak);
    });

    const draw = () => {
      shown = Math.max(target, shown * DECAY_PER_FRAME);
      target = 0;
      const fill = fillRef.current;
      if (fill) {
        fill.style.transform = `scaleX(${String(Math.min(shown, 1))})`;
        fill.dataset['hot'] = shown > 0.7 ? 'true' : 'false';
      }
      frame = requestAnimationFrame(draw);
    };
    frame = requestAnimationFrame(draw);

    return () => {
      cancelAnimationFrame(frame);
      unsubscribe();
    };
  }, [bridge]);

  return (
    <div className="meter" role="img" aria-label="Output level meter">
      <div ref={fillRef} className="meter__fill" />
    </div>
  );
}
