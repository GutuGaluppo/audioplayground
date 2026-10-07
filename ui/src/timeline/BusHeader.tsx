import { useRef, useState } from 'react';

import { useBridge } from '../bridge/BridgeContext';
import type { TimelineBus } from '../bridge/generated';
import { Splitter } from '../components/Splitter';
import { beginGesture } from '../params/gestures';

/** Name, mute, volume and pan of one effect bus (ADR-010). Selecting it shows its effect chain. */
export function BusHeader({
  bus,
  selected,
  onSelect,
}: {
  bus: TimelineBus;
  selected: boolean;
  onSelect: () => void;
}) {
  const bridge = useBridge();
  const [renaming, setRenaming] = useState(false);
  const gesture = useRef(0);

  const commitName = (name: string) => {
    setRenaming(false);
    if (name.trim() && name !== bus.name)
      bridge.send({ type: 'bus.rename', payload: { bus: bus.id, name } });
  };

  return (
    <div
      className="track-header bus-header"
      data-selected={selected}
      data-kind="bus"
      role="group"
      aria-label={`${bus.name} bus`}
      onPointerDown={onSelect}
    >
      <Splitter pref="laneHeight" orientation="horizontal" grow={1} label="Track height" />
      <div className="track-header__top">
        <span className="track-header__icon" title="Effect bus" aria-hidden="true">
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.6">
            <path d="M4 8h9l3 4-3 4H4M16 12h4" strokeLinecap="round" strokeLinejoin="round" />
          </svg>
        </span>
        {renaming ? (
          <input
            className="track-header__name-input"
            aria-label="Bus name"
            defaultValue={bus.name}
            autoFocus
            onBlur={(event) => {
              commitName(event.currentTarget.value);
            }}
            onKeyDown={(event) => {
              if (event.key === 'Enter') event.currentTarget.blur();
              if (event.key === 'Escape') setRenaming(false);
            }}
          />
        ) : (
          <button
            type="button"
            className="track-header__name"
            title="Double-click to rename"
            onDoubleClick={() => {
              setRenaming(true);
            }}
          >
            {bus.name}
          </button>
        )}
        <button
          type="button"
          className="track-header__toggle"
          data-tone="mute"
          aria-pressed={bus.muted}
          aria-label={`Mute ${bus.name}`}
          title="Mute"
          onClick={() => {
            bridge.send({ type: 'bus.setMute', payload: { bus: bus.id, muted: !bus.muted } });
          }}
        >
          M
        </button>
      </div>
      <div className="track-header__bottom">
        <input
          type="range"
          className="track-header__volume"
          min={-60}
          max={6}
          step={0.5}
          value={bus.volumeDb}
          aria-label={`${bus.name} volume`}
          aria-valuetext={`${bus.volumeDb.toFixed(1)} dB`}
          title={`Volume ${bus.volumeDb.toFixed(1)} dB (double-click: 0 dB)`}
          onPointerDown={() => (gesture.current = beginGesture())}
          onPointerUp={() => (gesture.current = 0)}
          onDoubleClick={() => {
            bridge.send({
              type: 'bus.setVolume',
              payload: { bus: bus.id, volumeDb: 0, gesture: 0 },
            });
          }}
          onChange={(event) => {
            bridge.send({
              type: 'bus.setVolume',
              payload: {
                bus: bus.id,
                volumeDb: Number(event.currentTarget.value),
                gesture: gesture.current,
              },
            });
          }}
        />
        <input
          type="range"
          className="track-header__pan"
          min={-1}
          max={1}
          step={0.05}
          value={bus.pan}
          aria-label={`${bus.name} pan`}
          title="Pan (double-click: centre)"
          onPointerDown={() => (gesture.current = beginGesture())}
          onPointerUp={() => (gesture.current = 0)}
          onDoubleClick={() => {
            bridge.send({ type: 'bus.setPan', payload: { bus: bus.id, pan: 0, gesture: 0 } });
          }}
          onChange={(event) => {
            bridge.send({
              type: 'bus.setPan',
              payload: {
                bus: bus.id,
                pan: Number(event.currentTarget.value),
                gesture: gesture.current,
              },
            });
          }}
        />
        <button
          type="button"
          className="track-header__action track-header__delete"
          aria-label={`Delete ${bus.name}`}
          title="Delete bus (its sends go with it)"
          onClick={() => {
            bridge.send({ type: 'bus.remove', payload: { bus: bus.id } });
          }}
        >
          ×
        </button>
      </div>
    </div>
  );
}
