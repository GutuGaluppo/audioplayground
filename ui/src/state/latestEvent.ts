import { useSyncExternalStore } from 'react';

import type { Bridge, PayloadOf } from '../bridge/bridge';
import type { NativeEventType } from '../bridge/generated';

export interface LatestEventStore<T extends NativeEventType> {
  readonly subscribe: (listener: () => void) => () => void;
  readonly getSnapshot: () => PayloadOf<T> | null;
}

/**
 * Keeps the most recent payload of one low-rate native event type for React.
 * High-rate data (meters) must not use this: see PeakMeter.
 */
export function createLatestEventStore<T extends NativeEventType>(
  bridge: Bridge,
  type: T,
): LatestEventStore<T> {
  let latest: PayloadOf<T> | null = null;
  const listeners = new Set<() => void>();

  bridge.on(type, (payload) => {
    latest = payload;
    listeners.forEach((listener) => {
      listener();
    });
  });

  return {
    subscribe: (listener) => {
      listeners.add(listener);
      return () => listeners.delete(listener);
    },
    getSnapshot: () => latest,
  };
}

export function useLatest<T extends NativeEventType>(
  store: LatestEventStore<T>,
): PayloadOf<T> | null {
  return useSyncExternalStore(store.subscribe, store.getSnapshot);
}
