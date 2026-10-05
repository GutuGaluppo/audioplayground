import { useBridge } from '../bridge/BridgeContext';
import { ParamSlider } from '../components/ParamSlider';
import { useParameter } from '../params/parameterStore';
import { useStores } from '../state/StoresContext';

const WAVEFORMS = ['Sine', 'Triangle', 'Saw', 'Square'] as const;

export function SynthPanel() {
  const bridge = useBridge();
  const waveform = useParameter(useStores().parameters, 'synth.waveform');

  return (
    <section className="card synth" aria-label="Synth">
      <h2 className="card__title">Synth</h2>

      <div className="segmented" role="radiogroup" aria-label="Waveform">
        {WAVEFORMS.map((name, index) => (
          <button
            key={name}
            type="button"
            role="radio"
            aria-checked={waveform === index}
            disabled={waveform === undefined}
            onClick={() => {
              bridge.send({
                type: 'param.set',
                payload: { id: 'synth.waveform', value: index, gesture: 0 },
              });
            }}
          >
            {name}
          </button>
        ))}
      </div>

      <div className="synth__grid">
        <ParamSlider id="synth.cutoff" />
        <ParamSlider id="synth.resonance" />
        <ParamSlider id="synth.detune" />
        <ParamSlider id="synth.pitch" />
        <ParamSlider id="synth.attack" />
        <ParamSlider id="synth.decay" />
        <ParamSlider id="synth.sustain" />
        <ParamSlider id="synth.release" />
        <ParamSlider id="synth.volume" />
      </div>
    </section>
  );
}
