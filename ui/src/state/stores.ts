import type { Bridge } from '../bridge/bridge';
import { createParameterStore } from '../params/parameterStore';
import { createLatestEventStore } from './latestEvent';

export function createStores(bridge: Bridge) {
  return {
    engineStatus: createLatestEventStore(bridge, 'engine.status'),
    transportState: createLatestEventStore(bridge, 'transport.state'),
    transportPosition: createLatestEventStore(bridge, 'transport.position'),
    parameters: createParameterStore(bridge),
  };
}

export type Stores = ReturnType<typeof createStores>;
