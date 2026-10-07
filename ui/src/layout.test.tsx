import { act, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it } from 'vitest';

import { App } from './App';
import { createSimulatedBridge } from './bridge/bridge';
import { BridgeContext } from './bridge/BridgeContext';
import {
  getLayoutPrefs,
  LAYOUT_DEFAULTS,
  reloadLayoutPrefs,
  resetLayoutPrefs,
} from './state/layoutPrefs';
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

describe('resizable layout', () => {
  beforeEach(() => {
    window.localStorage.removeItem('ap.layout');
    reloadLayoutPrefs();
  });
  afterEach(() => {
    resetLayoutPrefs();
  });

  it('sets the track height from the S/M/L presets and remembers it', async () => {
    await renderApp();
    fireEvent.click(screen.getByRole('button', { name: 'Large tracks' }));
    expect(getLayoutPrefs().laneHeight).toBe(120);
    expect(screen.getByRole('button', { name: 'Large tracks' }).getAttribute('aria-pressed')).toBe(
      'true',
    );
    expect(JSON.parse(window.localStorage.getItem('ap.layout') ?? '{}')).toMatchObject({
      laneHeight: 120,
    });

    fireEvent.click(screen.getByRole('button', { name: 'Small tracks' }));
    expect(getLayoutPrefs().laneHeight).toBe(48);
  });

  it('resizes a zone with the keyboard and clamps it to its limits', async () => {
    await renderApp();
    const bar = screen.getByRole('separator', { name: 'Editor panel height' });
    const before = getLayoutPrefs().bottomHeight;
    // The bottom zone grows when its handle moves up.
    fireEvent.keyDown(bar, { key: 'ArrowUp' });
    expect(getLayoutPrefs().bottomHeight).toBe(before + 16);
    fireEvent.keyDown(bar, { key: 'End' });
    expect(Number(bar.getAttribute('aria-valuenow'))).toBe(
      Number(bar.getAttribute('aria-valuemax')),
    );
    fireEvent.keyDown(bar, { key: 'Home' });
    expect(Number(bar.getAttribute('aria-valuenow'))).toBe(
      Number(bar.getAttribute('aria-valuemin')),
    );
  });

  it('resizes a zone by dragging', async () => {
    await renderApp();
    const bar = screen.getByRole('separator', { name: 'Instrument panel width' });
    fireEvent.pointerDown(bar, { clientX: 100, pointerId: 1 });
    fireEvent.pointerMove(window, { clientX: 160 });
    expect(getLayoutPrefs().instrumentWidth).toBe(LAYOUT_DEFAULTS.instrumentWidth + 60);
    fireEvent.pointerUp(window);
    fireEvent.pointerMove(window, { clientX: 400 });
    expect(getLayoutPrefs().instrumentWidth).toBe(LAYOUT_DEFAULTS.instrumentWidth + 60);
  });

  it('restores the defaults with Reset layout', async () => {
    await renderApp();
    fireEvent.keyDown(screen.getByRole('separator', { name: 'Keyboard height' }), { key: 'End' });
    expect(getLayoutPrefs().keyboardHeight).not.toBe(LAYOUT_DEFAULTS.keyboardHeight);
    fireEvent.click(screen.getByRole('button', { name: 'Reset layout' }));
    expect(getLayoutPrefs()).toEqual(LAYOUT_DEFAULTS);
  });

  it('ignores corrupt stored values', () => {
    window.localStorage.setItem('ap.layout', '{"laneHeight":"x","keyboardHeight":99999}');
    reloadLayoutPrefs();
    expect(getLayoutPrefs().laneHeight).toBe(LAYOUT_DEFAULTS.laneHeight);
    expect(getLayoutPrefs().keyboardHeight).toBe(220);
  });
});
