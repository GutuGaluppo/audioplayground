import { useEffect, useId, useRef } from 'react';
import type { ReactNode } from 'react';

const FOCUSABLE =
  'button:not([disabled]), input:not([disabled]), [href], select, [tabindex]:not([tabindex="-1"])';

/**
 * A question that holds the page until it is answered: named for screen readers, Escape cancels,
 * Tab stays inside it, focus moves in and goes back to where it was.
 */
export function Modal({
  title,
  onCancel,
  children,
}: {
  title: string;
  onCancel: () => void;
  children: ReactNode;
}) {
  const heading = useId();
  const box = useRef<HTMLDivElement>(null);

  useEffect(() => {
    const before = document.activeElement instanceof HTMLElement ? document.activeElement : null;
    const wanted = box.current?.querySelector<HTMLElement>('[data-autofocus]');
    (wanted ?? box.current?.querySelector<HTMLElement>(FOCUSABLE))?.focus();
    return () => {
      before?.focus();
    };
  }, []);

  const onKeyDown = (event: React.KeyboardEvent) => {
    if (event.key === 'Escape') {
      event.stopPropagation();
      onCancel();
      return;
    }
    if (event.key !== 'Tab' || !box.current) return;
    const items = [...box.current.querySelectorAll<HTMLElement>(FOCUSABLE)];
    const first = items[0];
    const last = items.at(-1);
    if (!first || !last) return;
    if (event.shiftKey && document.activeElement === first) {
      event.preventDefault();
      last.focus();
    } else if (!event.shiftKey && document.activeElement === last) {
      event.preventDefault();
      first.focus();
    }
  };

  return (
    <div className="dialog" onKeyDown={onKeyDown}>
      <div
        className="dialog__box"
        role="dialog"
        aria-modal="true"
        aria-labelledby={heading}
        ref={box}
      >
        <h2 id={heading} className="dialog__title">
          {title}
        </h2>
        {children}
      </div>
    </div>
  );
}
