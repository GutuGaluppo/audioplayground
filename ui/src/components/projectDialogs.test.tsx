import { act, fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';

import type { Bridge } from '../bridge/bridge';
import { BridgeContext } from '../bridge/BridgeContext';
import type { ProjectDialog, ProjectLibrary } from '../web/projectLibrary';
import { ProjectDialogs } from './ProjectDialogs';

function setup(initial: ProjectDialog | null) {
  let dialog = initial;
  const listeners = new Set<() => void>();
  const library = {
    dialog: () => dialog,
    subscribe: (listener: () => void) => {
      listeners.add(listener);
      return () => listeners.delete(listener);
    },
    openProject: vi.fn(() => Promise.resolve()),
    deleteProject: vi.fn(() => Promise.resolve()),
    saveAs: vi.fn(() => Promise.resolve()),
    answerDiscard: vi.fn(() => Promise.resolve()),
    answerRecover: vi.fn(() => Promise.resolve()),
    cancel: vi.fn(),
  } satisfies ProjectLibrary;
  const bridge = { kind: 'wasm', isNative: false, projects: library } as unknown as Bridge;
  render(
    <BridgeContext.Provider value={bridge}>
      <button type="button">before</button>
      <ProjectDialogs />
    </BridgeContext.Provider>,
  );
  const show = (next: ProjectDialog | null) => {
    dialog = next;
    act(() => {
      listeners.forEach((l) => {
        l();
      });
    });
  };
  return { library, show };
}

const songs = [
  { id: 'a', name: 'Late night', updatedAt: Date.parse('2026-10-07T12:00:00Z'), bytes: 4096 },
  { id: 'b', name: 'Demo', updatedAt: Date.parse('2026-10-06T12:00:00Z'), bytes: 2048 },
];

describe('song dialogs (browser version)', () => {
  it('shows nothing when there is no question, and nothing at all on the desktop', () => {
    setup(null);
    expect(screen.queryByRole('dialog')).toBeNull();
    const bridge = { kind: 'native', isNative: true } as unknown as Bridge;
    render(
      <BridgeContext.Provider value={bridge}>
        <ProjectDialogs />
      </BridgeContext.Provider>,
    );
    expect(screen.queryByRole('dialog')).toBeNull();
  });

  it('lists the saved songs and opens the one clicked', () => {
    const { library } = setup({ kind: 'open', projects: songs });
    expect(screen.getByRole('dialog', { name: 'Open a song' })).toBeTruthy();
    expect(screen.getByText(/kept in this browser/i)).toBeTruthy();
    fireEvent.click(screen.getByRole('button', { name: /^Demo/ }));
    expect(library.openProject).toHaveBeenCalledWith('b');
  });

  it('puts the first song in focus, and Escape or Cancel closes', () => {
    const { library } = setup({ kind: 'open', projects: songs });
    expect(document.activeElement?.textContent).toContain('Late night');
    fireEvent.keyDown(screen.getByRole('dialog'), { key: 'Escape' });
    expect(library.cancel).toHaveBeenCalledOnce();
    fireEvent.click(screen.getByRole('button', { name: 'Cancel' }));
    expect(library.cancel).toHaveBeenCalledTimes(2);
  });

  it('asks twice before deleting a song', () => {
    const { library } = setup({ kind: 'open', projects: songs });
    fireEvent.click(screen.getByRole('button', { name: 'Delete Demo' }));
    expect(library.deleteProject).not.toHaveBeenCalled();
    fireEvent.click(screen.getByRole('button', { name: 'Keep' }));
    expect(library.deleteProject).not.toHaveBeenCalled();
    fireEvent.click(screen.getByRole('button', { name: 'Delete Demo' }));
    fireEvent.click(screen.getByRole('button', { name: 'Delete “Demo”' }));
    expect(library.deleteProject).toHaveBeenCalledWith('b');
  });

  it('says so when there is nothing saved', () => {
    setup({ kind: 'open', projects: [] });
    expect(screen.getByText(/No saved songs yet/)).toBeTruthy();
  });

  it('asks for a name, warns when it replaces a song, and submits with Enter', () => {
    const { library } = setup({ kind: 'saveAs', name: 'First song', existing: songs });
    const input = screen.getByRole<HTMLInputElement>('textbox', { name: 'Name' });
    expect(input.value).toBe('First song');
    expect(screen.getByRole<HTMLButtonElement>('button', { name: 'Save' }).disabled).toBe(false);

    fireEvent.change(input, { target: { value: 'demo' } });
    expect(screen.getByRole('status').textContent).toContain('Saving replaces it');
    expect(screen.getByRole('button', { name: 'Replace' })).toBeTruthy();

    fireEvent.change(input, { target: { value: '  ' } });
    expect(screen.getByRole<HTMLButtonElement>('button', { name: 'Save' }).disabled).toBe(true);

    fireEvent.change(input, { target: { value: '  Brand new  ' } });
    fireEvent.submit(input.closest('form') as HTMLFormElement);
    expect(library.saveAs).toHaveBeenCalledWith('Brand new');
  });

  it('asks what to do with unsaved changes', () => {
    const { library } = setup({ kind: 'discard', name: 'Late night', then: 'new' });
    expect(screen.getByRole('dialog', { name: 'Save changes to “Late night”?' })).toBeTruthy();
    expect(document.activeElement?.textContent).toBe('Save');
    fireEvent.click(screen.getByRole('button', { name: 'Save' }));
    fireEvent.click(screen.getByRole('button', { name: "Don't save" }));
    fireEvent.click(screen.getByRole('button', { name: 'Cancel' }));
    fireEvent.keyDown(screen.getByRole('dialog'), { key: 'Escape' });
    expect(library.answerDiscard.mock.calls).toEqual([
      ['save'],
      ['discard'],
      ['cancel'],
      ['cancel'],
    ]);
  });

  it('offers to restore unsaved work from the last visit', () => {
    const { library } = setup({
      kind: 'recover',
      name: 'Late night',
      at: Date.parse('2026-10-07T12:00:00Z'),
    });
    expect(screen.getByRole('dialog', { name: 'Restore your unsaved work?' })).toBeTruthy();
    fireEvent.click(screen.getByRole('button', { name: 'Restore' }));
    fireEvent.click(screen.getByRole('button', { name: 'Discard' }));
    expect(library.answerRecover.mock.calls).toEqual([[true], [false]]);
  });

  it('keeps Tab inside the question and gives focus back when it closes', () => {
    const { show } = setup(null);
    const before = screen.getByRole('button', { name: 'before' });
    before.focus();
    show({ kind: 'recover', name: 'X', at: 1 });
    const restore = screen.getByRole('button', { name: 'Restore' });
    expect(document.activeElement).toBe(restore);
    fireEvent.keyDown(restore, { key: 'Tab' }); // Restore is the last control
    expect(document.activeElement).toBe(screen.getByRole('button', { name: 'Discard' }));
    fireEvent.keyDown(screen.getByRole('button', { name: 'Discard' }), {
      key: 'Tab',
      shiftKey: true,
    });
    expect(document.activeElement).toBe(restore);

    show(null);
    expect(document.activeElement).toBe(before);
  });
});
