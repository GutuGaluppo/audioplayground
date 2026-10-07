import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

import type { Intent } from '../bridge/generated';
import { WebProjectLibrary } from './projectLibrary';
import type { OpenProject, ProjectHost } from './projectLibrary';
import { createMemoryStore } from './projectStore';

// The engine, as far as the library can tell: a project with a name, and whether it is unsaved.
function setup() {
  const store = createMemoryStore();
  const notices: [number, string][] = [];
  const engine = { name: 'First song', dirty: false, hasLocation: false, changes: 0 };
  let ids = 0;
  let clock = Date.parse('2026-10-07T12:00:00Z');
  const sent: Intent[] = [];

  // eslint-disable-next-line prefer-const -- the host and the library need each other
  let library: WebProjectLibrary;
  const report = () => {
    const state: OpenProject = {
      name: engine.name,
      dirty: engine.dirty,
      hasLocation: engine.hasLocation,
    };
    library.projectState(state);
  };
  const host: ProjectHost = {
    exportProject: (stamp) =>
      Promise.resolve(JSON.stringify({ name: engine.name, changes: engine.changes, stamp })),
    importProject: (json, options) => {
      const file = JSON.parse(json) as { name: string; changes: number; bad?: boolean };
      if (file.bad)
        return Promise.resolve({ ok: false as const, error: 'The project file is not valid.' });
      Object.assign(engine, { name: file.name, changes: file.changes, ...options });
      report();
      return Promise.resolve({ ok: true as const });
    },
    markSaved: (hasLocation) => {
      engine.dirty = false;
      engine.hasLocation = hasLocation;
      report();
      return Promise.resolve();
    },
    markUnsaved: () => {
      engine.dirty = true;
      engine.hasLocation = false;
      report();
      return Promise.resolve();
    },
    send: (intent) => {
      sent.push(intent);
      if (intent.type === 'project.new')
        Object.assign(engine, { name: 'Untitled', dirty: false, hasLocation: false });
      if (intent.type === 'project.rename') {
        engine.name = intent.payload.name;
        engine.dirty = true;
      }
      report();
    },
    notice: (level, message) => notices.push([level, message]),
  };
  library = new WebProjectLibrary({
    store,
    host,
    autosaveDelayMs: 2000,
    newId: () => `id-${String(++ids)}`,
    now: () => new Date((clock += 1000)),
  });
  const edit = () => {
    engine.dirty = true;
    engine.changes++;
    report();
  };
  report();
  return { library, store, engine, notices, sent, edit };
}

const flush = async () => {
  await vi.advanceTimersByTimeAsync(0);
};

beforeEach(() => {
  vi.useFakeTimers();
  vi.stubGlobal('navigator', { storage: { persist: () => Promise.resolve(true) } });
});

afterEach(() => {
  vi.useRealTimers();
  vi.unstubAllGlobals();
});

