import { useEffect, useRef } from 'react';

import { useLayoutPrefs } from '../state/layoutPrefs';

import { useBridge } from '../bridge/BridgeContext';
import type { TimelineAsset, TimelineClip, TimelineTrack } from '../bridge/generated';
import { beginGesture } from '../params/gestures';
import { useKeyed } from '../state/keyedEvent';
import { useStores } from '../state/StoresContext';
import { trackDrag } from './dom';
import { clipEnd, isAudioClip, kindLabel, MAX_TICKS, noteSpans, PPQ, snap } from './model';
import { decodePeaks, peakIn } from './peaks';

// Canvas backing stores are limited in size; wider clips stretch the drawing.
const MAX_CANVAS_WIDTH = 8192;

interface ClipViewProps {
  clip: TimelineClip;
  track: TimelineTrack;
  asset: TimelineAsset | undefined;
  pxPerTick: number;
  grid: number;
  bpm: number;
  selected: boolean;
  onSelect: () => void;
}

type DragMode = 'move' | 'left' | 'right';

/** The track lane under a screen point, if it can hold clips of this kind. */
function laneAt(x: number, y: number, audio: boolean): number | null {
  const element =
    typeof document.elementFromPoint === 'function' ? document.elementFromPoint(x, y) : null;
  const lane = element?.closest<HTMLElement>('[data-lane-track]');
  if (!lane) return null;
  const isAudioLane = lane.dataset['laneKind'] === '0';
  return isAudioLane === audio ? Number(lane.dataset['laneTrack']) : null;
}

function AudioWaveform({
  clip,
  asset,
  bpm,
  width,
}: {
  clip: TimelineClip;
  asset: TimelineAsset;
  bpm: number;
  width: number;
}) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const { laneHeight } = useLayoutPrefs();
  const peaksEvent = useKeyed(useStores().peaks, asset.id);
  const peaks = peaksEvent ? decodePeaks(peaksEvent) : null;

  useEffect(() => {
    const canvas = canvasRef.current;
    const context = (() => {
      try {
        return canvas?.getContext('2d') ?? null;
      } catch {
        return null; // no canvas support (tests)
      }
    })();
    if (!canvas || !context || asset.durationSeconds <= 0) return;
    if (!peaks && asset.overview.length === 0) return;

    const ratio = window.devicePixelRatio || 1;
    const w = Math.min(MAX_CANVAS_WIDTH, Math.max(1, Math.round(width)));
    const h = canvas.clientHeight || 32;
    canvas.width = Math.round(w * ratio);
    canvas.height = Math.round(h * ratio);
    context.scale(ratio, ratio);
    context.clearRect(0, 0, w, h);
    context.fillStyle = getComputedStyle(canvas).getPropertyValue('--clip-ink').trim() || '#c9d4ff';

    // The clip shows [sourceOffset, sourceOffset + its length in seconds] of the file. Each pixel
    // column draws the loudest peak of the audio under it (detailed peaks once they arrive; the
    // coarse overview until then).
    const seconds = (clip.length * 60) / (bpm * PPQ);
    const perPixel = seconds / w;
    const points = asset.overview.length;
    const middle = h / 2;
    for (let x = 0; x < w; x++) {
      const from = clip.sourceOffsetSeconds + x * perPixel;
      if (from >= asset.durationSeconds) break;
      const peak = peaks
        ? peakIn(peaks, from, from + perPixel)
        : (asset.overview[
            Math.min(
              points - 1,
              Math.floor(((from + perPixel / 2) / asset.durationSeconds) * points),
            )
          ] ?? 0);
      if (peak <= 0) continue;
      const bar = Math.max(1, peak * (h - 2));
      context.fillRect(x, middle - bar / 2, 1, bar);
    }
  }, [clip.length, clip.sourceOffsetSeconds, asset, peaks, bpm, width, laneHeight]);

  return <canvas ref={canvasRef} className="clip__waveform" />;
}

function NotePreview({ clip, pxPerTick }: { clip: TimelineClip; pxPerTick: number }) {
  const spans = noteSpans(clip);
  if (spans.length === 0) return null;
  const pitches = spans.map((s) => s.pitch);
  const low = Math.min(...pitches);
  const high = Math.max(low + 11, Math.max(...pitches));
  const rows = high - low + 1;
  return (
    <svg className="clip__notes" aria-hidden="true" preserveAspectRatio="none">
      {spans.map((span, index) => (
        <rect
          key={index}
          x={(span.start - clip.start) * pxPerTick}
          y={`${String(((high - span.pitch) / rows) * 100)}%`}
          width={Math.max(2, (span.end - span.start) * pxPerTick - 1)}
          height={`${String(Math.max(100 / rows, 6))}%`}
          rx={1}
        />
      ))}
    </svg>
  );
}

