import type { Bridge } from '../bridge/bridge';
import { createParameterStore } from '../params/parameterStore';
import { createKeyedEventStore } from './keyedEvent';
import { createLatestEventStore } from './latestEvent';

export function createStores(bridge: Bridge) {
  return {
    engineStatus: createLatestEventStore(bridge, 'engine.status'),
    transportState: createLatestEventStore(bridge, 'transport.state'),
    transportPosition: createLatestEventStore(bridge, 'transport.position'),
    parameters: createParameterStore(bridge),
    history: createLatestEventStore(bridge, 'history.state'),
    project: createLatestEventStore(bridge, 'project.state'),
    instrument: createLatestEventStore(bridge, 'instrument.state'),
    sampler: createLatestEventStore(bridge, 'sampler.state'),
    drumPattern: createLatestEventStore(bridge, 'drums.pattern'),
    drumPads: createKeyedEventStore(bridge, 'drums.pad', (pad) => pad.pad),
  };
}

export type Stores = ReturnType<typeof createStores>;
