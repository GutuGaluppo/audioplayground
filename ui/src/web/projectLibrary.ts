import type { Intent } from '../bridge/generated';
import { audioPathsIn } from './audioFiles';
import type { Autosave, ProjectInfo, ProjectStore } from './projectStore';

/** What the person is asked, one at a time. */
export type ProjectDialog =
  | { readonly kind: 'open'; readonly projects: readonly ProjectInfo[] }
  | { readonly kind: 'saveAs'; readonly name: string; readonly existing: readonly ProjectInfo[] }
  | { readonly kind: 'discard'; readonly name: string; readonly then: 'new' | 'open' }
  | { readonly kind: 'recover'; readonly name: string; readonly at: number };

export type DiscardChoice = 'save' | 'discard' | 'cancel';

/** The part of the library the interface uses. */
export interface ProjectLibrary {
  dialog(): ProjectDialog | null;
  subscribe(listener: () => void): () => void;
  openProject(id: string): Promise<void>;
  deleteProject(id: string): Promise<void>;
  saveAs(name: string): Promise<void>;
  answerDiscard(choice: DiscardChoice): Promise<void>;
  answerRecover(restore: boolean): Promise<void>;
  cancel(): void;
}

/** What the library needs from the engine and the page. */
export interface ProjectHost {
  exportProject(timestamp: string): Promise<string>;
  importProject(
    json: string,
    options: { dirty: boolean; hasLocation: boolean },
  ): Promise<{ ok: true } | { ok: false; error: string }>;
  markSaved(hasLocation: boolean): Promise<void>;
  markUnsaved(): Promise<void>;
  send(intent: Intent): void;
  notice(level: 0 | 1 | 2, message: string): void;
}

export interface OpenProject {
  readonly name: string;
  readonly dirty: boolean;
  readonly hasLocation: boolean;
}

export interface LibraryOptions {
  readonly store: ProjectStore;
  readonly host: ProjectHost;
  readonly now?: () => Date;
  /** How long after the last change an unsaved song is kept (ms). */
  readonly autosaveDelayMs?: number;
  readonly newId?: () => string;
  /** The audio files the open song uses: never let go of those. */
  readonly audioInUse?: () => Iterable<string>;
}

/** An audio file nobody uses is kept this long (ms) first: another tab may be about to use it. */
export const AUDIO_GRACE_MS = 60 * 60 * 1000;

export const MAX_NAME_LENGTH = 128;

const timestamp = (date: Date) => date.toISOString().replace(/\.\d{3}Z$/, 'Z');

function defaultId(): string {
  const random = globalThis.crypto as Crypto | undefined;
  return typeof random?.randomUUID === 'function'
    ? random.randomUUID()
    : `${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 10)}`;
}

function describe(error: unknown): string {
  if (error instanceof DOMException && error.name === 'QuotaExceededError')
    return 'The browser has no room left to store songs. Delete some saved songs and try again.';
  return error instanceof Error && error.message ? error.message : 'Something went wrong.';
}

/**
 * Saving and opening songs in the browser: what New, Open, Save and Save As do, the "save changes?"
 * question, and keeping unsaved work safe. The engine turns a song into text and back (and refuses
 * what it cannot trust); this class decides when, and where the text is kept. Work is done one thing
 * at a time, so two clicks cannot interleave.
 */
export class WebProjectLibrary implements ProjectLibrary {
  private readonly store: ProjectStore;
  private readonly host: ProjectHost;
  private readonly now: () => Date;
  private readonly autosaveDelay: number;
  private readonly newId: () => string;
  private readonly audioInUse: () => Iterable<string>;
  private readonly listeners = new Set<() => void>();

  private current: ProjectDialog | null = null;
  private state: OpenProject | null = null;
  private currentId: string | null = null;
  private afterSave: (() => Promise<void>) | null = null;
  private autosaveTimer: ReturnType<typeof setTimeout> | undefined;
  private chain: Promise<void> = Promise.resolve();

  constructor(options: LibraryOptions) {
    this.store = options.store;
    this.host = options.host;
    this.now = options.now ?? (() => new Date());
    this.autosaveDelay = options.autosaveDelayMs ?? 2000;
    this.newId = options.newId ?? defaultId;
    this.audioInUse = options.audioInUse ?? (() => []);
  }

  // --- What the interface reads ---------------------------------------------------------------

  dialog = () => this.current;

  subscribe = (listener: () => void) => {
    this.listeners.add(listener);
    return () => {
      this.listeners.delete(listener);
    };
  };

