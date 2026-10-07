import { fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';

import { clampMarker, TrimMarkers } from './SamplerPanel';

describe('sampler trim markers', () => {
  it('keeps start left of end and both inside the sample', () => {
    expect(clampMarker('start', 99, 10, 60)).toBe(59.5);
    expect(clampMarker('end', 5, 10, 60)).toBe(10.5);
    expect(clampMarker('start', -4, 10, 60)).toBe(0);
    expect(clampMarker('end', 140, 10, 60)).toBe(100);
  });

  it('moves a marker with the arrow keys, 5 % with Shift', () => {
    const onChange = vi.fn();
    render(<TrimMarkers start={10} end={60} onChange={onChange} />);
    const start = screen.getByRole('slider', { name: 'Sample start' });
    fireEvent.keyDown(start, { key: 'ArrowRight' });
    fireEvent.keyDown(start, { key: 'ArrowLeft', shiftKey: true });
    fireEvent.keyDown(screen.getByRole('slider', { name: 'Sample end' }), { key: 'ArrowLeft' });
    expect(onChange.mock.calls).toEqual([
      ['start', 11, 0],
      ['start', 5, 0],
      ['end', 59, 0],
    ]);
  });
});
