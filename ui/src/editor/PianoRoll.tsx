import { useEffect, useLayoutEffect, useRef, useState } from 'react';

import { useBridge } from '../bridge/BridgeContext';
import type { TimelineClip, TimelineNote, TimelineTrack } from '../bridge/generated';
import { beginGesture } from '../params/gestures';
import { useLatest } from '../state/latestEvent';
import { useStores } from '../state/StoresContext';
import { isTextEntry, trackDrag } from '../timeline/dom';
import {
  clipEnd,
  contentLength,
  contentTimeAt,
  kindLabel,
  noteName,
  PPQ,
  snap,
  ticksPerBar,
} from '../timeline/model';

const LOWEST = 24; // C1
const HIGHEST = 108; // C8
const ROW = 14;
const MIN_PX_PER_TICK = 40 / PPQ; // the grid widens to fill the editor, never narrower than this
const GRID = PPQ / 4; // sixteenth notes
const KEYS_WIDTH = 44;

const isBlack = (pitch: number) => [1, 3, 6, 8, 10].includes(pitch % 12);
const rowOf = (pitch: number) => HIGHEST - pitch;

interface NoteKey {
  start: number;
  pitch: number;
}

/** Edits the notes of the selected synth or sampler clip. Coordinates are content time. */
export function PianoRoll({ track, clip }: { track: TimelineTrack; clip: TimelineClip }) {
  const bridge = useBridge();
  const stores = useStores();
  const transport = useLatest(stores.transportState);
  const position = useLatest(stores.transportPosition);
  const [selected, setSelected] = useState<NoteKey | null>(null);
  const [noteLength, setNoteLength] = useState(PPQ / 2);
  const scrollRef = useRef<HTMLDivElement>(null);

  const bar = ticksPerBar(transport?.numerator ?? 4, transport?.denominator ?? 4);
  const length = Math.max(bar, Math.ceil(contentLength(clip) / bar) * bar);
  const minWidth = length * MIN_PX_PER_TICK;
  const percent = (ticks: number) => `${String((ticks / length) * 100)}%`;

  // Open centred on the notes (or middle C).
  useLayoutEffect(() => {
    const element = scrollRef.current;
    if (!element) return;
    const pitches = clip.notes.map((n) => n.pitch);
    const centre = pitches.length > 0 ? (Math.min(...pitches) + Math.max(...pitches)) / 2 : 60;
    element.scrollTop = rowOf(centre) * ROW - element.clientHeight / 2;
    // Only when another clip is opened, not on every edit.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [clip.id]);

  const audition = (pitch: number) => {
    bridge.send({ type: 'note.on', payload: { note: pitch, velocity: 0.8 } });
    window.setTimeout(() => {
      bridge.send({ type: 'note.off', payload: { note: pitch } });
    }, 160);
  };

  const remove = (note: NoteKey) => {
    bridge.send({
      type: 'clip.removeNote',
      payload: { clip: clip.id, start: note.start, pitch: note.pitch },
    });
    setSelected(null);
  };

  // Delete the selected note before the timeline would delete the whole clip.
  const selectedRef = useRef(selected);
  useEffect(() => {
    selectedRef.current = selected;
  });
  useEffect(() => {
    const onKeyDown = (event: KeyboardEvent) => {
      const note = selectedRef.current;
      if (
        !note ||
        isTextEntry(event.target) ||
        (event.key !== 'Backspace' && event.key !== 'Delete')
      )
        return;
      event.preventDefault();
      event.stopImmediatePropagation();
      bridge.send({
        type: 'clip.removeNote',
        payload: { clip: clip.id, start: note.start, pitch: note.pitch },
      });
      setSelected(null);
    };
    window.addEventListener('keydown', onKeyDown, true);
    return () => {
      window.removeEventListener('keydown', onKeyDown, true);
    };
  }, [bridge, clip.id]);

  const gridRef = useRef<HTMLDivElement>(null);
  const ticksPerPx = () =>
    length / Math.max(1, gridRef.current?.getBoundingClientRect().width ?? minWidth);

  const dragNote = (event: React.PointerEvent, note: TimelineNote, mode: 'move' | 'resize') => {
    if (event.button !== 0) return;
    event.preventDefault();
    event.stopPropagation();
    const scale = ticksPerPx();
    const gesture = beginGesture();
    let current: NoteKey = { start: note.start, pitch: note.pitch };
    let currentLength = note.length;
    setSelected(current);
    if (mode === 'move') audition(note.pitch);

    trackDrag(event, (dx, dy, move) => {
      const grid = move.altKey ? 1 : GRID;
      const next =
        mode === 'move'
          ? {
              start: Math.max(0, snap(note.start + dx * scale, grid)),
              pitch: Math.min(127, Math.max(0, note.pitch - Math.round(dy / ROW))),
              length: note.length,
            }
          : {
              start: note.start,
              pitch: note.pitch,
              length: Math.max(
                grid,
                snap(note.start + note.length + dx * scale, grid) - note.start,
              ),
            };
      if (
        next.start === current.start &&
        next.pitch === current.pitch &&
        next.length === currentLength
      )
        return;
      if (next.pitch !== current.pitch) audition(next.pitch);
      bridge.send({
        type: 'clip.editNote',
        payload: {
          clip: clip.id,
          fromStart: current.start,
          fromPitch: current.pitch,
          ...next,
          gesture,
        },
      });
      current = { start: next.start, pitch: next.pitch };
      currentLength = next.length;
      setSelected(current);
      setNoteLength(next.length);
    });
  };

  const addNote = (event: React.PointerEvent<HTMLDivElement>) => {
    if (event.button !== 0 || event.target !== event.currentTarget) return;
    const rect = event.currentTarget.getBoundingClientRect();
    const gridWidth = rect.width > 0 ? rect.width : minWidth;
    const start = Math.max(
      0,
      Math.floor(((event.clientX - rect.left) * length) / gridWidth / GRID) * GRID,
    );
    const pitch = HIGHEST - Math.floor((event.clientY - rect.top) / ROW);
    if (pitch < LOWEST || pitch > HIGHEST) return;
    bridge.send({
      type: 'clip.addNote',
      payload: { clip: clip.id, start, length: noteLength, pitch, velocity: 0.8 },
    });
    setSelected({ start, pitch });
    audition(pitch);
  };

  // Where the clip's audible window lies in content time (everything when it loops).
  const visibleFrom = clip.loopLength > 0 ? 0 : clip.contentOffset;
  const visibleTo = clip.loopLength > 0 ? clip.loopLength : clip.contentOffset + clip.length;
  const ticks = position?.ticks ?? 0;
  const playing = (transport?.playing ?? false) && ticks >= clip.start && ticks < clipEnd(clip);
  const playheadAt = playing ? contentTimeAt(clip, ticks) : null;

  const rows = Array.from({ length: HIGHEST - LOWEST + 1 }, (_, i) => HIGHEST - i);

  return (
    <section className="card piano-roll" aria-label={`${kindLabel(track.kind)} notes`}>
      <div className="sampler__header">
        <h2 className="card__title">
          {kindLabel(track.kind)} notes
          <span className="drums__clip"> · click to add, drag to move, double-click to delete</span>
        </h2>
        <span className="piano-roll__count">{clip.notes.length} notes</span>
      </div>
      <div className="piano-roll__scroll" ref={scrollRef}>
        <div
          className="piano-roll__body"
          style={{ minWidth: `${String(KEYS_WIDTH + minWidth)}px` }}
        >
          <div className="piano-roll__keys" aria-hidden="true">
            {rows.map((pitch) => (
              <div
                key={pitch}
                className="piano-roll__key"
                data-black={isBlack(pitch)}
                style={{ height: `${String(ROW)}px` }}
              >
                {pitch % 12 === 0 ? noteName(pitch) : ''}
              </div>
            ))}
          </div>
          <div
            ref={gridRef}
            className="piano-roll__grid"
            role="grid"
            aria-label="Notes"
            style={
              {
                minWidth: `${String(minWidth)}px`,
                height: `${String(rows.length * ROW)}px`,
                '--row': `${String(ROW)}px`,
                '--beat-px': percent(PPQ),
                '--bar-px': percent(bar),
              } as React.CSSProperties
            }
            onPointerDown={addNote}
          >
            {visibleFrom > 0 ? (
              <span
                className="piano-roll__outside"
                style={{ left: 0, width: percent(visibleFrom) }}
              />
            ) : null}
            {visibleTo < length ? (
              <span
                className="piano-roll__outside"
                style={{ left: percent(visibleTo), right: 0 }}
              />
            ) : null}
            {clip.notes.map((note) => {
              const isSelected = selected?.start === note.start && selected.pitch === note.pitch;
              return (
                <div
                  key={`${String(note.start)}:${String(note.pitch)}`}
                  className="piano-roll__note"
                  role="gridcell"
                  aria-selected={isSelected}
                  aria-label={`${noteName(note.pitch)} at ${String(note.start / PPQ + 1)}`}
                  style={{
                    left: percent(note.start),
                    top: `${String(rowOf(note.pitch) * ROW)}px`,
                    width: percent(note.length),
                    minWidth: '4px',
                    height: `${String(ROW)}px`,
                    opacity: 0.35 + 0.65 * note.velocity,
                  }}
                  onPointerDown={(event) => {
                    dragNote(event, note, 'move');
                  }}
                  onDoubleClick={() => {
                    remove(note);
                  }}
                >
                  <span
                    className="piano-roll__resize"
                    onPointerDown={(event) => {
                      dragNote(event, note, 'resize');
                    }}
                  />
                </div>
              );
            })}
            {playheadAt !== null ? (
              <span className="piano-roll__playhead" style={{ left: percent(playheadAt) }} />
            ) : null}
          </div>
        </div>
      </div>
    </section>
  );
}
