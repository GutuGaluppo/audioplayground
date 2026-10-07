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

describe('drum kits', () => {
  it('offers every factory kit and follows the selection', async () => {
    const bridge = createSimulatedBridge();
    render(
      <BridgeContext.Provider value={bridge}>
        <StoresContext.Provider value={createStores(bridge)}>
          <App />
        </StoresContext.Provider>
      </BridgeContext.Provider>,
    );
    await flush();
    fireEvent.click(screen.getByRole('tab', { name: /Drums/ }));
    await flush();

    const select = screen.getByRole<HTMLSelectElement>('combobox', { name: 'Drum kit' });
    expect([...select.options].map((o) => o.text)).toEqual([
      'Classic',
      '808',
      'Lo-fi',
      'Acoustic',
      'Electro',
    ]);
    expect(select.value).toBe('0');

    fireEvent.change(select, { target: { value: '2' } });
    await flush();
    expect(screen.getByRole<HTMLSelectElement>('combobox', { name: 'Drum kit' }).value).toBe('2');
  });
});
