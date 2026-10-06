import { useState } from 'react';

const STORAGE_KEY = 'ap.quickStart.dismissed';

function wasDismissed(): boolean {
  try {
    return window.localStorage.getItem(STORAGE_KEY) === '1';
  } catch {
    return false; // storage unavailable: show the tips again, nothing else depends on it
  }
}

/**
 * First-use tips (plan §3.7: contextual help, never a blocking tutorial). Shown until dismissed;
 * remembered in this UI's local storage only.
 */
export function QuickStart() {
  const [dismissed, setDismissed] = useState(wasDismissed);
  if (dismissed) return null;

  return (
    <aside className="quick-start" aria-label="Quick start">
      <h2 className="quick-start__title">Quick start</h2>
      <ul>
        <li>
          <kbd>Space</kbd> plays the beat
        </li>
        <li>
          <kbd>A</kbd>–<kbd>L</kbd> play notes · <kbd>Z</kbd>/<kbd>X</kbd> change octave
        </li>
        <li>
          <kbd>Shift</kbd>+<kbd>R</kbd> records what you play · arm an audio track (●) to record the
          mic
        </li>
        <li>
          <kbd>Shift</kbd>+<kbd>C</kbd> keeps what you just played, even without recording
        </li>
        <li>Select a track to add effects · Export… makes a WAV</li>
      </ul>
      <button
        type="button"
        className="button button--quiet"
        onClick={() => {
          try {
            window.localStorage.setItem(STORAGE_KEY, '1');
          } catch {
            // not remembered; fine
          }
          setDismissed(true);
        }}
      >
        Got it
      </button>
    </aside>
  );
}
