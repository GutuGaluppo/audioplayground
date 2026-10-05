import { useSyncExternalStore } from 'react';

import type { Bridge, PayloadOf } from '../bridge/bridge';
import type { NativeEventType } from '../bridge/generated';

export interface KeyedEventStore<P, K> {
  readonly subscribe: (key: K, listener: () => void) => () => void;
  readonly get: (key: K) => P | undefined;
}

/** Latest payload per key (e.g. per drum pad) for one native event type. */
export function createKeyedEventStore<T extends NativeEventType, K>(
  bridge: Bridge,
  type: T,
  keyOf: (payload: PayloadOf<T>) => K,
): KeyedEventStore<PayloadOf<T>, K> {
  const values = new Map<K, PayloadOf<T>>();
  const listeners = new Map<K, Set<() => void>>();

  bridge.on(type, (payload) => {
    const key = keyOf(payload);
    values.set(key, payload);
    listeners.get(key)?.forEach((listener) => {
      listener();
    });
  });

  return {
    subscribe: (key, listener) => {
      let set = listeners.get(key);
      if (!set) {
        set = new Set();
        listeners.set(key, set);
      }
      set.add(listener);
      return () => set.delete(listener);
    },
    get: (key) => values.get(key),
  };
}

export function useKeyed<P, K>(store: KeyedEventStore<P, K>, key: K): P | undefined {
  return useSyncExternalStore(
    (listener) => store.subscribe(key, listener),
    () => store.get(key),
  );
}