export function ClipView({
  clip,
  track,
  asset,
  pxPerTick,
  grid,
  bpm,
  selected,
  onSelect,
}: ClipViewProps) {
  const bridge = useBridge();
  const audio = isAudioClip(clip);
  const width = clip.length * pxPerTick;
  const name = audio ? (asset?.name ?? 'Audio') : kindLabel(track.kind);

  const startDrag = (event: React.PointerEvent, mode: DragMode) => {
    if (event.button !== 0) return;
    event.preventDefault();
    event.stopPropagation();
    onSelect();

    const gesture = beginGesture();
    const original = { start: clip.start, end: clipEnd(clip), track: track.id };
    let last = { ...original };

    trackDrag(event, (dx, _dy, move) => {
      const ticks = dx / pxPerTick;
      const g = move.altKey ? 1 : grid; // Alt/Option: no snapping
      if (mode === 'move') {
        const start = Math.max(
          0,
          Math.min(MAX_TICKS - clip.length, snap(original.start + ticks, g)),
        );
        const target = laneAt(move.clientX, move.clientY, audio) ?? last.track;
        if (start === last.start && target === last.track) return;
        last = { ...last, start, track: target };
        bridge.send({
          type: 'clip.move',
          payload: { clip: clip.id, track: target, start, gesture },
        });
      } else if (mode === 'left') {
        const start = Math.max(0, snap(original.start + ticks, g));
        if (start === last.start) return;
        last = { ...last, start };
        bridge.send({
          type: 'clip.resize',
          payload: { clip: clip.id, edge: 0, ticks: start, gesture },
        });
      } else {
        const end = Math.min(MAX_TICKS, Math.max(0, snap(original.end + ticks, g)));
        if (end === last.end) return;
        last = { ...last, end };
        bridge.send({
          type: 'clip.resize',
          payload: { clip: clip.id, edge: 1, ticks: end, gesture },
        });
      }
    });
  };

  const loopMarks: number[] = [];
  if (clip.loopLength > 0) {
    for (let k = 1; loopMarks.length < 256; k++) {
      const at = k * clip.loopLength - clip.contentOffset;
      if (at >= clip.length) break;
      if (at > 0) loopMarks.push(at * pxPerTick);
    }
  }

  return (
    <div
      className="clip"
      data-kind={track.kind}
      data-selected={selected}
      data-missing={asset?.missing ?? false}
      style={{
        left: `${String(clip.start * pxPerTick)}px`,
        width: `${String(Math.max(4, width))}px`,
      }}
      role="button"
      tabIndex={0}
      aria-pressed={selected}
      aria-label={`${name} clip`}
      onPointerDown={(event) => {
        startDrag(event, 'move');
      }}
      onFocus={onSelect}
    >
      <span className="clip__name">
        {name}
        {clip.loopLength > 0 ? <span aria-label="looped"> ⟳</span> : null}
        {asset?.missing ? ' — missing' : asset?.loading ? ' — loading…' : ''}
      </span>
      {asset?.missing ? (
        <button
          type="button"
          className="clip__locate"
          aria-label={`Locate the missing file for ${name}`}
          title="Choose the file this clip should play"
          onPointerDown={(event) => {
            event.stopPropagation(); // not a drag
          }}
          onClick={(event) => {
            event.stopPropagation();
            bridge.send({ type: 'asset.locate', payload: { asset: asset.id } });
          }}
        >
          Locate…
        </button>
      ) : null}
      {audio && asset?.loaded ? (
        <AudioWaveform clip={clip} asset={asset} bpm={bpm} width={width} />
      ) : (
        <NotePreview clip={clip} pxPerTick={pxPerTick} />
      )}
      {loopMarks.map((x) => (
        <span
          key={x}
          className="clip__loop"
          style={{ left: `${String(x)}px` }}
          aria-hidden="true"
        />
      ))}
      <span
        className="clip__handle clip__handle--left"
        aria-hidden="true"
        onPointerDown={(event) => {
          startDrag(event, 'left');
        }}
      />
      <span
        className="clip__handle clip__handle--right"
        aria-hidden="true"
        onPointerDown={(event) => {
          startDrag(event, 'right');
        }}
      />
    </div>
  );
}
