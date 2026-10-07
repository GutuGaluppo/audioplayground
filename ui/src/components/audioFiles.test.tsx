import { act, fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it } from 'vitest';

import type { Bridge } from '../bridge/bridge';
import { BridgeContext } from '../bridge/BridgeContext';
import type { Intent, ProjectAsset } from '../bridge/generated';
import { createStores } from '../state/stores';
import { StoresContext } from '../state/StoresContext';
import { AudioFilesMenu, describeUse } from './AudioFilesMenu';

const asset = (over: Partial<ProjectAsset>): ProjectAsset => ({
  id: 1,
  name: 'kick.wav',
  clips: 0,
  pads: 0,
  sampler: false,
  missing: false,
  ...over,
});

function setup(assets: ProjectAsset[]) {
  const sent: Intent[] = [];
  const handlers = new Map<string, (payload: unknown) => void>();
  const bridge = {
    isNative: false,
    send: (intent: Intent) => sent.push(intent),
    on: (type: string, handler: (payload: unknown) => void) => {
      handlers.set(type, handler);
      return () => handlers.delete(type);
    },
  } as unknown as Bridge;
  const stores = createStores(bridge);
  render(
    <BridgeContext.Provider value={bridge}>
      <StoresContext.Provider value={stores}>
        <AudioFilesMenu />
      </StoresContext.Provider>
    </BridgeContext.Provider>,
  );
  act(() => {
    handlers.get('project.assets')?.({ assets });
  });
  return sent;
}

describe('project audio files', () => {
  it('says what uses a file', () => {
    expect(describeUse(asset({}))).toBe('Unused');
    expect(describeUse(asset({ clips: 2, sampler: true, pads: 1 }))).toBe(
      '2 clips · sampler · 1 pad',
    );
    expect(describeUse(asset({ clips: 1 }))).toBe('1 clip');
  });

  it('lists the files, locates missing ones and removes unused ones', () => {
    const sent = setup([
      asset({ id: 1, name: 'riff.wav', clips: 2 }),
      asset({ id: 2, name: 'gone.wav', clips: 1, missing: true }),
      asset({ id: 3, name: 'old.wav' }),
      asset({ id: 4, name: 'older.wav' }),
    ]);
    fireEvent.click(screen.getByRole('button', { name: 'Audio (4)' }));
    expect(screen.getByRole('dialog', { name: 'Project audio files' })).toBeTruthy();
    expect(screen.queryByRole('button', { name: 'Remove riff.wav' })).toBeNull();
    expect(screen.queryByRole('button', { name: 'Remove gone.wav' })).toBeNull();

    fireEvent.click(screen.getByRole('button', { name: 'Locate gone.wav' }));
    fireEvent.click(screen.getByRole('button', { name: 'Remove old.wav' }));
    fireEvent.click(screen.getByRole('button', { name: 'Remove unused (2)' }));
    expect(sent).toEqual([
      { type: 'asset.locate', payload: { asset: 2 } },
      { type: 'asset.remove', payload: { asset: 3 } },
      { type: 'asset.removeUnused', payload: {} },
    ]);
  });

  it('disables "Remove unused" when everything is in use', () => {
    setup([asset({ clips: 1 })]);
    fireEvent.click(screen.getByRole('button', { name: 'Audio (1)' }));
    expect(
      screen.getByRole<HTMLButtonElement>('button', { name: 'Remove unused (0)' }).disabled,
    ).toBe(true);
  });
});