  isDirty(): boolean {
    return this.state?.dirty ?? false;
  }

  /** The id of the saved song that is open, if it came from or went to storage. */
  openId(): string | null {
    return this.currentId;
  }

  // --- What the engine reports ----------------------------------------------------------------

  /** Every project.state event from the engine. */
  projectState(state: OpenProject): void {
    const wasDirty = this.state?.dirty ?? false;
    this.state = state;
    if (state.dirty) this.scheduleAutosave();
    else if (wasDirty) void this.dropAutosave();
  }

  /** Checks for work an earlier session left unsaved. Call once the engine is ready. */
  start(): Promise<void> {
    return this.queue(async () => {
      const autosave = await this.store.readAutosave().catch(() => undefined);
      if (autosave) this.show({ kind: 'recover', name: autosave.name, at: autosave.at });
      await this.collectAudio();
    });
  }

  /** Writes the unsaved work now (the page is being hidden or closed). */
  flush(): Promise<void> {
    clearTimeout(this.autosaveTimer);
    this.autosaveTimer = undefined;
    return this.queue(() => this.writeAutosave());
  }

  // --- The four commands ----------------------------------------------------------------------

  /** project.new, project.open, project.save, project.saveAs from the interface. */
  request(kind: 'new' | 'open' | 'save' | 'saveAs'): Promise<void> {
    return this.queue(async () => {
      if (this.current || !this.state) return; // one question at a time
      switch (kind) {
        case 'new':
        case 'open':
          if (this.state.dirty) this.show({ kind: 'discard', name: this.state.name, then: kind });
          else await this.proceed(kind);
          break;
        case 'save':
          if (this.currentId) await this.saveCurrent();
          else await this.askName();
          break;
        case 'saveAs':
          await this.askName();
          break;
      }
    });
  }

  // --- The answers ----------------------------------------------------------------------------

  openProject(id: string): Promise<void> {
    return this.queue(async () => {
      const info = (await this.store.list()).find((p) => p.id === id);
      const json = await this.store.read(id).catch(() => undefined);
      if (!info || json === undefined) {
        this.host.notice(2, 'That song is no longer stored in this browser.');
        await this.showOpen();
        return;
      }
      const result = await this.host.importProject(json, { dirty: false, hasLocation: true });
      if (!result.ok) {
        this.host.notice(2, `Could not open “${info.name}”. ${result.error}`);
        return; // the open song stays as it was; the list stays up
      }
      this.currentId = id;
      this.show(null);
      await this.dropAutosave();
    });
  }

  deleteProject(id: string): Promise<void> {
    return this.queue(async () => {
      try {
        await this.store.remove(id);
      } catch (error) {
        this.host.notice(2, describe(error));
        return;
      }
      if (id === this.currentId) {
        this.currentId = null;
        await this.host.markUnsaved(); // the open song now exists only on screen
      }
      await this.collectAudio();
      if (this.current?.kind === 'open') await this.showOpen();
    });
  }

  saveAs(rawName: string): Promise<void> {
    return this.queue(async () => {
      const name = rawName.trim().slice(0, MAX_NAME_LENGTH);
      if (!name || this.current?.kind !== 'saveAs') return;

      // A song with this name is replaced rather than duplicated: names are what people see.
      const same = (await this.store.list()).find(
        (p) => p.name.toLowerCase() === name.toLowerCase(),
      );
      const id = same?.id ?? this.newId();
      if (name !== this.state?.name) this.host.send({ type: 'project.rename', payload: { name } });

      if (await this.store_(id, name)) {
        this.currentId = id;
        this.show(null);
        const next = this.afterSave;
        this.afterSave = null;
        if (next) await next();
      }
    });
  }

  answerDiscard(choice: DiscardChoice): Promise<void> {
    return this.queue(async () => {
      const dialog = this.current;
      if (dialog?.kind !== 'discard') return;
      this.show(null);
      if (choice === 'cancel') return;
      const proceed = () => this.proceed(dialog.then);
      if (choice === 'discard') return proceed();
      if (this.currentId) {
        if (await this.saveCurrent()) await proceed();
      } else {
        this.afterSave = proceed;
        await this.askName();
      }
    });
  }

