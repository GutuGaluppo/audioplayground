import { useEffect, useRef, useState } from 'react';

import { useBridge } from '../bridge/BridgeContext';
import { beginGesture } from '../params/gestures';
import { useKeyed } from '../state/keyedEvent';
import { useLatest } from '../state/latestEvent';
import { useStores } from '../state/StoresContext';
import { hasStep, keyLabelForPad, padForKey } from './drumKeys';

const PADS = Array.from({ length: 16 }, (_, i) => i);
const STEPS = Array.from({ length: 16 }, (_, i) => i);
// Display order: top row shows pads 12-15, bottom row pads 0-3.
const PAD_GRID_ORDER = [12, 13, 14, 15, 8, 9, 10, 11, 4, 5, 6, 7, 0, 1, 2, 3];

function isTextEntry(target: EventTarget | null): boolean {
  return (
    target instanceof HTMLElement &&
    (target.isContentEditable ||
      target.tagName === 'TEXTAREA' ||
      (target instanceof HTMLInputElement && target.type !== 'range'))
  );
}

function PadButton({
  pad,
  selected,
  onSelect,
}: {
  pad: number;
  selected: boolean;
  onSelect: (pad: number) => void;
}) {
  const bridge = useBridge();
  const info = useKeyed(useStores().drumPads, pad);
  return (
    <button
      type="button"
      className="pad"
      aria-pressed={selected}
      data-muted={info?.muted ?? false}
      data-missing={info?.missing ?? false}
      aria-label={`${info?.name ?? `Pad ${String(pad + 1)}`}, key ${keyLabelForPad(pad)}`}
      onPointerDown={(event) => {
        event.preventDefault();
        onSelect(pad);
        bridge.send({ type: 'drums.trigger', payload: { pad, velocity: 1 } });
      }}
    >
      <span className="pad__name">{info?.name ?? ''}</span>
      <kbd className="pad__key">{keyLabelForPad(pad)}</kbd>
    </button>
  );
}

function StepRow({
  pad,
  mask,
  selected,
  playingStep,
  onSelect,
  paint,
}: {
  pad: number;
  mask: number;
  selected: boolean;
  playingStep: number | null;
  onSelect: (pad: number) => void;
  paint: { current: { on: boolean; gesture: number } | null };
}) {
  const bridge = useBridge();
  const info = useKeyed(useStores().drumPads, pad);

  const set = (step: number, on: boolean, gesture: number) => {
    bridge.send({ type: 'drums.setStep', payload: { pad, step, on, gesture } });
  };

  return (
    <div className="steps__row" data-selected={selected}>
      <button
        type="button"
        className="steps__label"
        onClick={() => {
          onSelect(pad);
        }}
        data-muted={info?.muted ?? false}
      >
        {info?.name ?? ''}
      </button>
      {STEPS.map((step) => {
        const on = hasStep(mask, step);
        return (
          <button
            key={step}
            type="button"
            className="step"
            aria-label={`${info?.name ?? 'Pad'} step ${String(step + 1)}`}
            aria-pressed={on}
            data-beat={step % 4 === 0}
            data-playing={playingStep === step}
            onPointerDown={(event) => {
              event.preventDefault();
              event.currentTarget.releasePointerCapture(event.pointerId);
              paint.current = { on: !on, gesture: beginGesture() };
              set(step, !on, paint.current.gesture);
            }}
            onPointerEnter={() => {
              const stroke = paint.current;
              if (stroke && on !== stroke.on) set(step, stroke.on, stroke.gesture);
            }}
          />
        );
      })}
    </div>
  );
}

