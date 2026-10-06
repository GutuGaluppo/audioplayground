import { useBridge } from '../bridge/BridgeContext';
import type { TimelineTrack } from '../bridge/generated';
import { ValueKnob } from '../components/Knob';
import { EFFECTS } from '../params/generated';
import type { EffectDescriptor } from '../params/generated';
import { useLatest } from '../state/latestEvent';
import { useStores } from '../state/StoresContext';
import { useSelection } from '../timeline/selection';
import { CURVES } from './curves';
import { PRESETS } from './presets';

const FILTER_MODES = ['Low-pass', 'High-pass', 'Band-pass'] as const;

/** One effect of the selected track's chain: on/off, a preset menu and its values. */
function EffectCard({
  track,
  index,
  effect,
}: {
  track: TimelineTrack;
  index: number;
  effect: EffectDescriptor;
}) {
  const bridge = useBridge();
  const state = track.effects[index];
  const enabled = state?.enabled ?? false;
  const values = effect.parameters.map((d, i) => state?.values[i] ?? d.defaultValue);
  const Curve = CURVES[effect.id];
  const presets = PRESETS.filter((p) => p.effect === index);

  const send = (next: { enabled?: boolean; values?: readonly number[] }, gesture = 0) => {
    bridge.send({
      type: 'track.setEffect',
      payload: {
        track: track.id,
        effect: index,
        enabled: next.enabled ?? enabled,
        values: [...(next.values ?? values)],
        gesture,
      },
    });
  };
  const setValue = (i: number, value: number, gesture: number) => {
    const next = [...values];
    next[i] = value;
    send({ values: next }, gesture);
  };

  return (
    <section
      className="effect"
      data-effect={effect.id}
      data-enabled={enabled}
      aria-label={`${effect.name} effect`}
    >
      <header className="effect__header">
        <button
          type="button"
          className="effect__toggle"
          aria-pressed={enabled}
          aria-label={`${effect.name} ${enabled ? 'on' : 'off'}`}
          onClick={() => {
            send({ enabled: !enabled });
          }}
        >
          {effect.name}
        </button>
        <select
          className="effect__preset"
          aria-label={`${effect.name} preset`}
          value=""
          onChange={(event) => {
            const preset = presets.find((p) => p.id === event.currentTarget.value);
            if (preset) send({ enabled: true, values: preset.values });
          }}
        >
          <option value="">Presets…</option>
          {presets.map((p) => (
            <option key={p.id} value={p.id}>
              {p.name}
            </option>
          ))}
        </select>
      </header>
      <div className="effect__body">
        <div className="effect__values">
          {effect.parameters.map((d, i) =>
            d.id === 'filter.mode' ? (
              <div key={d.id} className="segmented" role="radiogroup" aria-label="Filter mode">
                {FILTER_MODES.map((name, mode) => (
                  <button
                    key={name}
                    type="button"
                    role="radio"
                    aria-checked={values[i] === mode}
                    onClick={() => {
                      setValue(i, mode, 0);
                    }}
                  >
                    {name}
                  </button>
                ))}
              </div>
            ) : (
              <ValueKnob
                key={d.id}
                descriptor={d}
                value={values[i]}
                onChange={(value, gesture) => {
                  setValue(i, value, gesture);
                }}
              />
            ),
          )}
        </div>
        {Curve ? (
          <div className="effect__display">
            <Curve values={values} />
          </div>
        ) : null}
      </div>
    </section>
  );
}

/** The selected track's effect chain (ADR-009), in signal order. */
export function EffectsPanel() {
  const stores = useStores();
  const selection = useSelection(stores.selection);
  const timeline = useLatest(stores.timeline);
  const track = timeline?.tracks.find((t) => t.id === selection.track);
  if (!track) return null;

  return (
    <div className="effects" role="region" aria-label={`${track.name} effects`}>
      <h2 className="effects__title">{track.name} · effects</h2>
      <div className="effects__chain">
        {EFFECTS.map((effect, index) => (
          <EffectCard key={effect.id} track={track} index={index} effect={effect} />
        ))}
      </div>
    </div>
  );
}