  answerRecover(restore: boolean): Promise<void> {
    return this.queue(async () => {
      if (this.current?.kind !== 'recover') return;
      this.show(null);
      const autosave = await this.store.readAutosave().catch(() => undefined);
      if (!restore || !autosave) return this.dropAutosave();

      const stillThere =
        autosave.forId !== null && (await this.store.read(autosave.forId)) !== undefined;
      const result = await this.host.importProject(autosave.json, {
        dirty: true,
        hasLocation: stillThere,
      });
      if (!result.ok) {
        this.host.notice(2, `The unsaved work could not be recovered. ${result.error}`);
        return this.dropAutosave();
      }
      this.currentId = stillThere ? autosave.forId : null;
    });
  }

  cancel(): void {
    this.afterSave = null;
    this.show(null);
  }

  // --- Inside -----------------------------------------------------------------------------------

  private queue(work: () => Promise<void>): Promise<void> {
    const run = this.chain.then(work).catch((error: unknown) => {
      this.host.notice(2, describe(error));
    });
    this.chain = run;
    return run;
  }

  private show(dialog: ProjectDialog | null) {
    this.current = dialog;
    this.listeners.forEach((listener) => {
      listener();
    });
  }

  private async proceed(kind: 'new' | 'open') {
    if (kind === 'open') return this.showOpen();
    this.host.send({ type: 'project.new', payload: {} });
    this.currentId = null;
    await this.dropAutosave();
  }

  private async showOpen() {
    const projects = (await this.store.list()).sort((a, b) => b.updatedAt - a.updatedAt);
    this.show({ kind: 'open', projects });
  }

  private async askName() {
    this.show({
      kind: 'saveAs',
      name: this.state?.name ?? 'Untitled',
      existing: await this.store.list(),
    });
  }

  private async saveCurrent(): Promise<boolean> {
    if (!this.currentId || !this.state) return false;
    return this.store_(this.currentId, this.state.name);
  }

  // Stores the open song under `id`; tells the engine it is saved. False (with a notice) on failure.
  private async store_(id: string, name: string): Promise<boolean> {
    try {
      const json = await this.host.exportProject(timestamp(this.now()));
      await this.store.write(
        {
          id,
          name,
          updatedAt: this.now().getTime(),
          bytes: json.length,
          audio: audioPathsIn(json),
        },
        json,
      );
    } catch (error) {
      this.host.notice(2, `Could not save “${name}”. ${describe(error)}`);
      return false;
    }
    await this.host.markSaved(true);
    await this.dropAutosave();
    await this.collectAudio();
    this.host.notice(0, `Saved “${name}” in this browser.`);
    void this.keepStorage();
    return true;
  }

  // Asks the browser not to clear stored songs when it is short of space. It may say no.
  private async keepStorage() {
    try {
      await navigator.storage.persist();
    } catch {
      // not supported, or refused: the songs are stored either way
    }
  }

  private scheduleAutosave() {
    clearTimeout(this.autosaveTimer);
    this.autosaveTimer = setTimeout(() => {
      this.autosaveTimer = undefined;
      void this.queue(() => this.writeAutosave());
    }, this.autosaveDelay);
  }

  private async writeAutosave() {
    if (!this.state?.dirty || this.current?.kind === 'recover') return;
    const json = await this.host.exportProject(timestamp(this.now()));
    const record: Autosave = {
      name: this.state.name,
      json,
      at: this.now().getTime(),
      forId: this.currentId,
      audio: audioPathsIn(json),
    };
    await this.store.writeAutosave(record);
  }

  private async dropAutosave() {
    clearTimeout(this.autosaveTimer);
    this.autosaveTimer = undefined;
    await this.store.clearAutosave().catch(() => undefined);
  }

  /**
   * Lets go of audio files no song uses: not the saved songs, not the unsaved work kept for recovery,
   * not the open song, and not anything stored in the last hour (another tab may be about to use it).
   * Does nothing if it cannot be sure, i.e. if any song was saved before audio was tracked.
   */
  async collectAudio(): Promise<void> {
    try {
      const songs = await this.store.list();
      const autosave = await this.store.readAutosave();
      if (songs.some((song) => !song.audio) || (autosave && !autosave.audio)) return;

      const keep = new Set<string>(this.audioInUse());
      for (const song of songs) song.audio?.forEach((path) => keep.add(path));
      autosave?.audio?.forEach((path) => keep.add(path));

      const now = this.now().getTime();
      for (const file of await this.store.listAudio())
        if (!keep.has(file.path) && now - file.at > AUDIO_GRACE_MS)
          await this.store.removeAudio(file.path);
    } catch {
      // storage trouble: the files stay, which is the safe side
    }
  }
}
