import { useId, useRef } from 'react';

import { useBridge } from '../bridge/BridgeContext';
import { PARAMETERS } from '../params/generated';
import type { ParamId } from '../params/generated';
import { beginGesture } from '../params/gestures';
import { formatValue, fromNormalized, snapToStep, toNormalized } from '../params/mapping';
import { useParameter } from '../params/parameterStore';
import { useStores } from '../state/StoresContext';

const SLIDER_RESOLUTION = 1000;

/** Horizontal slider for any parameter, driven entirely by its generated descriptor. */
export function ParamSlider({ id, label }: { id: ParamId; label?: string }) {
  const bridge = useBridge();
  const descriptor = PARAMETERS[id];
  const value = useParameter(useStores().parameters, id);
  const inputId = useId();
  const shown = value ?? descriptor.defaultValue;
  const gesture = useRef(0);

  const send = (plain: number) => {
    bridge.send({
      type: 'param.set',
      payload: { id, value: snapToStep(descriptor, plain), gesture: gesture.current },
    });
  };

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
        disabled={value === undefined}
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
          send(fromNormalized(descriptor, Number(event.currentTarget.value) / SLIDER_RESOLUTION));
        }}
        onDoubleClick={() => {
          gesture.current = 0;
          send(descriptor.defaultValue);
        }}
      />
      <output htmlFor={inputId}>{formatValue(descriptor, shown)}</output>
    </div>
  );
}
