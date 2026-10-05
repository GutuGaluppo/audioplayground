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
  await flush(); // let the simulated engine answer app.ready
  return bridge;
}

describe('App', () => {
  it('has an accessible main heading', async () => {
    await renderApp();
    expect(screen.getByRole('heading', { level: 1 }).textContent).toBe('Audio Playground');
  });

  it('shows the device summary', async () => {
    await renderApp();
    expect(screen.getByText(/Simulated output · 48.0 kHz · 256 samples/)).toBeTruthy();
  });

  it('toggles the test tone with its button', async () => {
    await renderApp();
    fireEvent.click(screen.getByRole('button', { name: /play test tone/i }));
    await flush();
    expect(
      screen.getByRole('button', { name: /stop test tone/i }).getAttribute('aria-pressed'),
    ).toBe('true');
  });

  it('starts and stops the transport with the space bar', async () => {
    await renderApp();
    expect(screen.getByRole('button', { name: 'Play' }).getAttribute('aria-pressed')).toBe('false');

    fireEvent.keyDown(window, { code: 'Space' });
    await flush();
    expect(screen.getByRole('button', { name: 'Stop' }).getAttribute('aria-pressed')).toBe('true');

    fireEvent.keyDown(window, { code: 'Space' });
    await flush();
    expect(screen.getByRole('button', { name: 'Play' })).toBeTruthy();
  });

  it('ignores the space bar while typing in a field', async () => {
    await renderApp();
    fireEvent.keyDown(screen.getByRole('spinbutton', { name: /tempo/i }), { code: 'Space' });
    await flush();
    expect(screen.getByRole('button', { name: 'Play' })).toBeTruthy();
  });

  it('commits a new tempo on Enter and clamps it to the valid range', async () => {
    await renderApp();
    const tempo = screen.getByRole<HTMLInputElement>('spinbutton', { name: /tempo/i });
    expect(tempo.value).toBe('120');

    fireEvent.change(tempo, { target: { value: '999' } });
    fireEvent.blur(tempo);
    await flush();
    expect(tempo.value).toBe('300');

    fireEvent.change(tempo, { target: { value: '96.5' } });
    fireEvent.blur(tempo);
    await flush();
    expect(tempo.value).toBe('96.5');
  });

  it('toggles the metronome and count-in', async () => {
    await renderApp();
    const click = screen.getByRole('button', { name: 'Click' });
    const countIn = screen.getByRole('button', { name: 'Count-in' });

    fireEvent.click(click);
    fireEvent.click(countIn);
    await flush();

    expect(click.getAttribute('aria-pressed')).toBe('true');
    expect(countIn.getAttribute('aria-pressed')).toBe('true');
  });

  it('shows parameter values from the engine and sends changes', async () => {
    await renderApp();
    const level = screen.getByRole('slider', { name: 'Level' });
    expect(level.getAttribute('aria-valuetext')).toBe('-18.0 dB');

    fireEvent.change(level, { target: { value: '1000' } });
    await flush();
    expect(level.getAttribute('aria-valuetext')).toBe('-6.0 dB');

    fireEvent.doubleClick(level);
    await flush();
    expect(level.getAttribute('aria-valuetext')).toBe('-18.0 dB');
  });

  it('undoes and redoes a tempo change with the buttons', async () => {
    await renderApp();
    const tempo = screen.getByRole<HTMLInputElement>('spinbutton', { name: /tempo/i });
    expect(screen.getByRole('button', { name: 'Undo' })).toHaveProperty('disabled', true);

    fireEvent.change(tempo, { target: { value: '90' } });
    fireEvent.blur(tempo);
    await flush();
    expect(tempo.value).toBe('90');

    fireEvent.click(screen.getByRole('button', { name: 'Undo Change tempo' }));
    await flush();
    expect(tempo.value).toBe('120');

    fireEvent.click(screen.getByRole('button', { name: 'Redo Change tempo' }));
    await flush();
    expect(tempo.value).toBe('90');
  });

  it('records a whole slider drag as one undo step (keyboard shortcut)', async () => {
    await renderApp();
    const level = screen.getByRole('slider', { name: 'Level' });

    fireEvent.pointerDown(level);
    for (const value of ['600', '700', '800', '900']) {
      fireEvent.change(level, { target: { value } });
      await flush();
    }
    fireEvent.pointerUp(level);
    expect(level.getAttribute('aria-valuetext')).not.toBe('-18.0 dB');

    fireEvent.keyDown(window, { key: 'z', metaKey: true, ctrlKey: true });
    await flush();
    expect(level.getAttribute('aria-valuetext')).toBe('-18.0 dB');
    expect(screen.getByRole('button', { name: 'Undo' })).toHaveProperty('disabled', true);
  });

  it('leaves Cmd/Ctrl+Z to text fields while typing', async () => {
    await renderApp();
    const tempo = screen.getByRole<HTMLInputElement>('spinbutton', { name: /tempo/i });
    fireEvent.change(tempo, { target: { value: '90' } });
    fireEvent.blur(tempo);
    await flush();

    fireEvent.keyDown(tempo, { key: 'z', metaKey: true, ctrlKey: true });
    await flush();
    expect(tempo.value).toBe('90');
  });

  it('marks the project dirty after an edit and clean after saving', async () => {
    await renderApp();
    expect(screen.queryByLabelText('Unsaved changes')).toBeNull();

    fireEvent.click(screen.getByRole('button', { name: 'Click' }));
    const tempo = screen.getByRole<HTMLInputElement>('spinbutton', { name: /tempo/i });
    fireEvent.change(tempo, { target: { value: '100' } });
    fireEvent.blur(tempo);
    await flush();
    expect(screen.getByLabelText('Unsaved changes')).toBeTruthy();

    fireEvent.keyDown(window, { key: 's', metaKey: true, ctrlKey: true });
    await flush();
    expect(screen.queryByLabelText('Unsaved changes')).toBeNull();
    expect(screen.getByText('Saved (simulated).')).toBeTruthy();
  });

  it('renames the project inline', async () => {
    await renderApp();
    fireEvent.click(screen.getByRole('button', { name: 'Untitled' }));
    const input = screen.getByRole<HTMLInputElement>('textbox', { name: 'Project name' });
    fireEvent.change(input, { target: { value: 'Night Drive' } });
    fireEvent.keyDown(input, { key: 'Enter' });
    fireEvent.blur(input);
    await flush();
    expect(screen.getByRole('button', { name: 'Night Drive' })).toBeTruthy();
  });

  it('shows the bar and beat position', async () => {
    await renderApp();
    expect(screen.getByLabelText('Position').textContent).toBe('1.1');
  });
});
