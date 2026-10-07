import { useEffect, useRef, useState } from 'react';

import { useBridge } from '../bridge/BridgeContext';
import type { TimelineAsset, TimelineBus, TimelineTrack } from '../bridge/generated';
import { LANE_PRESETS, SNAP_OPTIONS, setLayoutPref, useLayoutPrefs } from '../state/layoutPrefs';
import { useLatest } from '../state/latestEvent';
import { useStores } from '../state/StoresContext';
import { ClipView } from './ClipView';
import { isMac, isTextEntry, trackDrag } from './dom';
import {
  clamp,
  clipEnd,
  findClip,
  instrumentOf,
  isAudioClip,
  kindLabel,
  MAX_TICKS,
  ticksPerBar,
  ticksPerBeat,
  timelineEnd,
  TrackKind,
} from './model';
import { useSelection } from './selection';
import { BusHeader } from './BusHeader';
import { TrackHeader } from './TrackHeader';

const MIN_PX_PER_BEAT = 6;
const MAX_PX_PER_BEAT = 96;
const HEADER_WIDTH = 228;
const MAX_BUSES = 8;
const NO_BUSES: readonly TimelineBus[] = [];
const NO_TRACKS: readonly TimelineTrack[] = [];

function useMeter() {
  const transport = useLatest(useStores().transportState);
  const numerator = transport?.numerator ?? 4;
  const denominator = transport?.denominator ?? 4;
  return {
    bar: ticksPerBar(numerator, denominator),
    beat: ticksPerBeat(denominator),
    bpm: transport?.bpm ?? 120,
  };
}

function AddTrackMenu({ tracks }: { tracks: readonly TimelineTrack[] }) {
  const bridge = useBridge();
  const [open, setOpen] = useState(false);
  const menuRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    if (!open) return;
    const close = (event: Event) => {
      if (
        event instanceof KeyboardEvent
          ? event.key === 'Escape'
          : !menuRef.current?.contains(event.target as Node)
      )
        setOpen(false);
    };
    window.addEventListener('pointerdown', close);
    window.addEventListener('keydown', close);
    return () => {
      window.removeEventListener('pointerdown', close);
      window.removeEventListener('keydown', close);
    };
  }, [open]);

  return (
    <div className="menu" ref={menuRef}>
      <button
        type="button"
        className="button"
        aria-haspopup="menu"
        aria-expanded={open}
        onClick={() => {
          setOpen((value) => !value);
        }}
      >
        + Track
      </button>
      {open ? (
        <div className="menu__list" role="menu">
          {[TrackKind.audio, TrackKind.synth, TrackKind.sampler, TrackKind.drums].map((kind) => {
            const exists = kind !== TrackKind.audio && tracks.some((t) => t.kind === kind);
            return (
              <button
                key={kind}
                type="button"
                role="menuitem"
                disabled={exists}
                title={exists ? 'This instrument already has a track' : undefined}
                onClick={() => {
                  bridge.send({ type: 'track.add', payload: { kind } });
                  setOpen(false);
                }}
              >
                {kindLabel(kind)}
              </button>
            );
          })}
        </div>
      ) : null}
    </div>
  );
}

