import { act, fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';

import type { Bridge } from '../bridge/bridge';
import { BridgeContext } from '../bridge/BridgeContext';
import type { ExportDownload, ExportResult } from '../web/exporter';
import { ExportReady } from './ExportReady';

function setup(initial: ExportResult | null) {
  let result = initial;
  const listeners = new Set<() => void>();
  const file = {
    result: () => result,
    subscribe: (listener: () => void) => {
      listeners.add(listener);
      return () => listeners.delete(listener);
    },
    download: vi.fn(),
    dismiss: vi.fn(),
  } satisfies ExportDownload;
  render(
    <BridgeContext.Provider
      value={{ kind: 'wasm', isNative: false, exportFile: file } as unknown as Bridge}
    >
      <ExportReady />
    </BridgeContext.Provider>,
  );
  return {
    file,
    show: (next: ExportResult | null) => {
      result = next;
      act(() => {
        listeners.forEach((l) => {
          l();
        });
      });
    },
  };
}

const ready: ExportResult = {
  fileName: 'Late night.wav',
  bytes: new Uint8Array(4),
  seconds: 187,
  lufs: -14.2,
  truePeak: -1,
  missingAudio: 0,
};

describe('finished export (browser version)', () => {
  it('shows nothing until a file is ready, nor on the desktop', () => {
    const { show } = setup(null);
    expect(screen.queryByRole('dialog')).toBeNull();
    show(ready);
    expect(screen.getByRole('dialog', { name: 'Your song is ready' })).toBeTruthy();

    const desktop = { kind: 'native', isNative: true } as unknown as Bridge;
    render(
      <BridgeContext.Provider value={desktop}>
        <ExportReady />
      </BridgeContext.Provider>,
    );
    expect(screen.getAllByRole('dialog')).toHaveLength(1);
  });

  it('says what the file holds, and Download is the way in', () => {
    const { file } = setup(ready);
    expect(screen.getByText('Late night.wav')).toBeTruthy();
    expect(screen.getByText(/3:07 · -14\.2 LUFS · peak -1\.0 dBTP/)).toBeTruthy();
    expect(document.activeElement).toBe(screen.getByRole('button', { name: 'Download' }));
    fireEvent.click(screen.getByRole('button', { name: 'Download' }));
    expect(file.download).toHaveBeenCalledOnce();
    fireEvent.click(screen.getByRole('button', { name: 'Close' }));
    fireEvent.keyDown(screen.getByRole('dialog'), { key: 'Escape' });
    expect(file.dismiss).toHaveBeenCalledTimes(2);
  });

  it('warns about audio that is silent in the file', () => {
    const { show } = setup({ ...ready, missingAudio: 1 });
    expect(screen.getByRole('status').textContent).toContain('One audio file could not be found');
    show({ ...ready, missingAudio: 3 });
    expect(screen.getByRole('status').textContent).toContain('3 audio files could not be found');
  });
});
