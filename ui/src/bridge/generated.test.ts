import { describe, expect, it } from 'vitest';

import { parseNativeEvent } from './generated';

const validStatus = {
  type: 'engine.status',
  payload: {
    deviceName: 'Speakers',
    sampleRate: 48000,
    bufferSize: 256,
    outputLatencyMs: 5,
    inputName: '',
    inputChannels: 0,
    roundTripLatencyMs: 0,
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

describe('parseNativeEvent with lists of records', () => {
  const note = { start: 0, length: 240, pitch: 60, velocity: 1 };
  const clip = {
    id: 1,
    start: 0,
    length: 3840,
    asset: 0,
    sourceOffsetSeconds: 0,
    contentOffset: 0,
    loopLength: 0,
    notes: [note],
  };
  const track = {
    id: 1,
    kind: 1,
    name: 'Synth',
    volumeDb: 0,
    pan: 0,
    muted: false,
    soloed: false,
    clips: [clip],
    effects: [{ enabled: true, values: [1, 2, 3] }],
  };
  const state = (tracks: unknown) => ({ type: 'timeline.state', payload: { tracks } });

  it('accepts a nested timeline', () => {
    expect(parseNativeEvent(state([track]))).toEqual(state([track]));
  });

  it.each([
    ['a non-array list', 'x'],
    ['an item with an extra key', [{ ...track, extra: 1 }]],
    ['a bad nested note', [{ ...track, clips: [{ ...clip, notes: [{ ...note, pitch: 128 }] }] }]],
    ['a null item', [null]],
    ['too many items', Array.from({ length: 65 }, () => track)],
  ])('rejects %s', (_label, tracks) => {
    expect(parseNativeEvent(state(tracks))).toBeNull();
  });
});
