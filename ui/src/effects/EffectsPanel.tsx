import { useBridge } from '../bridge/BridgeContext';
import type { TimelineBus, TimelineEffect, TimelineTrack } from '../bridge/generated';
import { Splitter } from '../components/Splitter';
import { ValueKnob } from '../components/Knob';
import { EFFECTS } from '../params/generated';
import type { EffectDescriptor } from '../params/generated';
import { useLatest } from '../state/latestEvent';
import { useStores } from '../state/StoresContext';
import { useSelection } from '../timeline/selection';
import { CURVES } from './curves';
import { PRESETS } from './presets';

const FILTER_MODES = ['Low-pass', 'High-pass', 'Band-pass'] as const;

interface EffectEdit {
  readonly enabled: boolean;
  readonly values: readonly number[];
}

/** One effect of a track's or bus's chain: on/off, a preset menu and its values. */
function EffectCard({
  effects,
  index,
  effect,
  onEdit,
}: {
  effects: readonly TimelineEffect[];
  index: number;
  effect: EffectDescriptor;
  onEdit: (edit: EffectEdit, gesture: number) => void;
}) {
  const state = effects[index];
  const enabled = state?.enabled ?? false;
  const values = effect.parameters.map((d, i) => state?.values[i] ?? d.defaultValue);
  const Curve = CURVES[effect.id];
  const presets = PRESETS.filter((p) => p.effect === index);

  const send = (next: { enabled?: boolean; values?: readonly number[] }, gesture = 0) => {
    onEdit({ enabled: next.enabled ?? enabled, values: next.values ?? values }, gesture);
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

const SEND_DESCRIPTOR = {
  id: 'send.level',
  name: 'Send',
  unit: 'dB',
  min: -60,
  max: 6,
  defaultValue: -60,
  step: 0.5,
  curve: 'linear',
} as const;

/** How much of the track goes to each bus (post-fader). All the way down means no send. */
function SendsCard({ track, buses }: { track: TimelineTrack; buses: readonly TimelineBus[] }) {
  const bridge = useBridge();
  return (
    <section className="effect effect--sends" data-effect="sends" aria-label="Sends">
      <header className="effect__header">
        <span className="effect__title">Sends</span>
      </header>
      <div className="effect__body">
        <div className="effect__values">
          {buses.map((bus) => (
            <ValueKnob
              key={bus.id}
              descriptor={SEND_DESCRIPTOR}
              label={`Send to ${bus.name}`}
              value={track.sends.find((s) => s.bus === bus.id)?.levelDb ?? SEND_DESCRIPTOR.min}
              onChange={(levelDb, gesture) => {
                bridge.send({
                  type: 'track.setSend',
                  payload: { track: track.id, bus: bus.id, levelDb, gesture },
                });
              }}
            />
          ))}
        </div>
      </div>
    </section>
  );
}

/** The selected track's (or bus's) effect chain (ADR-009, ADR-010), in signal order. */
export function EffectsPanel() {
  const bridge = useBridge();
  const stores = useStores();
  const selection = useSelection(stores.selection);
  const timeline = useLatest(stores.timeline);
  const track = timeline?.tracks.find((t) => t.id === selection.track);
  const bus = timeline?.buses.find((b) => b.id === selection.bus);
  const buses = timeline?.buses ?? [];
  if (!track && !bus) return null;

  const name = bus ? bus.name : (track?.name ?? '');
  const effects = bus ? bus.effects : (track?.effects ?? []);

  return (
    <>
      <Splitter pref="effectsWidth" orientation="vertical" grow={-1} label="Effects panel width" />
      <div className="effects" role="region" aria-label={`${name} effects`}>
        <h2 className="effects__title">
          {name} · {bus ? 'bus effects' : 'effects'}
        </h2>
        <div className="effects__chain">
          {track && buses.length > 0 ? <SendsCard track={track} buses={buses} /> : null}
          {EFFECTS.map((effect, index) => (
            <EffectCard
              key={effect.id}
              effects={effects}
              index={index}
              effect={effect}
              onEdit={(next, gesture) => {
                const values = [...next.values];
                if (bus)
                  bridge.send({
                    type: 'bus.setEffect',
                    payload: { bus: bus.id, effect: index, enabled: next.enabled, values, gesture },
                  });
                else if (track)
                  bridge.send({
                    type: 'track.setEffect',
                    payload: {
                      track: track.id,
                      effect: index,
                      enabled: next.enabled,
                      values,
                      gesture,
                    },
                  });
              }}
            />
          ))}
        </div>
      </div>
    </>
  );
}
