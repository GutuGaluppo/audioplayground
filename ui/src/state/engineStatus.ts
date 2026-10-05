import { useSyncExternalStore } from 'react';

import type { Bridge } from '../bridge/bridge';
import type { EngineStatus } from '../bridge/generated';

export type EngineStatusPayload = EngineStatus['payload'];

/**
 * Low-rate engine status mirrored from native events. High-rate data (meters) deliberately does
 * not go through here: it would re-render React 30+ times per second (guide §20).
 */
export function createEngineStatusStore(bridge: Bridge) {
  let status: EngineStatusPayload | null = null;
  const listeners = new Set<() => void>();

  bridge.on('engine.status', (payload) => {
    status = payload;
    listeners.forEach((listener) => {
      listener();
    });
  });

  return {
    subscribe: (listener: () => void) => {
      listeners.add(listener);
      return () => listeners.delete(listener);
    },
    getSnapshot: () => status,
  };
}

export type EngineStatusStore = ReturnType<typeof createEngineStatusStore>;

export function useEngineStatus(store: EngineStatusStore): EngineStatusPayload | null {
  return useSyncExternalStore(store.subscribe, store.getSnapshot);
}