function Ruler({ pxPerTick, totalTicks }: { pxPerTick: number; totalTicks: number }) {
  const bridge = useBridge();
  const transport = useLatest(useStores().transportState);
  const { bar, beat } = useMeter();
  const barPx = bar * pxPerTick;
  const labelEvery = [1, 2, 4, 8, 16, 32].find((n) => n * barPx >= 36) ?? 64;
  const bars = Math.ceil(totalTicks / bar);
  const loopShown = transport && transport.loopEnd > transport.loopStart;

  const ticksAt = (clientX: number, element: HTMLElement) =>
    Math.max(0, (clientX - element.getBoundingClientRect().left) / pxPerTick);

  return (
    <div
      className="ruler"
      title="Click to move the playhead; drag to set the loop"
      onPointerDown={(event) => {
        if (event.button !== 0) return;
        const element = event.currentTarget;
        const from = ticksAt(event.clientX, element);
        trackDrag(
          event,
          (_dx, _dy, move) => {
            const to = ticksAt(move.clientX, element);
            const start = Math.floor(Math.min(from, to) / bar) * bar;
            const end = Math.max(start + bar, Math.ceil(Math.max(from, to) / bar) * bar);
            bridge.send({
              type: 'transport.setLoop',
              payload: { enabled: true, start, end: Math.min(end, MAX_TICKS) },
            });
          },
          (moved) => {
            if (!moved)
              bridge.send({
                type: 'transport.seek',
                payload: { ticks: Math.round(from / beat) * beat },
              });
          },
        );
      }}
    >
      {loopShown ? (
        <span
          className="ruler__loop"
          data-enabled={transport.loopEnabled}
          style={{
            left: `${String(transport.loopStart * pxPerTick)}px`,
            width: `${String((transport.loopEnd - transport.loopStart) * pxPerTick)}px`,
          }}
        />
      ) : null}
      {Array.from({ length: Math.ceil(bars / labelEvery) }, (_, i) => i * labelEvery).map(
        (index) => (
          <span key={index} className="ruler__bar" style={{ left: `${String(index * barPx)}px` }}>
            {index + 1}
          </span>
        ),
      )}
    </div>
  );
}

function Playhead({
  pxPerTick,
  scroller,
}: {
  pxPerTick: number;
  scroller: React.RefObject<HTMLDivElement | null>;
}) {
  const stores = useStores();
  const position = useLatest(stores.transportPosition);
  const playing = useLatest(stores.transportState)?.playing ?? false;
  const x = Math.max(0, position?.ticks ?? 0) * pxPerTick;

  // Keep the playhead in view while playing.
  useEffect(() => {
    const element = scroller.current;
    if (!playing || !element) return;
    const visible = element.clientWidth - HEADER_WIDTH;
    if (x < element.scrollLeft || x > element.scrollLeft + visible - 24)
      element.scrollTo({ left: Math.max(0, x - 48) });
  }, [x, playing, scroller]);

  return (
    <div
      className="playhead"
      style={{ transform: `translateX(${String(x)}px)` }}
      aria-hidden="true"
    />
  );
}

function Lane({
  track,
  assets,
  pxPerTick,
}: {
  track: TimelineTrack;
  assets: readonly TimelineAsset[];
  pxPerTick: number;
}) {
  const bridge = useBridge();
  const stores = useStores();
  const selection = useSelection(stores.selection);
  const { bar, beat, bpm } = useMeter();
  const option = SNAP_OPTIONS[useLayoutPrefs().snap] ?? SNAP_OPTIONS[3];
  const grid = option.bar ? bar : option.beats * beat;

  const select = (clip: number | null) => {
    stores.selection.set({ track: track.id, clip, bus: null });
    const instrument = instrumentOf(track.kind);
    if (instrument !== null) bridge.send({ type: 'instrument.select', payload: { instrument } });
  };

  return (
    <div
      className="lane"
      data-lane-track={track.id}
      data-lane-kind={track.kind === TrackKind.audio ? 0 : 1}
      data-selected={selection.track === track.id}
      style={
        {
          '--beat-px': `${String(beat * pxPerTick)}px`,
          '--bar-px': `${String(bar * pxPerTick)}px`,
        } as React.CSSProperties
      }
      onPointerDown={(event) => {
        if (event.target === event.currentTarget) select(null);
      }}
      onDoubleClick={(event) => {
        if (event.target !== event.currentTarget) return;
        const x = event.clientX - event.currentTarget.getBoundingClientRect().left;
        const start = Math.floor(x / pxPerTick / bar) * bar;
        if (track.kind === TrackKind.audio)
          bridge.send({ type: 'track.importAudio', payload: { track: track.id, ticks: start } });
        else
          bridge.send({
            type: 'clip.create',
            payload: {
              track: track.id,
              start,
              length: track.kind === TrackKind.drums ? 4 * bar : bar,
            },
          });
      }}
      title={
        track.clips.length === 0
          ? track.kind === TrackKind.audio
            ? 'Double-click to import audio here'
            : 'Double-click to add a clip, or record'
          : undefined
      }
    >
      {track.clips.map((clip) => (
        <ClipView
          key={clip.id}
          clip={clip}
          track={track}
          asset={isAudioClip(clip) ? assets.find((a) => a.id === clip.asset) : undefined}
          pxPerTick={pxPerTick}
          grid={grid}
          bpm={bpm}
          selected={selection.clip === clip.id}
          onSelect={() => {
            select(clip.id);
          }}
        />
      ))}
    </div>
  );
}