describe('saving', () => {
  it('asks for a name the first time, then saves over the same song without asking', async () => {
    const t = setup();
    t.edit();
    await t.library.request('save');
    expect(t.library.dialog()).toMatchObject({ kind: 'saveAs', name: 'First song' });

    await t.library.saveAs('Late night');
    expect(t.library.dialog()).toBeNull();
    expect(t.engine).toMatchObject({ dirty: false, hasLocation: true, name: 'Late night' });
    expect(t.sent).toContainEqual({ type: 'project.rename', payload: { name: 'Late night' } });
    const stored = await t.store.list();
    expect(stored).toHaveLength(1);
    expect(stored[0]).toMatchObject({ id: 'id-1', name: 'Late night' });
    expect(t.notices.at(-1)?.[1]).toContain('Saved “Late night”');

    t.edit();
    await t.library.request('save');
    expect(t.library.dialog()).toBeNull(); // no question: it has a place now
    expect(await t.store.list()).toHaveLength(1);
    expect(JSON.parse((await t.store.read('id-1')) ?? '{}')).toMatchObject({ changes: 2 });
    expect(t.engine.dirty).toBe(false);
  });

  it('Save As with the name of another song replaces that song instead of duplicating it', async () => {
    const t = setup();
    await t.library.request('saveAs');
    await t.library.saveAs('Demo');
    t.edit();
    await t.library.request('saveAs');
    expect(t.library.dialog()).toMatchObject({ kind: 'saveAs', existing: [{ name: 'Demo' }] });
    await t.library.saveAs('demo'); // same name, different case
    const stored = await t.store.list();
    expect(stored).toHaveLength(1);
    expect(stored[0]?.id).toBe('id-1');
  });

  it('Save As with a new name keeps both songs', async () => {
    const t = setup();
    await t.library.request('saveAs');
    await t.library.saveAs('Demo');
    await t.library.request('saveAs');
    await t.library.saveAs('Demo 2');
    expect((await t.store.list()).map((p) => p.name).sort()).toEqual(['Demo', 'Demo 2']);
    expect(t.library.openId()).toBe('id-2');
  });

  it('ignores an empty name and keeps the question open', async () => {
    const t = setup();
    await t.library.request('saveAs');
    await t.library.saveAs('   ');
    expect(t.library.dialog()?.kind).toBe('saveAs');
    expect(await t.store.list()).toHaveLength(0);
  });

  it('says why it could not save, and the song stays unsaved', async () => {
    const t = setup();
    t.edit();
    await t.library.request('saveAs');
    t.store.failNextWrite(new DOMException('full', 'QuotaExceededError'));
    await t.library.saveAs('Big');
    expect(t.notices.at(-1)).toEqual([2, expect.stringContaining('no room left')]);
    expect(t.engine.dirty).toBe(true);
    expect(await t.store.list()).toHaveLength(0);
    expect(t.library.dialog()?.kind).toBe('saveAs'); // try again with another name, or cancel
  });
});

