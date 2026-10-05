import { useBridge } from '../bridge/BridgeContext';
import { useLatest } from '../state/latestEvent';
import { useStores } from '../state/StoresContext';
import { SamplerPanel } from './SamplerPanel';
import { SynthPanel } from './SynthPanel';

const INSTRUMENTS = ['Synth', 'Sampler'] as const;

/** Chooses what the keyboard plays and shows that instrument's controls. */
export function InstrumentArea() {
  const bridge = useBridge();
  const state = useLatest(useStores().instrument);
  const selected = state?.instrument ?? 0;

  return (
    <div className="instrument">
      <div className="tabs" role="tablist" aria-label="Instrument">
        {INSTRUMENTS.map((name, index) => (
          <button
            key={name}
            type="button"
            role="tab"
            aria-selected={selected === index}
            onClick={() => {
              bridge.send({ type: 'instrument.select', payload: { instrument: index } });
            }}
          >
            {name}
          </button>
        ))}
      </div>
      <div role="tabpanel" aria-label={INSTRUMENTS[selected]}>
        {selected === 1 ? <SamplerPanel /> : <SynthPanel />}
      </div>
    </div>
  );
}
