import { act, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';

import { App } from '../App';
import { createSimulatedBridge } from '../bridge/bridge';
import type { Bridge } from '../bridge/bridge';
import { BridgeContext } from '../bridge/BridgeContext';
import type { AccompanimentState, Intent } from '../bridge/generated';
import { createStores } from '../state/stores';
import { StoresContext } from '../state/StoresContext';
import { AccompanimentBar, AccompanimentStates } from './AccompanimentBar';

const base: AccompanimentState['payload'] = {
  state: 0,
  reason: 0,
  message: '',
  grooveName: '',
  detail: '',
  clip: 0,
  canTryAnother: false,
};

function setup() {
  const sent: Intent[] = [];
  const handlers = new Map<string, (payload: unknown) => void>();
  const bridge = {
    isNative: true,
    send: (intent: Intent) => sent.push(intent),
    on: (type: string, handler: (payload: unknown) => void) => {
      handlers.set(type, handler);
      return () => handlers.delete(type);
    },
  } as unknown as Bridge;
  render(
    <BridgeContext.Provider value={bridge}>
      <StoresContext.Provider value={createStores(bridge)}>
        <AccompanimentBar />
      </StoresContext.Provider>
    </BridgeContext.Provider>,
  );
  const show = (over: Partial<AccompanimentState['payload']>) => {
    act(() => {
      handlers.get('accompaniment.state')?.({ ...base, ...over });
    });
  };
  return { sent, show };
}

afterEach(() => {
  vi.useRealTimers();
});

describe('beat suggestion strip', () => {
  it('stays out of the way until there is something to say', () => {
    const { show } = setup();
    expect(screen.queryByRole('region', { name: 'Beat suggestion' })).toBeNull();
    show({ state: AccompanimentStates.dismissed });
    expect(screen.queryByRole('region', { name: 'Beat suggestion' })).toBeNull();
    show({ state: AccompanimentStates.idle });
    expect(screen.queryByRole('region', { name: 'Beat suggestion' })).toBeNull();
  });

  it('says it is listening, without offering buttons', () => {
    const { show } = setup();
    show({ state: AccompanimentStates.analyzing, message: 'Listening to your take…' });
    expect(screen.getByRole('status').textContent).toContain('Listening');
    expect(screen.queryByRole('button')).toBeNull();
  });

  it('offers a beat with Preview first, then Add / Try another / Stop while it plays', () => {
    const { sent, show } = setup();
    show({
      state: AccompanimentStates.ready,
      message: 'Try a beat',
      grooveName: 'Soft pocket',
      detail: 'sits well at 96 BPM',
      canTryAnother: true,
    });
    expect(screen.getByRole('status').textContent).toContain('Try a beat');
    expect(screen.getByRole('status').textContent).toContain('Soft pocket');
    expect(screen.getByRole('status').textContent).toContain('sits well at 96 BPM');
    expect(screen.queryByRole('button', { name: 'Add' })).toBeNull(); // not before hearing it

    fireEvent.click(screen.getByRole('button', { name: 'Preview' }));
    fireEvent.click(screen.getByRole('button', { name: 'Try another' }));
    expect(sent).toEqual([
      { type: 'accompaniment.preview', payload: {} },
      { type: 'accompaniment.next', payload: {} },
    ]);

    show({ state: AccompanimentStates.previewing, grooveName: 'Rock', canTryAnother: true });
    expect(screen.queryByRole('button', { name: 'Preview' })).toBeNull();
    fireEvent.click(screen.getByRole('button', { name: 'Add' }));
    fireEvent.click(screen.getByRole('button', { name: 'Stop' }));
    fireEvent.click(screen.getByRole('button', { name: 'Dismiss suggestion' }));
    expect(sent.slice(2)).toEqual([
      { type: 'accompaniment.add', payload: {} },
      { type: 'accompaniment.stopPreview', payload: {} },
      { type: 'accompaniment.dismiss', payload: {} },
    ]);
  });

  it('hides Try another when there is no other beat', () => {
    const { show } = setup();
    show({ state: AccompanimentStates.ready, grooveName: 'Rock', canTryAnother: false });
    expect(screen.queryByRole('button', { name: 'Try another' })).toBeNull();
  });

  it('explains why there is no suggestion', () => {
    const { sent, show } = setup();
    show({
      state: AccompanimentStates.unavailable,
      reason: 1,
      message: "Couldn't find a rhythm pattern here. Try tapping the tempo or add drums manually.",
    });
    expect(screen.getByRole('status').textContent).toContain("Couldn't find a rhythm pattern");
    expect(screen.queryByRole('button', { name: 'Preview' })).toBeNull();
    fireEvent.click(screen.getByRole('button', { name: 'Dismiss suggestion' }));
    expect(sent).toEqual([{ type: 'accompaniment.dismiss', payload: {} }]);
  });

  it('confirms an added beat, then goes away on its own', () => {
    vi.useFakeTimers();
    const { show } = setup();
    show({ state: AccompanimentStates.accepted, message: 'Added Rock', grooveName: 'Rock' });
    expect(screen.getByRole('status').textContent).toContain('Added Rock');
    act(() => {
      vi.advanceTimersByTime(6500);
    });
    expect(screen.queryByRole('region', { name: 'Beat suggestion' })).toBeNull();
  });
});

describe('Try a beat on an audio clip', () => {
  it('is available only for a selected audio clip, and asks for that clip', async () => {
    const bridge = createSimulatedBridge();
    const sent: Intent[] = [];
    const send = bridge.send.bind(bridge);
    bridge.send = (intent) => {
      sent.push(intent);
      send(intent);
    };
    render(
      <BridgeContext.Provider value={bridge}>
        <StoresContext.Provider value={createStores(bridge)}>
          <App />
        </StoresContext.Provider>
      </BridgeContext.Provider>,
    );
    const flush = async () => {
      await act(async () => {
        await Promise.resolve();
        await Promise.resolve();
      });
    };
    await flush();

    const button = () => screen.getByRole<HTMLButtonElement>('button', { name: 'Try a beat' });
    expect(button().disabled).toBe(true); // nothing selected

    fireEvent.click(screen.getByRole('button', { name: '+ Track' }));
    fireEvent.click(screen.getByRole('menuitem', { name: /Audio/ }));
    await flush();
    const lane = document.querySelector('[data-lane-kind="0"]');
    expect(lane).not.toBeNull();
    if (lane) fireEvent.doubleClick(lane);
    await flush();
    const clip = document.querySelector<HTMLElement>('.clip');
    expect(clip).not.toBeNull();
    if (clip) fireEvent.pointerDown(clip);
    await flush();

    expect(button().disabled).toBe(false);
    fireEvent.click(button());
    const suggest = sent.find((i) => i.type === 'accompaniment.suggest');
    expect(suggest).toBeDefined();
  });
});
