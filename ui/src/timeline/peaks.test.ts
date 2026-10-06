import { describe, expect, it } from 'vitest';

import { decodePeaks, peakIn } from './peaks';

describe('peaks', () => {
  const event = {
    asset: 1,
    peaksPerSecond: 10,
    data: btoa(String.fromCharCode(0, 51, 255, 0, 102)),
  };

  it('decodes the base64 data once', () => {
    const peaks = decodePeaks(event);
    expect(peaks?.data).toEqual(new Uint8Array([0, 51, 255, 0, 102]));
    expect(decodePeaks(event)).toBe(peaks);
  });

  it('rejects malformed data', () => {
    expect(decodePeaks({ asset: 1, peaksPerSecond: 10, data: '%%%' })).toBeNull();
  });

  it('finds the loudest peak in a time range', () => {
    const peaks = decodePeaks(event);
    if (!peaks) throw new Error('decode failed');
    expect(peakIn(peaks, 0, 0.1)).toBe(0);
    expect(peakIn(peaks, 0.1, 0.2)).toBeCloseTo(0.2);
    expect(peakIn(peaks, 0, 0.5)).toBe(1);
    expect(peakIn(peaks, 0.31, 0.32)).toBe(0); // a range smaller than a slice reads its slice
    expect(peakIn(peaks, 0.45, 9)).toBeCloseTo(0.4);
    expect(peakIn(peaks, 2, 3)).toBe(0); // past the end
  });
});
