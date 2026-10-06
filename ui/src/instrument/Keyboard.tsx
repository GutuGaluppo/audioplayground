import { useCallback, useEffect, useRef, useState } from 'react';

import { useBridge } from '../bridge/BridgeContext';
import { useLatest } from '../state/latestEvent';
import { useStores } from '../state/StoresContext';
import {
  isBlackKey,
  MAX_BASE_NOTE,
  MIN_BASE_NOTE,
  noteForKey,
  noteName,
  OCTAVE_DOWN_CODE,
  OCTAVE_UP_CODE,
  semitoneLabelForCode,
  VELOCITY_DOWN_CODE,
  VELOCITY_UP_CODE,
} from './qwerty';

const KEYS_SHOWN = 25; // two octaves + top C
const VELOCITY_STEPS = [0.25, 0.4, 0.55, 0.7, 0.85, 1] as const;

function isTextEntry(target: EventTarget | null): boolean {
  return (
    target instanceof HTMLElement &&
    (target.isContentEditable ||
      target.tagName === 'TEXTAREA' ||
      (target instanceof HTMLInputElement && target.type !== 'range'))
  );
}

/**
 * Playable keyboard: on-screen keys (mouse/touch) and the computer keyboard. All notes are
 * released when the window loses focus, so a note can never get stuck.
 */
/** The piano keyboard for melodic instruments; the drums use their own pads instead. */
export function Keyboard() {
  const instrument = useLatest(useStores().instrument);
  return instrument?.instrument === 2 ? null : <PianoKeyboard />;
}

function PianoKeyboard() {
  const bridge = useBridge();
  const [baseNote, setBaseNote] = useState(48); // C3
  const [velocityIndex, setVelocityIndex] = useState(4);
  const [held, setHeld] = useState<ReadonlySet<number>>(new Set());
  const keyToNote = useRef(new Map<string, number>()); // which note each physical key started
  const pointerNote = useRef<number | null>(null);

  const velocity = VELOCITY_STEPS[velocityIndex] ?? 0.85;

  const press = useCallback(
    (note: number) => {
      bridge.send({ type: 'note.on', payload: { note, velocity } });
      setHeld((current) => new Set(current).add(note));
    },
    [bridge, velocity],
  );

  const release = useCallback(
    (note: number) => {
      bridge.send({ type: 'note.off', payload: { note } });
      setHeld((current) => {
        const next = new Set(current);
        next.delete(note);
        return next;
      });
    },
    [bridge],
  );

  const releaseAll = useCallback(() => {
    keyToNote.current.clear();
    pointerNote.current = null;
    setHeld(new Set());
    bridge.send({ type: 'note.allOff', payload: {} });
  }, [bridge]);

  useEffect(() => {
    const onKeyDown = (event: KeyboardEvent) => {
      if (event.metaKey || event.ctrlKey || event.altKey || isTextEntry(event.target)) return;
      if (event.code === OCTAVE_DOWN_CODE || event.code === OCTAVE_UP_CODE) {
        if (event.shiftKey) return; // Shift+letter is a command (e.g. Shift+C: capture)
        if (!event.repeat)
          setBaseNote((n) =>
            Math.min(
              MAX_BASE_NOTE,
              Math.max(MIN_BASE_NOTE, n + (event.code === OCTAVE_UP_CODE ? 12 : -12)),
            ),
          );
        return;
      }
      if (event.code === VELOCITY_DOWN_CODE || event.code === VELOCITY_UP_CODE) {
        if (event.shiftKey) return;
        if (!event.repeat)
          setVelocityIndex((i) =>
            Math.min(
              VELOCITY_STEPS.length - 1,
              Math.max(0, i + (event.code === VELOCITY_UP_CODE ? 1 : -1)),
            ),
          );
        return;
      }
      const note = noteForKey(event.code, baseNote);
      if (note === null) return;
      event.preventDefault();
      if (event.repeat || keyToNote.current.has(event.code)) return;
      keyToNote.current.set(event.code, note);
      press(note);
    };
    const onKeyUp = (event: KeyboardEvent) => {
      const note = keyToNote.current.get(event.code);
      if (note === undefined) return;
      keyToNote.current.delete(event.code);
      release(note);
    };
    const onVisibility = () => {
      if (document.hidden) releaseAll();
    };

    window.addEventListener('keydown', onKeyDown);
    window.addEventListener('keyup', onKeyUp);
    window.addEventListener('blur', releaseAll);
    document.addEventListener('visibilitychange', onVisibility);
    return () => {
      window.removeEventListener('keydown', onKeyDown);
      window.removeEventListener('keyup', onKeyUp);
      window.removeEventListener('blur', releaseAll);
      document.removeEventListener('visibilitychange', onVisibility);
    };
  }, [baseNote, press, release, releaseAll]);

  const notes = Array.from({ length: KEYS_SHOWN }, (_, i) => baseNote + i);
  const whiteKeys = notes.filter((n) => !isBlackKey(n));

  return (
    <section className="keyboard" aria-label="Keyboard">
      <div className="keyboard__info">
        <span>
          Octave <strong>{noteName(baseNote)}</strong> <kbd>Z</kbd>
          <kbd>X</kbd>
        </span>
        <span>
          Velocity <strong>{Math.round(velocity * 127)}</strong> <kbd>C</kbd>
          <kbd>V</kbd>
        </span>
      </div>
      <div
        className="keyboard__keys"
        onPointerLeave={() => {
          if (pointerNote.current !== null) {
            release(pointerNote.current);
            pointerNote.current = null;
          }
        }}
      >
        {notes.map((note) => {
          const black = isBlackKey(note);
          const whiteIndex = whiteKeys.indexOf(black ? note - 1 : note);
          const label = semitoneLabelForCode(note - baseNote);
          return (
            <button
              key={note}
              type="button"
              tabIndex={-1}
              className={black ? 'key key--black' : 'key key--white'}
              data-held={held.has(note)}
              aria-label={noteName(note)}
              style={{ ['--key-index' as string]: black ? whiteIndex + 1 : whiteIndex }}
              onPointerDown={(event) => {
                event.currentTarget.releasePointerCapture(event.pointerId);
                pointerNote.current = note;
                press(note);
              }}
              onPointerUp={() => {
                if (pointerNote.current === note) {
                  pointerNote.current = null;
                  release(note);
                }
              }}
              onPointerEnter={(event) => {
                // Glide across keys while the button is held.
                if (
                  event.buttons === 1 &&
                  pointerNote.current !== null &&
                  pointerNote.current !== note
                ) {
                  release(pointerNote.current);
                  pointerNote.current = note;
                  press(note);
                }
              }}
            >
              {label ? <span className="key__label">{label}</span> : null}
            </button>
          );
        })}
      </div>
    </section>
  );
}
