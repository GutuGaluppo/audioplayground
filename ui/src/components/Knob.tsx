import { useId, useRef } from 'react';
import type { KeyboardEvent, PointerEvent, WheelEvent } from 'react';

import { useBridge } from '../bridge/BridgeContext';
import { PARAMETERS } from '../params/generated';
import type { ParamId, ValueDescriptor } from '../params/generated';
import { beginGesture } from '../params/gestures';
import { formatValue, fromNormalized, snapToStep, toNormalized } from '../params/mapping';
import { useParameter } from '../params/parameterStore';
import { useStores } from '../state/StoresContext';

const DRAG_PX = 160; // vertical travel for the full range
const SWEEP = 270; // degrees from min to max

/**
 * Rotary control for any described value. Same contract as ValueSlider: onChange gets the snapped
 * value and the gesture it belongs to (0 for a single change), so one drag is one undo step.
 * Drag up/down, scroll, or use the arrow keys; double-click resets.
 */
export function ValueKnob({
  descriptor,
  value,
  label,
  size = 'md',
  disabled = false,
  onChange,
}: {
  descriptor: ValueDescriptor;
  value: number | undefined;
  label?: string;
  size?: 'sm' | 'md';
  disabled?: boolean;
  onChange: (value: number, gesture: number) => void;
}) {
  const labelId = useId();
  const gesture = useRef(0);
  const drag = useRef<{ y: number; start: number } | null>(null);
  const shown = value ?? descriptor.defaultValue;
  const normalized = toNormalized(descriptor, shown);
  const inactive = disabled || value === undefined;
  const name = label ?? descriptor.name;

  const commit = (next: number, id: number) => {
    onChange(
      snapToStep(descriptor, fromNormalized(descriptor, Math.min(1, Math.max(0, next)))),
      id,
    );
  };

  const onPointerDown = (event: PointerEvent<HTMLDivElement>) => {
    if (inactive) return;
    event.currentTarget.setPointerCapture(event.pointerId);
    drag.current = { y: event.clientY, start: normalized };
    gesture.current = beginGesture();
  };
  const onPointerMove = (event: PointerEvent<HTMLDivElement>) => {
    const d = drag.current;
    if (!d) return;
    const scale = event.shiftKey ? 4 : 1; // Shift = fine
    commit(d.start + (d.y - event.clientY) / (DRAG_PX * scale), gesture.current);
  };
  const endDrag = () => {
    drag.current = null;
    gesture.current = 0;
  };
  const onWheel = (event: WheelEvent<HTMLDivElement>) => {
    if (inactive) return;
    commit(normalized - Math.sign(event.deltaY) * 0.02, 0);
  };
  const onKeyDown = (event: KeyboardEvent<HTMLDivElement>) => {
    if (inactive) return;
    const step = event.shiftKey ? 0.1 : 0.02;
    const moves: Record<string, number | undefined> = {
      ArrowUp: normalized + step,
      ArrowRight: normalized + step,
      ArrowDown: normalized - step,
      ArrowLeft: normalized - step,
      PageUp: normalized + 0.1,
      PageDown: normalized - 0.1,
      Home: 0,
      End: 1,
    };
    const next = moves[event.key];
    if (next === undefined) return;
    event.preventDefault();
    commit(next, 0);
  };

  return (
    <div className="knob" data-size={size} data-disabled={inactive}>
      <div
        role="slider"
        tabIndex={inactive ? -1 : 0}
        aria-labelledby={labelId}
        aria-valuemin={descriptor.min}
        aria-valuemax={descriptor.max}
        aria-valuenow={shown}
        aria-valuetext={formatValue(descriptor, shown)}
        aria-disabled={inactive}
        title="Drag, scroll or use the arrow keys. Double-click to reset."
        className="knob__dial"
        style={{ ['--knob-angle' as string]: `${String(-SWEEP / 2 + normalized * SWEEP)}deg` }}
        onPointerDown={onPointerDown}
        onPointerMove={onPointerMove}
        onPointerUp={endDrag}
        onPointerCancel={endDrag}
        onWheel={onWheel}
        onKeyDown={onKeyDown}
        onDoubleClick={() => {
          if (!inactive) onChange(descriptor.defaultValue, 0);
        }}
      >
        <span className="knob__cap" />
      </div>
      <span id={labelId} className="knob__label">
        {name}
      </span>
      <output className="knob__value">{formatValue(descriptor, shown)}</output>
    </div>
  );
}

/** Knob for a global parameter, driven by its generated descriptor. */
export function ParamKnob({
  id,
  label,
  size,
}: {
  id: ParamId;
  label?: string;
  size?: 'sm' | 'md';
}) {
  const bridge = useBridge();
  const value = useParameter(useStores().parameters, id);
  return (
    <ValueKnob
      descriptor={PARAMETERS[id]}
      value={value}
      {...(label !== undefined ? { label } : {})}
      {...(size !== undefined ? { size } : {})}
      onChange={(next, gesture) => {
        bridge.send({ type: 'param.set', payload: { id, value: next, gesture } });
      }}
    />
  );
}
