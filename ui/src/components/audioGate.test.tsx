import { act, fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';

import type { AudioGate as Gate, AudioGateState, Bridge } from '../bridge/bridge';
import { BridgeContext } from '../bridge/BridgeContext';
import { AudioGate } from './AudioGate';

function setup(initial: AudioGateState) {
  let state = initial;
  let message = '';
  const listeners = new Set<() => void>();
  const start = vi.fn(() => {
    state = 'running';
    listeners.forEach((l) => {
      l();
    });
    return Promise.resolve();
  });
  const gate: Gate = {
    state: () => state,
    error: () => message,
    subscribe: (listener) => {
      listeners.add(listener);
      return () => listeners.delete(listener);
    },
    start,
  };
  const bridge = { kind: 'wasm', isNative: false, audio: gate } as unknown as Bridge;
  render(
    <BridgeContext.Provider value={bridge}>
      <AudioGate />
    </BridgeContext.Provider>,
  );
  const change = (next: AudioGateState, text = '') => {
    state = next;
    message = text;
    act(() => {
      listeners.forEach((l) => {
        l();
      });
    });
  };
  return { start, change };
}

describe('start-audio gate', () => {
  it('is not there when the engine does not need it', () => {
    const bridge = { kind: 'native', isNative: true } as unknown as Bridge;
    render(
      <BridgeContext.Provider value={bridge}>
        <AudioGate />
      </BridgeContext.Provider>,
    );
    expect(screen.queryByRole('dialog')).toBeNull();
  });

  it('says it is loading, then asks for a click, then gets out of the way', () => {
    const { start, change } = setup('loading');
    expect(screen.getByRole('status').textContent).toContain('Loading');
    expect(screen.getByRole<HTMLButtonElement>('button', { name: 'Start audio' }).disabled).toBe(
      true,
    );

    change('blocked');
    expect(screen.getByRole('status').textContent).toContain('needs a click');
    const button = screen.getByRole<HTMLButtonElement>('button', { name: 'Start audio' });
    expect(button.disabled).toBe(false);
    expect(document.activeElement).toBe(button);

    fireEvent.click(button);
    expect(start).toHaveBeenCalledOnce();
    expect(screen.queryByRole('dialog')).toBeNull();
  });

  it('shows what went wrong', () => {
    const { change } = setup('loading');
    change('failed', 'This browser cannot run the audio engine (no AudioWorklet).');
    expect(screen.getByRole('alert').textContent).toContain('no AudioWorklet');
    expect(screen.getByRole<HTMLButtonElement>('button', { name: 'Start audio' }).disabled).toBe(
      true,
    );
  });
});
