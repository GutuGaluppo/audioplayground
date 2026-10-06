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

  it('plays notes from the computer keyboard and releases them on blur', async () => {
    const bridge = await renderApp();
    const sent: string[] = [];
    const original = bridge.send.bind(bridge);
    bridge.send = (intent) => {
      sent.push(
        intent.type === 'note.on' || intent.type === 'note.off'
          ? `${intent.type}:${String(intent.payload.note)}`
          : intent.type,
      );
      original(intent);
    };

    fireEvent.keyDown(window, { code: 'KeyA' });
    fireEvent.keyDown(window, { code: 'KeyA', repeat: true }); // auto-repeat ignored
    expect(screen.getByRole('button', { name: 'C3' }).getAttribute('data-held')).toBe('true');
    fireEvent.keyUp(window, { code: 'KeyA' });

    fireEvent.keyDown(window, { code: 'KeyX' }); // octave up
    fireEvent.keyDown(window, { code: 'KeyA' });
    fireEvent.blur(window);

    expect(sent).toEqual(['note.on:48', 'note.off:48', 'note.on:60', 'note.allOff']);
  });

  it('switches the synth waveform', async () => {
    await renderApp();
    const square = screen.getByRole('radio', { name: 'Square' });
    fireEvent.click(square);
    await flush();
    expect(square.getAttribute('aria-checked')).toBe('true');
    expect(screen.getByRole('radio', { name: 'Saw' }).getAttribute('aria-checked')).toBe('false');
  });

  it('switches to the sampler and loads a sample', async () => {
    await renderApp();
    fireEvent.click(screen.getByRole('tab', { name: 'Sampler' }));
    await flush();
    expect(screen.getByRole('tab', { name: 'Sampler' }).getAttribute('aria-selected')).toBe('true');
    expect(screen.getByText('No sample loaded')).toBeTruthy();

    fireEvent.click(screen.getByRole('button', { name: 'Load sample…' }));
    await flush();
    expect(screen.getByText('Simulated sample.wav')).toBeTruthy();
    expect(screen.getByText('1.50 s')).toBeTruthy();
  });

  it('paints drum steps in one stroke and plays pads from the keyboard', async () => {
    const bridge = await renderApp();
    fireEvent.click(screen.getByRole('tab', { name: 'Drums' }));
    await flush();

    const sent: string[] = [];
    const original = bridge.send.bind(bridge);
    bridge.send = (intent) => {
      if (intent.type === 'drums.setStep')
        sent.push(
          `${String(intent.payload.step)}:${String(intent.payload.on)}:${String(intent.payload.gesture > 0)}`,
        );
      if (intent.type === 'drums.trigger') sent.push(`pad ${String(intent.payload.pad)}`);
      original(intent);
    };

    const step = (n: number) => screen.getByRole('button', { name: `Kick step ${String(n)}` });
    fireEvent.pointerDown(step(1));
    fireEvent.pointerEnter(step(2));
    fireEvent.pointerEnter(step(3));
    fireEvent.pointerUp(window);
    fireEvent.pointerEnter(step(4)); // stroke ended: no change
    await flush();

    expect(sent).toEqual(['0:true:true', '1:true:true', '2:true:true']);
    expect(step(1).getAttribute('aria-pressed')).toBe('true');
    expect(step(4).getAttribute('aria-pressed')).toBe('false');

    fireEvent.keyDown(window, { code: 'KeyZ' });
    fireEvent.keyDown(window, { code: 'Digit4' });
    expect(sent.slice(-2)).toEqual(['pad 0', 'pad 15']);
    expect(screen.queryByRole('region', { name: 'Keyboard' })).toBeNull(); // piano hidden for drums
  });

  it('shows the bar and beat position', async () => {
    await renderApp();
    expect(screen.getByLabelText('Position').textContent).toBe('1.1');
  });

  it('adds tracks from the menu, one per instrument', async () => {
    await renderApp();
    fireEvent.click(screen.getByRole('button', { name: '+ Track' }));
    fireEvent.click(screen.getByRole('menuitem', { name: 'Synth' }));
    await flush();
    expect(screen.getByRole('group', { name: 'Synth track' })).toBeTruthy();

    fireEvent.click(screen.getByRole('button', { name: '+ Track' }));
    expect(screen.getByRole('menuitem', { name: 'Synth' })).toHaveProperty('disabled', true);
    fireEvent.click(screen.getByRole('menuitem', { name: 'Audio' }));
    await flush();
    expect(screen.getByRole('group', { name: 'Audio 1 track' })).toBeTruthy();
  });

  it('creates a clip, edits its notes, and deletes it with undo', async () => {
    await renderApp();
    fireEvent.click(screen.getByRole('button', { name: '+ Track' }));
    fireEvent.click(screen.getByRole('menuitem', { name: 'Synth' }));
    await flush();

    const lane = document.querySelector<HTMLElement>('[data-lane-track]');
    if (!lane) throw new Error('no lane');
    fireEvent.doubleClick(lane, { clientX: 0 });
    await flush();
    const clip = screen.getByRole('button', { name: 'Synth clip' });

    fireEvent.pointerDown(clip, { button: 0, clientX: 5, clientY: 5 });
    fireEvent.pointerUp(window);
    await flush();
    expect(clip.getAttribute('aria-pressed')).toBe('true');

    // The piano roll opens for the selected clip; clicking the grid adds a note.
    const grid = screen.getByRole('grid', { name: 'Notes' });
    fireEvent.pointerDown(grid, { button: 0, clientX: 0, clientY: 0 });
    await flush();
    expect(screen.getAllByRole('gridcell')).toHaveLength(1);

    // Backspace deletes the selected note first, then (nothing selected in the editor) the clip.
    fireEvent.keyDown(window, { key: 'Backspace' });
    await flush();
    expect(screen.queryAllByRole('gridcell')).toHaveLength(0);
    fireEvent.keyDown(window, { key: 'Backspace' });
    await flush();
    expect(screen.queryByRole('button', { name: 'Synth clip' })).toBeNull();

    fireEvent.click(screen.getByRole('button', { name: 'Undo Delete clip' }));
    await flush();
    expect(screen.getByRole('button', { name: 'Synth clip' })).toBeTruthy();
  });

  it('starts a drum pattern from the first step and shows it on the timeline', async () => {
    await renderApp();
    fireEvent.click(screen.getByRole('tab', { name: 'Drums' }));
    await flush();
    expect(screen.queryByRole('button', { name: 'Drums clip' })).toBeNull();

    fireEvent.pointerDown(screen.getByRole('button', { name: 'Kick step 1' }));
    fireEvent.pointerUp(window);
    await flush();
    expect(screen.getByRole('button', { name: 'Drums clip' })).toBeTruthy();
    expect(screen.getByRole('button', { name: 'Kick step 1' }).getAttribute('aria-pressed')).toBe(
      'true',
    );

    fireEvent.click(screen.getByRole('button', { name: 'Clear pattern' }));
    await flush();
    expect(screen.getByRole('button', { name: 'Kick step 1' }).getAttribute('aria-pressed')).toBe(
      'false',
    );
  });

  it('records and loops from the transport', async () => {
    await renderApp();
    fireEvent.click(screen.getByRole('button', { name: 'Record' }));
    await flush();
    expect(
      screen.getByRole('button', { name: 'Stop recording' }).getAttribute('aria-pressed'),
    ).toBe('true');
    fireEvent.keyDown(window, { code: 'Space' });
    await flush();
    expect(screen.getByRole('button', { name: 'Record' }).getAttribute('aria-pressed')).toBe(
      'false',
    );

    const loop = screen.getByRole('button', { name: 'Loop' });
    fireEvent.click(loop);
    await flush();
    expect(loop.getAttribute('aria-pressed')).toBe('true');
  });

  it('arms an audio track and meters its input', async () => {
    await renderApp();
    fireEvent.click(screen.getByRole('button', { name: '+ Track' }));
    fireEvent.click(screen.getByRole('menuitem', { name: 'Audio' }));
    await flush();
    expect(screen.queryByRole('img', { name: 'Audio 1 input level' })).toBeNull();

    const arm = screen.getByRole('button', { name: 'Record into Audio 1' });
    fireEvent.click(arm);
    await flush();
    expect(arm.getAttribute('aria-pressed')).toBe('true');
    expect(screen.getByRole('img', { name: 'Audio 1 input level' })).toBeTruthy();

    fireEvent.click(arm);
    await flush();
    expect(arm.getAttribute('aria-pressed')).toBe('false');
  });

  it('captures what was just played with Shift+C', async () => {
    await renderApp();
    const capture = screen.getByRole('button', { name: 'Capture what you just played' });
    expect(capture).toHaveProperty('disabled', true);

    fireEvent.keyDown(window, { code: 'KeyA' });
    fireEvent.keyUp(window, { code: 'KeyA' });
    await flush();
    expect(capture).toHaveProperty('disabled', false);

    fireEvent.keyDown(window, { code: 'KeyC', shiftKey: true });
    await flush();
    expect(capture).toHaveProperty('disabled', true);
  });

  it("edits the selected track's effects, with presets and undo", async () => {
    await renderApp();
    fireEvent.click(screen.getByRole('button', { name: '+ Track' }));
    fireEvent.click(screen.getByRole('menuitem', { name: 'Audio' }));
    await flush();
    expect(screen.queryByRole('region', { name: 'Audio 1 effects' })).toBeNull();

    fireEvent.pointerDown(screen.getByRole('group', { name: 'Audio 1 track' }));
    await flush();
    const panel = screen.getByRole('region', { name: 'Audio 1 effects' });
    expect(panel).toBeTruthy();

    const reverb = screen.getByRole('button', { name: 'Reverb off' });
    fireEvent.click(reverb);
    await flush();
    expect(screen.getByRole('button', { name: 'Reverb on' }).getAttribute('aria-pressed')).toBe(
      'true',
    );

    fireEvent.change(screen.getByRole('combobox', { name: 'Delay preset' }), {
      target: { value: 'delay.space-echo' },
    });
    await flush();
    expect(screen.getByRole('button', { name: 'Delay on' })).toBeTruthy();
    expect(screen.getByRole('slider', { name: 'Time' }).getAttribute('aria-valuetext')).toBe(
      '380 ms',
    );

    fireEvent.click(screen.getByRole('button', { name: /undo/i }));
    await flush();
    expect(screen.getByRole('button', { name: 'Delay off' })).toBeTruthy();
  });
});
