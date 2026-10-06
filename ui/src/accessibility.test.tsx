import { act, fireEvent, render, screen } from '@testing-library/react';
import { computeAccessibleName } from 'dom-accessibility-api';
import { describe, expect, it } from 'vitest';

import { App } from './App';
import { createSimulatedBridge } from './bridge/bridge';
import { BridgeContext } from './bridge/BridgeContext';
import { createStores } from './state/stores';
import { StoresContext } from './state/StoresContext';
import tokens from './styles/tokens.css?raw';

// --- Contrast (WCAG 2.2 AA: 4.5:1 for text) ---------------------------------------------------

function tokenValues(css: string, selector: string): Map<string, string> {
  const start = css.indexOf(selector);
  const block = css.slice(start, css.indexOf('}', start));
  const values = new Map<string, string>();
  for (const [, name, value] of block.matchAll(/--ap-([\w-]+):\s*(#[0-9a-f]{6})/gi))
    if (name && value) values.set(name, value);
  return values;
}

function luminance(hex: string): number {
  const channel = (offset: number) => {
    const c = parseInt(hex.slice(offset, offset + 2), 16) / 255;
    return c <= 0.03928 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4;
  };
  return 0.2126 * channel(1) + 0.7152 * channel(3) + 0.0722 * channel(5);
}

function contrast(a: string, b: string): number {
  const [high, low] = [luminance(a), luminance(b)].sort((x, y) => y - x) as [number, number];
  return (high + 0.05) / (low + 0.05);
}

describe('colour tokens', () => {
  const dark = tokenValues(tokens, ':root {');

  it('meet AA contrast for text', () => {
    const theme = dark;
    const get = (name: string) => {
      const value = theme.get(name);
      if (!value) throw new Error(`missing token ${name}`);
      return value;
    };
    const pairs: [string, string][] = [
      ['text', 'bg'],
      ['text', 'surface'],
      ['text-muted', 'bg'],
      ['text-muted', 'surface'],
      ['accent', 'bg'],
      ['accent', 'surface'],
      ['accent-ink', 'accent'], // text on accent buttons
      ['clip-ink', 'clip-audio'],
      ['clip-ink', 'clip-synth'],
      ['clip-ink', 'clip-sampler'],
      ['clip-ink', 'clip-drums'],
    ];
    for (const [fg, bg] of pairs)
      expect(contrast(get(fg), get(bg)), `${fg} on ${bg}`).toBeGreaterThanOrEqual(4.5);
  });
});

// --- Every control has a name and can be reached by keyboard ----------------------------------

async function flush() {
  await act(async () => {
    await Promise.resolve();
    await Promise.resolve();
  });
}

async function renderEverything() {
  const bridge = createSimulatedBridge();
  render(
    <BridgeContext.Provider value={bridge}>
      <StoresContext.Provider value={createStores(bridge)}>
        <App />
      </StoresContext.Provider>
    </BridgeContext.Provider>,
  );
  await flush();
  for (const kind of ['Audio', 'Synth', 'Sampler', 'Drums']) {
    fireEvent.click(screen.getByRole('button', { name: '+ Track' }));
    fireEvent.click(screen.getByRole('menuitem', { name: kind }));
    await flush();
  }
  fireEvent.pointerDown(screen.getByRole('group', { name: 'Audio 1 track' }));
  fireEvent.click(screen.getByRole('button', { name: 'Export…' }));
  await flush();
}

describe('accessibility', () => {
  it('gives every interactive element an accessible name', async () => {
    await renderEverything();
    const controls = document.querySelectorAll<HTMLElement>(
      'button, input, select, textarea, [role="button"], [role="tab"], [role="radio"], [role="slider"], [role="menuitem"]',
    );
    expect(controls.length).toBeGreaterThan(50);
    const unnamed = [...controls].filter((element) => computeAccessibleName(element).trim() === '');
    expect(unnamed.map((element) => element.outerHTML.slice(0, 120))).toEqual([]);
  });

  it('keeps every interactive element reachable by keyboard', async () => {
    await renderEverything();
    const unreachable = [
      ...document.querySelectorAll<HTMLElement>('[role="button"], [role="tab"], [role="radio"]'),
    ].filter((element) => element.tabIndex < 0 && !(element instanceof HTMLButtonElement));
    expect(unreachable.map((element) => element.outerHTML.slice(0, 120))).toEqual([]);
  });

  it('moves and resizes the selected clip with the arrow keys', async () => {
    await renderEverything();
    const synthLane = document.querySelector<HTMLElement>('[data-lane-kind="1"]');
    if (!synthLane) throw new Error('no synth lane');
    fireEvent.doubleClick(synthLane, { clientX: 0, clientY: 0 });
    await flush();
    const firstClip = () => document.querySelector<HTMLElement>('.clip');
    const clip = firstClip();
    if (!clip) throw new Error('no clip');
    fireEvent.focus(clip);
    const left = clip.style.left;
    const width = clip.style.width;

    fireEvent.keyDown(window, { key: 'ArrowRight' });
    await flush();
    expect(firstClip()?.style.left).not.toBe(left);

    fireEvent.keyDown(window, { key: 'ArrowRight', shiftKey: true });
    await flush();
    expect(firstClip()?.style.width).not.toBe(width);
  });
});
