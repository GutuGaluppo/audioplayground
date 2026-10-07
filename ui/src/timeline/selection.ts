import { useSyncExternalStore } from 'react';

export interface Selection {
  readonly track: number | null;
  readonly clip: number | null;
  /** An effect bus selected instead of a track (its effects show in the effects panel). */
  readonly bus: number | null;
}

export interface SelectionStore {
  readonly subscribe: (listener: () => void) => () => void;
  readonly get: () => Selection;
  readonly set: (next: Selection) => void;
}

/** What the user has selected in the timeline. UI state only: never part of the project. */
export function createSelectionStore(): SelectionStore {
  let value: Selection = { track: null, clip: null, bus: null };
  const listeners = new Set<() => void>();
  return {
    subscribe: (listener) => {
      listeners.add(listener);
      return () => listeners.delete(listener);
    },
    get: () => value,
    set: (next) => {
      if (next.track === value.track && next.clip === value.clip && next.bus === value.bus) return;
      value = next;
      listeners.forEach((listener) => {
        listener();
      });
    },
  };
}

export function useSelection(store: SelectionStore): Selection {
  return useSyncExternalStore(store.subscribe, store.get);
}
