import { useRef, useState } from 'react';

import { useBridge } from '../bridge/BridgeContext';
import type { TimelineTrack } from '../bridge/generated';
import { PeakMeter } from '../components/PeakMeter';
import { beginGesture } from '../params/gestures';
import { useLatest } from '../state/latestEvent';
import { useStores } from '../state/StoresContext';
import { kindLabel, TrackKind } from './model';

const KIND_ICONS = ['〰', '◆', '▶', '◼'];

/** Name, mute, solo, volume and pan of one track (guide §3.5: no separate mixer in the MVP). */
export function TrackHeader({
  track,
  selected,
  onSelect,
  onImport,
}: {
  track: TimelineTrack;
  selected: boolean;
  onSelect: () => void;
  onImport: () => void;
}) {
  const bridge = useBridge();
  const transport = useLatest(useStores().transportState);
  const [renaming, setRenaming] = useState(false);
  const [inputSilent, setInputSilent] = useState(false);
  const gesture = useRef(0);
  const isAudio = track.kind === TrackKind.audio;
  const armed = isAudio && transport?.armedTrack === track.id;

  const commitName = (name: string) => {
    setRenaming(false);
    if (name.trim() && name !== track.name)
      bridge.send({ type: 'track.rename', payload: { track: track.id, name } });
  };

  return (
    <div
      className="track-header"
      data-selected={selected}
      data-kind={track.kind}
      role="group"
      aria-label={`${track.name} track`}
      onPointerDown={onSelect}
    >
      <div className="track-header__top">
        <span className="track-header__icon" title={kindLabel(track.kind)} aria-hidden="true">
          {KIND_ICONS[track.kind] ?? '•'}
        </span>
        {renaming ? (
          <input
            className="track-header__name-input"
            aria-label="Track name"
            defaultValue={track.name}
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
            {track.name}
          </button>
        )}
        {isAudio ? (
          <button
            type="button"
            className="track-header__toggle"
            data-tone="arm"
            aria-pressed={armed}
            aria-label={`Record into ${track.name}`}
            title={
              armed
                ? 'Armed: Record captures the audio input on this track'
                : 'Arm for recording (opens the audio input)'
            }
            disabled={transport?.recording ?? false}
            onClick={() => {
              bridge.send({ type: 'track.setArmed', payload: { track: track.id, armed: !armed } });
            }}
          >
            ●
          </button>
        ) : null}
        <button
          type="button"
          className="track-header__toggle"
          data-tone="mute"
          aria-pressed={track.muted}
          aria-label={`Mute ${track.name}`}
          title="Mute"
          onClick={() => {
            bridge.send({
              type: 'track.setMute',
              payload: { track: track.id, muted: !track.muted },
            });
          }}
        >
          M
        </button>
        <button
          type="button"
          className="track-header__toggle"
          data-tone="solo"
          aria-pressed={track.soloed}
          aria-label={`Solo ${track.name}`}
          title="Solo"
          onClick={() => {
            bridge.send({
              type: 'track.setSolo',
              payload: { track: track.id, soloed: !track.soloed },
            });
          }}
        >
          S
        </button>
      </div>
      <div className="track-header__bottom">
        <input
          type="range"
          className="track-header__volume"
          min={-60}
          max={6}
          step={0.5}
          value={track.volumeDb}
          aria-label={`${track.name} volume`}
          aria-valuetext={`${track.volumeDb.toFixed(1)} dB`}
          title={`Volume ${track.volumeDb.toFixed(1)} dB (double-click: 0 dB)`}
          onPointerDown={() => (gesture.current = beginGesture())}
          onPointerUp={() => (gesture.current = 0)}
          onDoubleClick={() => {
            bridge.send({
              type: 'track.setVolume',
              payload: { track: track.id, volumeDb: 0, gesture: 0 },
            });
          }}
          onChange={(event) => {
            bridge.send({
              type: 'track.setVolume',
              payload: {
                track: track.id,
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
          value={track.pan}
          aria-label={`${track.name} pan`}
          aria-valuetext={
            track.pan === 0
              ? 'centre'
              : `${String(Math.round(Math.abs(track.pan) * 100))}% ${track.pan < 0 ? 'left' : 'right'}`
          }
          title="Pan (double-click: centre)"
          onPointerDown={() => (gesture.current = beginGesture())}
          onPointerUp={() => (gesture.current = 0)}
          onDoubleClick={() => {
            bridge.send({ type: 'track.setPan', payload: { track: track.id, pan: 0, gesture: 0 } });
          }}
          onChange={(event) => {
            bridge.send({
              type: 'track.setPan',
              payload: {
                track: track.id,
                pan: Number(event.currentTarget.value),
                gesture: gesture.current,
              },
            });
          }}
        />
        {isAudio ? (
          <button
            type="button"
            className="track-header__action"
            title="Import an audio file at the playhead"
            onClick={onImport}
          >
            Import…
          </button>
        ) : null}
        <button
          type="button"
          className="track-header__action track-header__delete"
          aria-label={`Delete ${track.name}`}
          title="Delete track"
          onClick={() => {
            bridge.send({ type: 'track.remove', payload: { track: track.id } });
          }}
        >
          ×
        </button>
      </div>
      {armed ? (
        <>
          <PeakMeter
            source="input"
            label={`${track.name} input level`}
            className="meter track-header__input"
            onSilence={setInputSilent}
          />
          {inputSilent ? (
            <p
              className="track-header__hint"
              role="status"
              title="No input signal. Check the microphone permission in your system settings."
            >
              No input signal. Check the microphone permission in your system settings.
            </p>
          ) : null}
        </>
      ) : null}
    </div>
  );
}
