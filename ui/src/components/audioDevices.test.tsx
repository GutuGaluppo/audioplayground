import { act, fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it } from 'vitest';

import type { Bridge } from '../bridge/bridge';
import { BridgeContext } from '../bridge/BridgeContext';
import type { Intent } from '../bridge/generated';
import { createStores } from '../state/stores';
import { StoresContext } from '../state/StoresContext';
import { AudioDevicesMenu, bufferMs, formatRate } from './AudioDevicesMenu';

const DEVICES = {
  outputs: [{ name: 'Speakers' }, { name: 'USB DAC' }],
  inputs: [{ name: 'Built-in mic' }, { name: 'USB mic' }],
  output: 'Speakers',
  input: '',
  sampleRates: [44100, 48000, 96000],
  bufferSizes: [64, 128, 256],
  sampleRate: 48000,
  bufferSize: 128,
};

function setup(devices: typeof DEVICES | null) {
  const sent: Intent[] = [];
  const handlers = new Map<string, (payload: unknown) => void>();
  const bridge = {
    isNative: true,
    send: (intent: Intent) => sent.push(intent),
    on: (type: string, handler: (payload: unknown) => void) => {
      handlers.set(type, handler);
      return () => handlers.delete(type);
    },
  } as unknown as Bridge;
  render(
    <BridgeContext.Provider value={bridge}>
      <StoresContext.Provider value={createStores(bridge)}>
        <AudioDevicesMenu />
      </StoresContext.Provider>
    </BridgeContext.Provider>,
  );
  act(() => {
    if (devices) handlers.get('audio.devices')?.(devices);
    handlers.get('engine.status')?.({
      deviceName: devices ? 'Speakers' : '',
      sampleRate: 48000,
      bufferSize: 128,
      outputLatencyMs: 8.5,
      inputName: '',
      inputChannels: 0,
      roundTripLatencyMs: 0,
      error: '',
      toneEnabled: false,
    });
  });
  fireEvent.click(screen.getByRole('button', { name: 'Audio settings' }));
  return sent;
}

describe('audio device menu', () => {
  it('formats rates and buffer times', () => {
    expect(formatRate(44100)).toBe('44.1 kHz');
    expect(formatRate(48000)).toBe('48 kHz');
    expect(bufferMs(128, 48000)).toBe('2.7 ms');
    expect(bufferMs(128, 0)).toBe('');
  });

  it('shows the current choices and sends the change that was picked', () => {
    const sent = setup(DEVICES);
    const value = (name: string) => screen.getByRole<HTMLSelectElement>('combobox', { name }).value;
    expect(value('Output')).toBe('Speakers');
    expect(value('Sample rate')).toBe('48000');
    expect(value('Buffer size')).toBe('128');

    fireEvent.change(screen.getByRole('combobox', { name: 'Output' }), {
      target: { value: 'USB DAC' },
    });
    fireEvent.change(screen.getByRole('combobox', { name: 'Input (for recording)' }), {
      target: { value: 'USB mic' },
    });
    fireEvent.change(screen.getByRole('combobox', { name: 'Sample rate' }), {
      target: { value: '96000' },
    });
    fireEvent.change(screen.getByRole('combobox', { name: 'Buffer size' }), {
      target: { value: '64' },
    });
    expect(sent).toEqual([
      { type: 'audio.setOutput', payload: { name: 'USB DAC' } },
      { type: 'audio.setInput', payload: { name: 'USB mic' } },
      { type: 'audio.setSampleRate', payload: { rate: 96000 } },
      { type: 'audio.setBufferSize', payload: { size: 64 } },
    ]);
    expect(screen.getByRole('status').textContent).toContain('8.5 ms');
  });

  it('explains when no output is open and keeps the native dialog one click away', () => {
    const sent = setup(null);
    expect(screen.getByRole('status').textContent).toContain('No audio output is open');
    fireEvent.click(screen.getByRole('button', { name: 'Advanced…' }));
    expect(sent).toEqual([{ type: 'audio.openSettings', payload: {} }]);
  });
});
