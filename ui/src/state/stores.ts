import type { Bridge } from '../bridge/bridge';
import { createParameterStore } from '../params/parameterStore';
import { createSelectionStore } from '../timeline/selection';
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
    drumPads: createKeyedEventStore(bridge, 'drums.pad', (pad) => pad.pad),
    timeline: createLatestEventStore(bridge, 'timeline.state'),
    timelineAssets: createLatestEventStore(bridge, 'timeline.assets'),
    peaks: createKeyedEventStore(bridge, 'timeline.peaks', (event) => event.asset),
    selection: createSelectionStore(),
  };
}

export type Stores = ReturnType<typeof createStores>;
