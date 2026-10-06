import { useBridge } from '../bridge/BridgeContext';
import { PARAMETERS } from '../params/generated';
import type { ParamId } from '../params/generated';
import { useParameter } from '../params/parameterStore';
import { useStores } from '../state/StoresContext';
import { ValueSlider } from './ValueSlider';

/** Slider for a global parameter, driven entirely by its generated descriptor. */
export function ParamSlider({ id, label }: { id: ParamId; label?: string }) {
  const bridge = useBridge();
  const value = useParameter(useStores().parameters, id);
  return (
    <ValueSlider
      descriptor={PARAMETERS[id]}
      value={value}
      {...(label !== undefined ? { label } : {})}
      onChange={(next, gesture) => {
        bridge.send({ type: 'param.set', payload: { id, value: next, gesture } });
      }}
    />
  );
}
