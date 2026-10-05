import { useSyncExternalStore } from 'react';

import type { Bridge } from '../bridge/bridge';
import { isParamId, PARAMETERS } from './generated';
import type { ParamId } from './generated';

/** Mirror of native parameter values, updated from param.value events. */
export function createParameterStore(bridge: Bridge) {
  const values = new Map<ParamId, number>();
  const listeners = new Map<ParamId, Set<() => void>>();

  bridge.on('param.value', ({ id, value }) => {
    if (!isParamId(id)) return; // native knows a parameter this UI build does not
    const d = PARAMETERS[id];
    if (value < d.min || value > d.max) return;
    values.set(id, value);
    listeners.get(id)?.forEach((listener) => {
      listener();
    });
  });

  return {
    subscribe: (id: ParamId, listener: () => void) => {
      let set = listeners.get(id);
      if (!set) {
        set = new Set();
        listeners.set(id, set);
      }
      set.add(listener);
      return () => set.delete(listener);
    },
    get: (id: ParamId): number | undefined => values.get(id),
  };
}

export type ParameterStore = ReturnType<typeof createParameterStore>;

export function useParameter(store: ParameterStore, id: ParamId): number | undefined {
  return useSyncExternalStore(
    (listener) => store.subscribe(id, listener),
    () => store.get(id),
  );
}
