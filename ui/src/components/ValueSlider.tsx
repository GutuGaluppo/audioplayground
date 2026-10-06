import { useId, useRef } from 'react';

import type { ValueDescriptor } from '../params/generated';
import { beginGesture } from '../params/gestures';
import { formatValue, fromNormalized, snapToStep, toNormalized } from '../params/mapping';

const SLIDER_RESOLUTION = 1000;

/**
 * Horizontal slider for any described value (range, curve, step, unit). onChange receives the
 * snapped value and the gesture it belongs to (0 for a single change), so a drag is one undo step.
 */
export function ValueSlider({
  descriptor,
  value,
  label,
  disabled = false,
  onChange,
}: {
  descriptor: ValueDescriptor;
  value: number | undefined;
  label?: string;
  disabled?: boolean;
  onChange: (value: number, gesture: number) => void;
}) {
  const inputId = useId();
  const gesture = useRef(0);
  const shown = value ?? descriptor.defaultValue;

  return (
    <div className="field">
      <label htmlFor={inputId}>{label ?? descriptor.name}</label>
      <input
        id={inputId}
        type="range"
        min={0}
        max={SLIDER_RESOLUTION}
        step={1}
        value={Math.round(toNormalized(descriptor, shown) * SLIDER_RESOLUTION)}
        disabled={disabled || value === undefined}
        aria-valuetext={formatValue(descriptor, shown)}
        title="Double-click to reset"
        onPointerDown={() => {
          gesture.current = beginGesture();
        }}
        onPointerUp={() => {
          gesture.current = 0;
        }}
        onPointerCancel={() => {
          gesture.current = 0;
        }}
        onChange={(event) => {
          const plain = fromNormalized(
            descriptor,
            Number(event.currentTarget.value) / SLIDER_RESOLUTION,
          );
          onChange(snapToStep(descriptor, plain), gesture.current);
        }}
        onDoubleClick={() => {
          gesture.current = 0;
          onChange(descriptor.defaultValue, 0);
        }}
      />
      <output htmlFor={inputId}>{formatValue(descriptor, shown)}</output>
    </div>
  );
}
