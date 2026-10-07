import { act, fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it } from 'vitest';

import { App } from './App';
import { createSimulatedBridge } from './bridge/bridge';
import { BridgeContext } from './bridge/BridgeContext';
import { createStores } from './state/stores';
import { StoresContext } from './state/StoresContext';

async function flush() {
  await act(async () => {
    await Promise.resolve();
    await Promise.resolve();
  });
}

async function renderApp() {
  const bridge = createSimulatedBridge();
  render(
    <BridgeContext.Provider value={bridge}>
      <StoresContext.Provider value={createStores(bridge)}>
        <App />
      </StoresContext.Provider>
    </BridgeContext.Provider>,
  );
  await flush();
}

const undo = async () => {
  fireEvent.click(screen.getByRole('button', { name: /undo/i }));
  await flush();
};

describe('instrument presets', () => {
  it('applies a synth preset to every knob as one undo step', async () => {
    await renderApp();
    const cutoff = () => screen.getByRole('slider', { name: 'Brightness' });
    const before = cutoff().getAttribute('aria-valuetext');

    fireEvent.change(screen.getByRole('combobox', { name: 'Synth preset' }), {
      target: { value: 'synth.punchy-bass' },
    });
    await flush();
    expect(cutoff().getAttribute('aria-valuetext')).not.toBe(before);
    expect(screen.getByRole('radio', { name: 'Saw' }).getAttribute('aria-checked')).toBe('true');

    await undo(); // one step puts back everything the preset changed
    expect(cutoff().getAttribute('aria-valuetext')).toBe(before);
  });

  it('writes a drum pattern into the pattern clip, creating it, in one undo step', async () => {
    await renderApp();
    fireEvent.click(screen.getByRole('tab', { name: /Drums/ }));
    await flush();
    const step = (pad: string, n: number) =>
      screen.getByRole('button', { name: `${pad} step ${String(n)}` });
    expect(step('Kick', 1).getAttribute('aria-pressed')).toBe('false');

    fireEvent.change(screen.getByRole('combobox', { name: 'Drum pattern preset' }), {
      target: { value: 'drums.four-on-the-floor' },
    });
    await flush();
    for (const n of [1, 5, 9, 13])
      expect(step('Kick', n).getAttribute('aria-pressed')).toBe('true');
    expect(step('Kick', 2).getAttribute('aria-pressed')).toBe('false');
    expect(step('Clap', 5).getAttribute('aria-pressed')).toBe('true');

    // A second pattern replaces the first instead of adding to it.
    fireEvent.change(screen.getByRole('combobox', { name: 'Drum pattern preset' }), {
      target: { value: 'drums.boom-bap' },
    });
    await flush();
    expect(step('Kick', 5).getAttribute('aria-pressed')).toBe('false');
    expect(step('Kick', 8).getAttribute('aria-pressed')).toBe('true');
    expect(step('Clap', 5).getAttribute('aria-pressed')).toBe('false');

    await undo();
    expect(step('Kick', 5).getAttribute('aria-pressed')).toBe('true'); // back to the first pattern
    expect(step('Clap', 5).getAttribute('aria-pressed')).toBe('true');
  });
});
