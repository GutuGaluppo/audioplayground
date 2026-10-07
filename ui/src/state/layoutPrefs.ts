import { useSyncExternalStore } from 'react';

/**
 * How big the user made each zone of the window. UI-only preference (ADR-003): kept in this UI's
 * local storage, never in the project and never sent to the core.
 */
export interface LayoutPrefs {
  laneHeight: number;
  bottomHeight: number;
  instrumentWidth: number;
  effectsWidth: number;
  keyboardHeight: number;
}

export const LAYOUT_DEFAULTS: Readonly<LayoutPrefs> = {
  laneHeight: 76,
  bottomHeight: 300,
  instrumentWidth: 520,
  effectsWidth: 520,
  keyboardHeight: 96,
};

export const LAYOUT_LIMITS: Readonly<Record<keyof LayoutPrefs, readonly [number, number]>> = {
  laneHeight: [40, 240],
  bottomHeight: [120, 700],
  instrumentWidth: [320, 1100],
  effectsWidth: [280, 1100],
  keyboardHeight: [64, 220],
};

/** Preset lane heights. */
export const LANE_PRESETS = [
  { label: 'S', name: 'Small', height: 48 },
  { label: 'M', name: 'Medium', height: 76 },
  { label: 'L', name: 'Large', height: 120 },
] as const;

const STORAGE_KEY = 'ap.layout';

function clamp(key: keyof LayoutPrefs, value: number): number {
  const [min, max] = LAYOUT_LIMITS[key];
  return Math.round(Math.min(max, Math.max(min, value)));
}

function load(): LayoutPrefs {
  const prefs = { ...LAYOUT_DEFAULTS };
  try {
    const raw: unknown = JSON.parse(window.localStorage.getItem(STORAGE_KEY) ?? '{}');
    if (typeof raw === 'object' && raw !== null) {
      for (const key of Object.keys(prefs) as (keyof LayoutPrefs)[]) {
        const value = (raw as Record<string, unknown>)[key];
        if (typeof value === 'number' && Number.isFinite(value)) prefs[key] = clamp(key, value);
      }
    }
  } catch {
    // storage unavailable or corrupt: use the defaults, nothing else depends on it
  }
  return prefs;
}

let current = load();
const listeners = new Set<() => void>();

function publish(next: LayoutPrefs): void {
  current = next;
  try {
    window.localStorage.setItem(STORAGE_KEY, JSON.stringify(next));
  } catch {
    // not persisted; the layout still applies for this session
  }
  listeners.forEach((listener) => {
    listener();
  });
}

export function setLayoutPref(key: keyof LayoutPrefs, value: number): void {
  const next = clamp(key, value);
  if (next !== current[key]) publish({ ...current, [key]: next });
}

export function resetLayoutPrefs(): void {
  publish({ ...LAYOUT_DEFAULTS });
}

export function getLayoutPrefs(): LayoutPrefs {
  return current;
}

/** Test helper: re-read storage (the module state outlives a test's localStorage reset). */
export function reloadLayoutPrefs(): void {
  current = load();
  listeners.forEach((listener) => {
    listener();
  });
}

function subscribe(listener: () => void): () => void {
  listeners.add(listener);
  return () => {
    listeners.delete(listener);
  };
}

export function useLayoutPrefs(): LayoutPrefs {
  return useSyncExternalStore(subscribe, getLayoutPrefs);
}
