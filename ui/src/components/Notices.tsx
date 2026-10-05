import { useEffect, useState } from 'react';

import { useBridge } from '../bridge/BridgeContext';

interface Notice {
  readonly id: number;
  readonly level: number;
  readonly message: string;
}

const DISMISS_AFTER_MS = 6000;
const MAX_VISIBLE = 3;

/** Transient messages from native code. Errors are announced assertively to screen readers. */
export function Notices() {
  const bridge = useBridge();
  const [notices, setNotices] = useState<readonly Notice[]>([]);

  useEffect(() => {
    let nextId = 0;
    const timers = new Set<number>();
    const unsubscribe = bridge.on('app.notice', ({ level, message }) => {
      const id = nextId++;
      setNotices((current) => [...current, { id, level, message }].slice(-MAX_VISIBLE));
      const timer = window.setTimeout(() => {
        timers.delete(timer);
        setNotices((current) => current.filter((n) => n.id !== id));
      }, DISMISS_AFTER_MS);
      timers.add(timer);
    });
    return () => {
      unsubscribe();
      timers.forEach((timer) => {
        window.clearTimeout(timer);
      });
    };
  }, [bridge]);

  return (
    <div className="notices">
      {notices.map((notice) => (
        <div
          key={notice.id}
          className="notice"
          data-level={notice.level}
          role={notice.level === 2 ? 'alert' : 'status'}
        >
          <span>{notice.message}</span>
          <button
            type="button"
            className="notice__close"
            aria-label="Dismiss"
            onClick={() => {
              setNotices((current) => current.filter((n) => n.id !== notice.id));
            }}
          >
            ×
          </button>
        </div>
      ))}
    </div>
  );
}
