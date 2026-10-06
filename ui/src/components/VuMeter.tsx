import { useEffect, useRef } from 'react';

import { useBridge } from '../bridge/BridgeContext';

const MIN_DB = -40;
const MAX_DB = 3;
const BALLISTICS = 0.12; // needle smoothing per frame

const TICKS = [-40, -30, -20, -10, -5, 0, 3] as const;

function angleFor(db: number): number {
  const t = (Math.min(MAX_DB, Math.max(MIN_DB, db)) - MIN_DB) / (MAX_DB - MIN_DB);
  return -50 + t * 100;
}

function polar(angle: number, radius: number): [number, number] {
  const rad = ((angle - 90) * Math.PI) / 180;
  return [100 + radius * Math.cos(rad), 118 + radius * Math.sin(rad)];
}

/**
 * Analogue-style output meter (VU face). The needle follows the output peak through a ref at
 * display rate, never through React state.
 */
export function VuMeter({ label = 'Output VU meter' }: { label?: string }) {
  const bridge = useBridge();
  const needle = useRef<SVGLineElement>(null);

  useEffect(() => {
    let target = 0;
    let shown = MIN_DB;
    let frame = 0;
    const unsubscribe = bridge.on('engine.meters', ({ peak }) => {
      target = Math.max(target, peak);
    });
    const draw = () => {
      const db = target > 0 ? 20 * Math.log10(target) : MIN_DB;
      target = 0;
      shown += (db - shown) * BALLISTICS * (db > shown ? 3 : 1);
      needle.current?.setAttribute('transform', `rotate(${String(angleFor(shown))} 100 118)`);
      frame = requestAnimationFrame(draw);
    };
    frame = requestAnimationFrame(draw);
    return () => {
      cancelAnimationFrame(frame);
      unsubscribe();
    };
  }, [bridge]);

  const arc = (from: number, to: number, radius: number) => {
    const [x1, y1] = polar(angleFor(from), radius);
    const [x2, y2] = polar(angleFor(to), radius);
    return `M${x1.toFixed(1)} ${y1.toFixed(1)}A${String(radius)} ${String(radius)} 0 0 1 ${x2.toFixed(1)} ${y2.toFixed(1)}`;
  };

  return (
    <svg className="vu" viewBox="0 0 200 110" role="img" aria-label={label}>
      <path className="vu__arc" d={arc(MIN_DB, 0, 84)} />
      <path className="vu__arc vu__arc--hot" d={arc(0, MAX_DB, 84)} />
      {TICKS.map((db) => {
        const [x1, y1] = polar(angleFor(db), 84);
        const [x2, y2] = polar(angleFor(db), 94);
        const [tx, ty] = polar(angleFor(db), 74);
        return (
          <g key={db}>
            <line className="vu__tick" x1={x1} y1={y1} x2={x2} y2={y2} />
            <text className="vu__num" x={tx} y={ty} textAnchor="middle">
              {db}
            </text>
          </g>
        );
      })}
      <text className="vu__unit" x="100" y="100" textAnchor="middle">
        dB
      </text>
      <line ref={needle} className="vu__needle" x1="100" y1="118" x2="100" y2="40" />
    </svg>
  );
}
