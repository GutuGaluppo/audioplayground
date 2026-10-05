import { act, fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it } from 'vitest';

import { App } from './App';
import { createSimulatedBridge } from './bridge/bridge';
import { BridgeContext } from './bridge/BridgeContext';
import { createEngineStatusStore } from './state/engineStatus';

async function renderApp() {
  const bridge = createSimulatedBridge();
  const store = createEngineStatusStore(bridge);
  render(
    <BridgeContext.Provider value={bridge}>
      <App statusStore={store} />
    </BridgeContext.Provider>,
  );
  // Let the simulated engine answer the initial status request.
  bridge.send({ type: 'tone.setLevel', payload: { db: -18 } });
  await act(async () => {
    await Promise.resolve();
  });
  return bridge;
}

describe('App', () => {
  it('renders the product name as the main heading', async () => {
    await renderApp();
    expect(screen.getByRole('heading', { level: 1 }).textContent).toBe('Audio Playground');
  });

  it('toggles the test tone with the button', async () => {
    await renderApp();
    const button = screen.getByRole('button', { name: /play test tone/i });
    expect(button.getAttribute('aria-pressed')).toBe('false');

    await act(async () => {
      fireEvent.click(button);
      await Promise.resolve();
    });

    expect(
      screen.getByRole('button', { name: /stop test tone/i }).getAttribute('aria-pressed'),
    ).toBe('true');
  });

  it('toggles the test tone with the space bar', async () => {
    await renderApp();
    await act(async () => {
      fireEvent.keyDown(window, { code: 'Space' });
      await Promise.resolve();
    });
    expect(screen.getByRole('button', { name: /stop test tone/i })).toBeTruthy();
  });

  it('shows the device summary', async () => {
    await renderApp();
    expect(screen.getByText(/Simulated output · 48.0 kHz · 256 samples/)).toBeTruthy();
  });
});