function PadControls({ pad }: { pad: number }) {
  const bridge = useBridge();
  const info = useKeyed(useStores().drumPads, pad);
  const gesture = useRef(0);

  if (!info) return null;
  const update = (changes: Partial<{ volumeDb: number; pitch: number; muted: boolean }>) => {
    bridge.send({
      type: 'drums.setPad',
      payload: {
        pad,
        volumeDb: changes.volumeDb ?? info.volumeDb,
        pitch: changes.pitch ?? info.pitch,
        muted: changes.muted ?? info.muted,
        gesture: gesture.current,
      },
    });
  };

  return (
    <div className="pad-controls">
      <div className="pad-controls__title">
        <strong>{info.name}</strong>
        {info.missing ? (
          <span className="pad-controls__missing">missing — using the built-in sound</span>
        ) : null}
      </div>
      <div className="field">
        <label htmlFor={`pad-volume-${String(pad)}`}>Volume</label>
        <input
          id={`pad-volume-${String(pad)}`}
          type="range"
          min={-60}
          max={6}
          step={0.5}
          value={info.volumeDb}
          onPointerDown={() => (gesture.current = beginGesture())}
          onPointerUp={() => (gesture.current = 0)}
          onChange={(event) => {
            update({ volumeDb: Number(event.currentTarget.value) });
          }}
        />
        <output>{info.volumeDb.toFixed(1)} dB</output>
      </div>
      <div className="field">
        <label htmlFor={`pad-pitch-${String(pad)}`}>Pitch</label>
        <input
          id={`pad-pitch-${String(pad)}`}
          type="range"
          min={-24}
          max={24}
          step={1}
          value={info.pitch}
          onPointerDown={() => (gesture.current = beginGesture())}
          onPointerUp={() => (gesture.current = 0)}
          onChange={(event) => {
            update({ pitch: Number(event.currentTarget.value) });
          }}
        />
        <output>{String(info.pitch)} st</output>
      </div>
      <div className="pad-controls__actions">
        <button
          type="button"
          className="toggle"
          aria-pressed={info.muted}
          onClick={() => {
            gesture.current = 0;
            update({ muted: !info.muted });
          }}
        >
          Mute
        </button>
        <button
          type="button"
          className="button button--quiet"
          onClick={() => {
            bridge.send({ type: 'drums.loadPad', payload: { pad } });
          }}
        >
          Load sample…
        </button>
        {info.custom ? (
          <button
            type="button"
            className="button button--quiet"
            onClick={() => {
              bridge.send({ type: 'drums.resetPad', payload: { pad } });
            }}
          >
            Use built-in
          </button>
        ) : null}
      </div>
    </div>
  );
}

export function DrumPanel() {
  const bridge = useBridge();
  const stores = useStores();
  const pattern = useLatest(stores.drumPattern);
  const transport = useLatest(stores.transportState);
  const position = useLatest(stores.transportPosition);
  const [selected, setSelected] = useState(0);
  const paint = useRef<{ on: boolean; gesture: number } | null>(null);

  useEffect(() => {
    const endStroke = () => (paint.current = null);
    const onKeyDown = (event: KeyboardEvent) => {
      if (
        event.metaKey ||
        event.ctrlKey ||
        event.altKey ||
        event.repeat ||
        isTextEntry(event.target)
      )
        return;
      const pad = padForKey(event.code);
      if (pad === null) return;
      event.preventDefault();
      setSelected(pad);
      bridge.send({ type: 'drums.trigger', payload: { pad, velocity: 1 } });
    };
    window.addEventListener('pointerup', endStroke);
    window.addEventListener('keydown', onKeyDown);
    return () => {
      window.removeEventListener('pointerup', endStroke);
      window.removeEventListener('keydown', onKeyDown);
    };
  }, [bridge]);

  const playingStep = transport?.playing && position && !position.countingIn ? position.step : null;

  return (
    <section className="card drums" aria-label="Drums">
      <div className="sampler__header">
        <h2 className="card__title">Drums</h2>
        <button
          type="button"
          className="button button--quiet"
          onClick={() => {
            bridge.send({ type: 'drums.clear', payload: {} });
          }}
        >
          Clear pattern
        </button>
      </div>

      <div className="drums__layout">
        <div className="pads" role="group" aria-label="Pads">
          {PAD_GRID_ORDER.map((pad) => (
            <PadButton key={pad} pad={pad} selected={pad === selected} onSelect={setSelected} />
          ))}
        </div>
        <PadControls pad={selected} />
      </div>

      <div className="steps" role="grid" aria-label="Pattern">
        {PADS.map((pad) => (
          <StepRow
            key={pad}
            pad={pad}
            mask={pattern?.steps[pad] ?? 0}
            selected={pad === selected}
            playingStep={playingStep}
            onSelect={setSelected}
            paint={paint}
          />
        ))}
      </div>
    </section>
  );
}
