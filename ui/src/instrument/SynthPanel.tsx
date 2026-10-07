import { useBridge } from '../bridge/BridgeContext';
import { ParamKnob } from '../components/Knob';
import { useParameter } from '../params/parameterStore';
import { useStores } from '../state/StoresContext';
import { SYNTH_PRESETS } from './presets';

const WAVEFORMS = ['Sine', 'Triangle', 'Saw', 'Square'] as const;

export function SynthPanel() {
  const bridge = useBridge();
  const waveform = useParameter(useStores().parameters, 'synth.waveform');

  return (
    <section className="card synth" aria-label="Synth">
      <div className="sampler__header">
        <h2 className="card__title">Synth</h2>
        <select
          className="effect__preset"
          aria-label="Synth preset"
          value=""
          onChange={(event) => {
            const preset = SYNTH_PRESETS.find((p) => p.id === event.currentTarget.value);
            if (preset)
              bridge.send({ type: 'synth.setPreset', payload: { values: preset.values } });
          }}
        >
          <option value="">Presets…</option>
          {SYNTH_PRESETS.map((p) => (
            <option key={p.id} value={p.id}>
              {p.name}
            </option>
          ))}
        </select>
      </div>

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

      <div className="knob-grid">
        <ParamKnob id="synth.cutoff" />
        <ParamKnob id="synth.resonance" />
        <ParamKnob id="synth.detune" />
        <ParamKnob id="synth.pitch" />
        <ParamKnob id="synth.attack" />
        <ParamKnob id="synth.decay" />
        <ParamKnob id="synth.sustain" />
        <ParamKnob id="synth.release" />
        <ParamKnob id="synth.volume" />
      </div>
    </section>
  );
}