describe('New and Open', () => {
  it('a clean song is replaced without a question', async () => {
    const t = setup();
    await t.library.request('new');
    expect(t.library.dialog()).toBeNull();
    expect(t.sent).toContainEqual({ type: 'project.new', payload: {} });
    expect(t.engine.name).toBe('Untitled');
  });

  it('unsaved changes ask first: Cancel keeps everything', async () => {
    const t = setup();
    t.edit();
    await t.library.request('new');
    expect(t.library.dialog()).toEqual({ kind: 'discard', name: 'First song', then: 'new' });
    await t.library.answerDiscard('cancel');
    expect(t.library.dialog()).toBeNull();
    expect(t.sent.filter((i) => i.type === 'project.new')).toHaveLength(0);
    expect(t.engine.dirty).toBe(true);
  });

  it("Don't save throws the changes away", async () => {
    const t = setup();
    t.edit();
    await t.library.request('new');
    await t.library.answerDiscard('discard');
    expect(t.engine.name).toBe('Untitled');
    expect(await t.store.list()).toHaveLength(0);
  });

  it('Save, for a song with a place, saves and then goes on', async () => {
    const t = setup();
    await t.library.request('saveAs');
    await t.library.saveAs('Demo');
    t.edit();
    await t.library.request('new');
    await t.library.answerDiscard('save');
    expect(JSON.parse((await t.store.read('id-1')) ?? '{}')).toMatchObject({
      name: 'Demo',
      changes: 1,
    });
    expect(t.engine.name).toBe('Untitled');
  });

  it('Save, for a song with no place, asks for a name first and then goes on', async () => {
    const t = setup();
    t.edit();
    await t.library.request('open');
    await t.library.answerDiscard('save');
    expect(t.library.dialog()?.kind).toBe('saveAs');
    await t.library.saveAs('Kept');
    expect(t.library.dialog()).toMatchObject({ kind: 'open', projects: [{ name: 'Kept' }] });
  });

  it('cancelling the name question drops the pending New', async () => {
    const t = setup();
    t.edit();
    await t.library.request('new');
    await t.library.answerDiscard('save');
    t.library.cancel();
    await t.library.request('save');
    await t.library.saveAs('Other');
    expect(t.engine.name).toBe('Other'); // it did not go on to a new song
  });

  it('lists saved songs, newest first, and opens the one chosen', async () => {
    const t = setup();
    await t.library.request('saveAs');
    await t.library.saveAs('Old');
    await t.library.request('saveAs');
    await t.library.saveAs('New');
    await t.library.request('open');
    const dialog = t.library.dialog();
    expect(dialog?.kind === 'open' && dialog.projects.map((p) => p.name)).toEqual(['New', 'Old']);

    await t.library.openProject('id-1');
    expect(t.library.dialog()).toBeNull();
    expect(t.engine).toMatchObject({ name: 'Old', dirty: false, hasLocation: true });
    expect(t.library.openId()).toBe('id-1');

    t.edit();
    await t.library.request('save'); // goes back to the song that was opened
    expect(JSON.parse((await t.store.read('id-1')) ?? '{}')).toMatchObject({
      name: 'Old',
      changes: 1,
    });
  });

  it('a song that cannot be trusted is not opened, and the open one stays', async () => {
    const t = setup();
    await t.store.write(
      { id: 'x', name: 'Broken', updatedAt: 1, bytes: 10 },
      JSON.stringify({ name: 'Broken', bad: true }),
    );
    await t.library.request('open');
    await t.library.openProject('x');
    expect(t.notices.at(-1)).toEqual([2, expect.stringContaining('Could not open “Broken”')]);
    expect(t.engine.name).toBe('First song');
    expect(t.library.dialog()?.kind).toBe('open'); // pick another
  });

  it('a song that disappeared meanwhile says so and shows the list again', async () => {
    const t = setup();
    await t.library.request('open');
    await t.library.openProject('gone');
    expect(t.notices.at(-1)?.[1]).toContain('no longer stored');
    expect(t.library.dialog()?.kind).toBe('open');
  });

  it('deleting a song refreshes the list; deleting the open one leaves it unsaved', async () => {
    const t = setup();
    await t.library.request('saveAs');
    await t.library.saveAs('A');
    await t.library.request('saveAs');
    await t.library.saveAs('B');
    await t.library.request('open');
    await t.library.deleteProject('id-1');
    expect(t.library.dialog()).toMatchObject({ kind: 'open', projects: [{ name: 'B' }] });
    expect(t.engine.dirty).toBe(false);

    await t.library.deleteProject('id-2'); // the song that is open
    expect(t.engine).toMatchObject({ dirty: true, hasLocation: false });
    expect(t.library.openId()).toBeNull();
  });

  it('asks one thing at a time', async () => {
    const t = setup();
    await t.library.request('open');
    await t.library.request('new');
    await t.library.request('saveAs');
    expect(t.library.dialog()?.kind).toBe('open');
    t.library.cancel();
    expect(t.library.dialog()).toBeNull();
  });
});

