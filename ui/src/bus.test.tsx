import { act, fireEvent, render, screen, within } from '@testing-library/react';
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

async function addAudioTrack() {
  fireEvent.click(screen.getByRole('button', { name: '+ Track' }));
  fireEvent.click(screen.getByRole('menuitem', { name: /Audio/ }));
  await flush();
}

describe('effect buses', () => {
  it('adds a bus, selects it to edit its effects, and deletes it', async () => {
    await renderApp();
    fireEvent.click(screen.getByRole('button', { name: '+ Bus' }));
    await flush();

    const bus = screen.getByRole('group', { name: 'Bus 1 bus' });
    fireEvent.pointerDown(bus);
    await flush();
    const panel = screen.getByRole('region', { name: 'Bus 1 effects' });
    fireEvent.click(within(panel).getByRole('button', { name: 'Reverb off' }));
    await flush();
    expect(
      within(screen.getByRole('region', { name: 'Bus 1 effects' }))
        .getByRole('button', { name: 'Reverb on' })
        .getAttribute('aria-pressed'),
    ).toBe('true');

    fireEvent.click(screen.getByRole('button', { name: 'Delete Bus 1' }));
    await flush();
    expect(screen.queryByRole('group', { name: 'Bus 1 bus' })).toBeNull();
    expect(screen.queryByRole('region', { name: 'Bus 1 effects' })).toBeNull();
  });

  it('lets a track send to a bus, and undoes the send', async () => {
    await renderApp();
    await addAudioTrack();
    fireEvent.click(screen.getByRole('button', { name: '+ Bus' }));
    await flush();
    fireEvent.pointerDown(screen.getByRole('group', { name: 'Audio 1 track' }));
    await flush();

    const knob = within(screen.getByRole('region', { name: 'Audio 1 effects' })).getByRole(
      'slider',
      {
        name: 'Send to Bus 1',
      },
    );
    expect(knob.getAttribute('aria-valuetext')).toBe('-60.0 dB');
    fireEvent.keyDown(knob, { key: 'End' });
    await flush();
    expect(
      screen.getByRole('slider', { name: 'Send to Bus 1' }).getAttribute('aria-valuetext'),
    ).toBe('6.0 dB');

    fireEvent.click(screen.getByRole('button', { name: /undo/i }));
    await flush();
    expect(
      screen.getByRole('slider', { name: 'Send to Bus 1' }).getAttribute('aria-valuetext'),
    ).toBe('-60.0 dB');
  });

  it('stops offering buses after eight', async () => {
    await renderApp();
    for (let i = 0; i < 8; i++) {
      fireEvent.click(screen.getByRole('button', { name: '+ Bus' }));
      await flush();
    }
    expect(screen.getByRole<HTMLButtonElement>('button', { name: '+ Bus' }).disabled).toBe(true);
  });
});
