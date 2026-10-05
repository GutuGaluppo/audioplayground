import { describe, expect, it } from 'vitest';

import { parseNativeEvent } from './generated';

const validStatus = {
  type: 'engine.status',
  payload: {
    deviceName: 'Speakers',
    sampleRate: 48000,
    bufferSize: 256,
    outputLatencyMs: 5,
    error: '',
    toneEnabled: false,
  },
};

describe('parseNativeEvent', () => {
  it('accepts a well-formed event', () => {
    expect(parseNativeEvent(validStatus)).toEqual(validStatus);
  });

  it.each([
    ['null', null],
    ['an array', []],
    ['an unknown type', { type: 'engine.explode', payload: {} }],
    ['a missing payload', { type: 'engine.meters' }],
    ['an extra envelope key', { type: 'engine.meters', payload: { peak: 0.5 }, extra: 1 }],
    ['an extra payload key', { type: 'engine.meters', payload: { peak: 0.5, extra: 1 } }],
    ['a missing payload key', { type: 'engine.meters', payload: {} }],
    ['a wrong type', { type: 'engine.meters', payload: { peak: '0.5' } }],
    ['NaN', { type: 'engine.meters', payload: { peak: Number.NaN } }],
    ['an out-of-range number', { type: 'engine.meters', payload: { peak: 2 } }],
    [
      'a non-integer int',
      { ...validStatus, payload: { ...validStatus.payload, bufferSize: 256.5 } },
    ],
    [
      'an over-long string',
      { ...validStatus, payload: { ...validStatus.payload, deviceName: 'x'.repeat(257) } },
    ],
    [
      'an inherited key',
      { type: 'engine.meters', payload: Object.create({ peak: 0.5 }) as object },
    ],
  ])('rejects %s', (_label, message) => {
    expect(parseNativeEvent(message)).toBeNull();
  });
});