describe('keeping unsaved work safe', () => {
  it('keeps what changed a moment after the last change, and drops it once saved', async () => {
    const t = setup();
    t.edit();
    await vi.advanceTimersByTimeAsync(1500);
    expect(await t.store.readAutosave()).toBeUndefined(); // not yet: more changes may follow
    t.edit();
    await vi.advanceTimersByTimeAsync(1500);
    expect(await t.store.readAutosave()).toBeUndefined(); // the delay starts again with each change
    await vi.advanceTimersByTimeAsync(600);
    expect(await t.store.readAutosave()).toMatchObject({ name: 'First song', forId: null });

    await t.library.request('saveAs');
    await t.library.saveAs('Kept');
    expect(await t.store.readAutosave()).toBeUndefined();
  });

  it('writes at once when the page is being hidden', async () => {
    const t = setup();
    t.edit();
    await t.library.flush();
    expect(await t.store.readAutosave()).toBeDefined();
    await vi.advanceTimersByTimeAsync(5000);
  });

  it('writes nothing for a clean song', async () => {
    const t = setup();
    await t.library.flush();
    await vi.advanceTimersByTimeAsync(5000);
    expect(await t.store.readAutosave()).toBeUndefined();
  });

  it('offers to restore after a crash, as unsaved work', async () => {
    const t = setup();
    await t.library.request('saveAs');
    await t.library.saveAs('Demo');
    t.edit();
    t.edit();
    await t.library.flush();

    // A new session (new engine, new library) over the same storage.
    const next = setup();
    const kept = await t.store.readAutosave();
    if (!kept) throw new Error('the crash left no autosave');
    await next.store.writeAutosave(kept);
    const second = new WebProjectLibrary({
      store: next.store,
      host: nextHost(next),
      autosaveDelayMs: 2000,
    });
    await second.start();
    expect(second.dialog()).toMatchObject({ kind: 'recover', name: 'Demo' });
    await second.answerRecover(true);
    expect(next.engine).toMatchObject({ name: 'Demo', changes: 2, dirty: true });
    expect(await next.store.readAutosave()).toBeDefined(); // still the safety net until saved
  });

  it('Discard forgets it', async () => {
    const t = setup();
    t.edit();
    await t.library.flush();
    const again = new WebProjectLibrary({ store: t.store, host: nextHost(t) });
    await again.start();
    await again.answerRecover(false);
    expect(await t.store.readAutosave()).toBeUndefined();
    expect(again.dialog()).toBeNull();
  });

  it('a recovered song whose saved copy was deleted comes back as a new, unsaved song', async () => {
    const t = setup();
    await t.store.writeAutosave({
      name: 'Orphan',
      json: JSON.stringify({ name: 'Orphan', changes: 3 }),
      at: 1,
      forId: 'deleted',
    });
    const again = new WebProjectLibrary({ store: t.store, host: nextHost(t) });
    await again.start();
    await again.answerRecover(true);
    expect(t.engine).toMatchObject({ name: 'Orphan', dirty: true, hasLocation: false });
    expect(again.openId()).toBeNull();
  });

  it('work that cannot be recovered is reported and forgotten', async () => {
    const t = setup();
    await t.store.writeAutosave({
      name: 'Bad',
      json: JSON.stringify({ name: 'Bad', bad: true, changes: 0 }),
      at: 1,
      forId: null,
    });
    const again = new WebProjectLibrary({ store: t.store, host: nextHost(t) });
    await again.start();
    await again.answerRecover(true);
    expect(t.notices.at(-1)?.[1]).toContain('could not be recovered');
    expect(await t.store.readAutosave()).toBeUndefined();
  });

  it('does not overwrite the saved work before the person has chosen', async () => {
    const t = setup();
    await t.store.writeAutosave({
      name: 'Earlier',
      json: JSON.stringify({ name: 'Earlier', changes: 1 }),
      at: 1,
      forId: null,
    });
    const again = new WebProjectLibrary({
      store: t.store,
      host: nextHost(t),
      autosaveDelayMs: 100,
    });
    await again.start();
    t.edit(); // the starter song changes behind the question
    again.projectState({ name: 'First song', dirty: true, hasLocation: false });
    await vi.advanceTimersByTimeAsync(1000);
    await flush();
    expect((await t.store.readAutosave())?.name).toBe('Earlier');
  });
});

// A host over the same fake engine, for a library that starts later.
function nextHost(t: ReturnType<typeof setup>): ProjectHost {
  return {
    exportProject: () =>
      Promise.resolve(JSON.stringify({ name: t.engine.name, changes: t.engine.changes })),
    importProject: (json, options) => {
      const file = JSON.parse(json) as { name: string; changes: number; bad?: boolean };
      if (file.bad)
        return Promise.resolve({ ok: false as const, error: 'The project file is not valid.' });
      Object.assign(t.engine, { name: file.name, changes: file.changes, ...options });
      return Promise.resolve({ ok: true as const });
    },
    markSaved: () => Promise.resolve(),
    markUnsaved: () => Promise.resolve(),
    send: () => undefined,
    notice: (level, message) => t.notices.push([level, message]),
  };
}