/** Arrangement view (guide §14): tracks, clips, ruler and playhead. */
/** Below this lane height the track header drops its second row of controls. */
const COMPACT_BELOW = 64;

export function Timeline() {
  const bridge = useBridge();
  const stores = useStores();
  const timeline = useLatest(stores.timeline);
  const assets = useLatest(stores.timelineAssets)?.assets ?? [];
  const position = useLatest(stores.transportPosition);
  const selection = useSelection(stores.selection);
  const { bar, beat } = useMeter();
  const [pxPerBeat, setPxPerBeat] = useState(24);
  const scrollerRef = useRef<HTMLDivElement>(null);
  const { laneHeight, snap: snapIndex } = useLayoutPrefs();

  const tracks = timeline?.tracks ?? NO_TRACKS;
  const buses = timeline?.buses ?? NO_BUSES;
  const pxPerTick = pxPerBeat / beat;
  const totalTicks = Math.min(MAX_TICKS, Math.max(timelineEnd(tracks) + 16 * bar, 64 * bar));
  const playhead = Math.max(0, position?.ticks ?? 0);
  const selected = selection.clip !== null ? findClip(tracks, selection.clip) : null;
  const canSplit =
    selected !== null && playhead > selected.clip.start && playhead < clipEnd(selected.clip);

  // Forget a selection whose clip or track is gone (deleted, undone).
  useEffect(() => {
    const { track, clip, bus } = stores.selection.get();
    const trackGone = track !== null && !tracks.some((t) => t.id === track);
    const clipGone = clip !== null && findClip(tracks, clip) === null;
    const busGone = bus !== null && !buses.some((b) => b.id === bus);
    if (trackGone || clipGone || busGone)
      stores.selection.set({
        track: trackGone ? null : track,
        clip: null,
        bus: busGone ? null : bus,
      });
  }, [tracks, buses, stores.selection]);

  const actions = {
    split: () => {
      if (selected && canSplit)
        bridge.send({ type: 'clip.split', payload: { clip: selected.clip.id, ticks: playhead } });
    },
    duplicate: () => {
      if (selected) bridge.send({ type: 'clip.duplicate', payload: { clip: selected.clip.id } });
    },
    remove: () => {
      if (selected) bridge.send({ type: 'clip.remove', payload: { clip: selected.clip.id } });
    },
    // Keyboard editing: arrows move the selected clip by a beat, Shift+arrows change its length.
    nudge: (direction: number, resize: boolean) => {
      if (!selected) return;
      const { clip, track } = selected;
      if (resize) {
        const end = clamp(clipEnd(clip) + direction * beat, clip.start + beat, MAX_TICKS);
        bridge.send({
          type: 'clip.resize',
          payload: { clip: clip.id, edge: 1, ticks: end, gesture: 0 },
        });
      } else {
        const start = clamp(clip.start + direction * beat, 0, MAX_TICKS - clip.length);
        if (start !== clip.start)
          bridge.send({
            type: 'clip.move',
            payload: { clip: clip.id, track: track.id, start, gesture: 0 },
          });
      }
    },
    loop: () => {
      if (selected && !isAudioClip(selected.clip))
        bridge.send({
          type: 'clip.setLoop',
          payload: { clip: selected.clip.id, enabled: selected.clip.loopLength === 0 },
        });
    },
  };
  const actionsRef = useRef(actions);
  useEffect(() => {
    actionsRef.current = actions;
  });

  useEffect(() => {
    const onKeyDown = (event: KeyboardEvent) => {
      if (isTextEntry(event.target) || event.altKey) return;
      const mod = isMac ? event.metaKey : event.ctrlKey;
      const key = event.key.toLowerCase();
      if (!mod && (event.key === 'Backspace' || event.key === 'Delete'))
        actionsRef.current.remove();
      else if (mod && key === 'd') actionsRef.current.duplicate();
      else if (mod && key === 't') actionsRef.current.split();
      else if (
        !mod &&
        (event.key === 'ArrowLeft' || event.key === 'ArrowRight') &&
        !(event.target instanceof HTMLInputElement || event.target instanceof HTMLSelectElement)
      )
        actionsRef.current.nudge(event.key === 'ArrowLeft' ? -1 : 1, event.shiftKey);
      else if (!mod && event.key === 'Escape')
        stores.selection.set({ track: null, clip: null, bus: null });
      else return;
      event.preventDefault();
    };
    window.addEventListener('keydown', onKeyDown);
    return () => {
      window.removeEventListener('keydown', onKeyDown);
    };
  }, [stores.selection]);

  const zoom = (factor: number) => {
    setPxPerBeat((value) => clamp(Math.round(value * factor), MIN_PX_PER_BEAT, MAX_PX_PER_BEAT));
  };
  const shortcut = isMac ? '⌘' : 'Ctrl+';

  return (
    <section className="timeline" aria-label="Timeline">
      <div className="timeline__toolbar">
        <AddTrackMenu tracks={tracks} />
        <button
          type="button"
          className="button"
          disabled={buses.length >= MAX_BUSES}
          title="Add an effect bus: a shared reverb or delay that tracks send to"
          onClick={() => {
            bridge.send({ type: 'bus.add', payload: {} });
          }}
        >
          + Bus
        </button>
        <span className="timeline__divider" aria-hidden="true" />
        <button
          type="button"
          className="button"
          disabled={!canSplit}
          onClick={actions.split}
          title={`Split at playhead (${shortcut}T)`}
        >
          Split
        </button>
        <button
          type="button"
          className="button"
          disabled={!selected}
          onClick={actions.duplicate}
          title={`Duplicate (${shortcut}D)`}
        >
          Duplicate
        </button>
        <button
          type="button"
          className="toggle"
          disabled={!selected || isAudioClip(selected.clip)}
          aria-pressed={selected !== null && selected.clip.loopLength > 0}
          onClick={actions.loop}
          title="Repeat the clip's content when you make it longer"
        >
          Loop clip
        </button>
        <button
          type="button"
          className="button"
          disabled={!selected}
          onClick={actions.remove}
          title="Delete (⌫)"
        >
          Delete
        </button>
        <button
          type="button"
          className="button"
          disabled={!selected || !isAudioClip(selected.clip)}
          onClick={() => {
            if (selected)
              bridge.send({ type: 'accompaniment.suggest', payload: { clip: selected.clip.id } });
          }}
          title="Listen to the selected audio and suggest a beat to play with it"
        >
          Try a beat
        </button>
        <span className="timeline__spacer" />
        <label className="timeline__snap">
          Snap{' '}
          <select
            value={snapIndex}
            aria-label="Snap to grid"
            onChange={(event) => {
              setLayoutPref('snap', Number(event.target.value));
            }}
          >
            {SNAP_OPTIONS.map((option, index) => (
              <option key={option.label} value={index}>
                {option.label}
              </option>
            ))}
          </select>
        </label>
        <div className="timeline__sizes" role="group" aria-label="Track height">
          {LANE_PRESETS.map((preset) => (
            <button
              key={preset.label}
              type="button"
              className="toggle"
              aria-pressed={laneHeight === preset.height}
              aria-label={`${preset.name} tracks`}
              title={`${preset.name} tracks`}
              onClick={() => {
                setLayoutPref('laneHeight', preset.height);
              }}
            >
              {preset.label}
            </button>
          ))}
        </div>
        <button
          type="button"
          className="button"
          aria-label="Zoom out"
          onClick={() => {
            zoom(1 / 1.5);
          }}
        >
          −
        </button>
        <button
          type="button"
          className="button"
          aria-label="Zoom in"
          onClick={() => {
            zoom(1.5);
          }}
        >
          +
        </button>
      </div>

      <div
        className="timeline__scroll"
        ref={scrollerRef}
        onWheel={(event) => {
          if (event.ctrlKey || event.metaKey) zoom(event.deltaY < 0 ? 1.15 : 1 / 1.15);
        }}
      >
        <div
          className="timeline__grid"
          data-compact={laneHeight < COMPACT_BELOW}
          style={
            {
              '--header-width': `${String(HEADER_WIDTH)}px`,
              '--lanes-width': `${String(totalTicks * pxPerTick)}px`,
              '--lane-height': `${String(laneHeight)}px`,
            } as React.CSSProperties
          }
        >
          <div className="timeline__corner">Bar</div>
          <Ruler pxPerTick={pxPerTick} totalTicks={totalTicks} />
          {tracks.map((track) => (
            <TimelineRow
              key={track.id}
              track={track}
              assets={assets}
              pxPerTick={pxPerTick}
              selected={selection.track === track.id}
              onImport={() => {
                bridge.send({
                  type: 'track.importAudio',
                  payload: { track: track.id, ticks: Math.floor(playhead / bar) * bar },
                });
              }}
              onSelect={() => {
                stores.selection.set({ track: track.id, clip: null, bus: null });
                const instrument = instrumentOf(track.kind);
                if (instrument !== null)
                  bridge.send({ type: 'instrument.select', payload: { instrument } });
              }}
            />
          ))}
          {buses.map((bus) => (
            <BusRow key={`bus-${String(bus.id)}`} bus={bus} selected={selection.bus === bus.id} />
          ))}
          {tracks.length === 0 ? (
            <p className="timeline__empty">
              Add a track with <strong>+ Track</strong>, press <strong>●</strong> to record what you
              play, or click a drum step to start a beat.
            </p>
          ) : null}
          <div className="playhead-layer">
            <Playhead pxPerTick={pxPerTick} scroller={scrollerRef} />
          </div>
        </div>
      </div>
    </section>
  );
}

function TimelineRow({
  track,
  assets,
  pxPerTick,
  selected,
  onSelect,
  onImport,
}: {
  track: TimelineTrack;
  assets: readonly TimelineAsset[];
  pxPerTick: number;
  selected: boolean;
  onSelect: () => void;
  onImport: () => void;
}) {
  return (
    <>
      <TrackHeader track={track} selected={selected} onSelect={onSelect} onImport={onImport} />
      <Lane track={track} assets={assets} pxPerTick={pxPerTick} />
    </>
  );
}

function BusRow({ bus, selected }: { bus: TimelineBus; selected: boolean }) {
  const stores = useStores();
  return (
    <>
      <BusHeader
        bus={bus}
        selected={selected}
        onSelect={() => {
          stores.selection.set({ track: null, clip: null, bus: bus.id });
        }}
      />
      <div className="lane lane--bus" data-selected={selected}>
        <span className="lane__note">Effect bus: tracks send to it from their effects panel</span>
      </div>
    </>
  );
}
