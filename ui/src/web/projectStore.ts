/** What the browser keeps about a saved song, without its contents. */
export interface ProjectInfo {
  readonly id: string;
  readonly name: string;
  /** Milliseconds since the epoch. */
  readonly updatedAt: number;
  readonly bytes: number;
  /** The audio files it uses (their paths in the song), so a file nobody uses can be let go. */
  readonly audio?: readonly string[];
}

/** An audio file kept for songs: the file as it was imported, found again by the path the song uses. */
export interface StoredAudio {
  readonly path: string;
  readonly at: number;
}

/** The last unsaved state, kept as it changes so a closed tab or a crash loses almost nothing. */
export interface Autosave {
  readonly name: string;
  readonly json: string;
  readonly at: number;
  /** The saved song it was a change of, if any. */
  readonly forId: string | null;
  readonly audio?: readonly string[];
}

/** Where songs live in the browser. Everything the page stores is untrusted when read back. */
export interface ProjectStore {
  list(): Promise<ProjectInfo[]>;
  read(id: string): Promise<string | undefined>;
  write(info: ProjectInfo, json: string): Promise<void>;
  remove(id: string): Promise<void>;
  readAutosave(): Promise<Autosave | undefined>;
  writeAutosave(autosave: Autosave): Promise<void>;
  clearAutosave(): Promise<void>;
  readAudio(path: string): Promise<ArrayBuffer | undefined>;
  writeAudio(path: string, bytes: ArrayBuffer): Promise<void>;
  removeAudio(path: string): Promise<void>;
  listAudio(): Promise<StoredAudio[]>;
}

const DATABASE = 'audio-playground';
const VERSION = 2;
const INFO = 'projects';
const DATA = 'files';
const AUTOSAVE = 'autosave';
const AUDIO = 'audio';
const AUDIO_INFO = 'audioInfo';
const AUTOSAVE_KEY = 'current';

function request<T>(req: IDBRequest<T>): Promise<T> {
  return new Promise((resolve, reject) => {
    req.onsuccess = () => {
      resolve(req.result);
    };
    req.onerror = () => {
      reject(req.error ?? new Error('IndexedDB request failed'));
    };
  });
}

function finished(tx: IDBTransaction): Promise<void> {
  return new Promise((resolve, reject) => {
    tx.oncomplete = () => {
      resolve();
    };
    tx.onerror = () => {
      reject(tx.error ?? new Error('IndexedDB transaction failed'));
    };
    tx.onabort = () => {
      reject(tx.error ?? new DOMException('The save was aborted', 'AbortError'));
    };
  });
}

const isInfo = (value: unknown): value is ProjectInfo =>
  typeof value === 'object' &&
  value !== null &&
  typeof (value as ProjectInfo).id === 'string' &&
  typeof (value as ProjectInfo).name === 'string' &&
  typeof (value as ProjectInfo).updatedAt === 'number' &&
  typeof (value as ProjectInfo).bytes === 'number';

const isAutosave = (value: unknown): value is Autosave =>
  typeof value === 'object' &&
  value !== null &&
  typeof (value as Autosave).name === 'string' &&
  typeof (value as Autosave).json === 'string' &&
  typeof (value as Autosave).at === 'number' &&
  (typeof (value as Autosave).forId === 'string' || (value as Autosave).forId === null);

/**
 * Songs in IndexedDB. The listing (names and dates) and the contents are separate stores, so
 * showing the list never reads megabytes. A write stores both in one transaction: a song is
 * either saved whole or not at all. Rejects if the browser has no IndexedDB (some private modes).
 */
export async function openIndexedDbStore(factory?: IDBFactory): Promise<ProjectStore> {
  const idb = factory ?? (typeof indexedDB === 'undefined' ? undefined : indexedDB);
  if (!idb) throw new Error('This browser cannot store songs.');

  const db = await new Promise<IDBDatabase>((resolve, reject) => {
    const open = idb.open(DATABASE, VERSION);
    open.onupgradeneeded = () => {
      // Version 1 had songs and the autosave; version 2 adds the audio files (contents and a
      // listing, so looking at what is stored never reads the files).
      const created = open.result;
      for (const [name, options] of [
        [INFO, { keyPath: 'id' }],
        [DATA, undefined],
        [AUTOSAVE, undefined],
        [AUDIO, undefined],
        [AUDIO_INFO, { keyPath: 'path' }],
      ] as const)
        if (!created.objectStoreNames.contains(name)) created.createObjectStore(name, options);
    };
    open.onsuccess = () => {
      resolve(open.result);
    };
    open.onerror = () => {
      reject(open.error ?? new Error('IndexedDB could not be opened'));
    };
    open.onblocked = () => {
      reject(new Error('Another tab is using an older version of the song storage.'));
    };
  });

  return {
    async list() {
      const rows: unknown[] = await request(db.transaction(INFO).objectStore(INFO).getAll());
      return rows.filter(isInfo);
    },
    async read(id) {
      const text: unknown = await request(db.transaction(DATA).objectStore(DATA).get(id));
      return typeof text === 'string' ? text : undefined;
    },
    async write(info, json) {
      const tx = db.transaction([INFO, DATA], 'readwrite');
      tx.objectStore(INFO).put(info);
      tx.objectStore(DATA).put(json, info.id);
      await finished(tx);
    },
    async remove(id) {
      const tx = db.transaction([INFO, DATA], 'readwrite');
      tx.objectStore(INFO).delete(id);
      tx.objectStore(DATA).delete(id);
      await finished(tx);
    },
    async readAutosave() {
      const value: unknown = await request(
        db.transaction(AUTOSAVE).objectStore(AUTOSAVE).get(AUTOSAVE_KEY),
      );
      return isAutosave(value) ? value : undefined;
    },
    async writeAutosave(autosave) {
      const tx = db.transaction(AUTOSAVE, 'readwrite');
      tx.objectStore(AUTOSAVE).put(autosave, AUTOSAVE_KEY);
      await finished(tx);
    },
    async clearAutosave() {
      const tx = db.transaction(AUTOSAVE, 'readwrite');
      tx.objectStore(AUTOSAVE).delete(AUTOSAVE_KEY);
      await finished(tx);
    },
    async readAudio(path) {
      const bytes: unknown = await request(db.transaction(AUDIO).objectStore(AUDIO).get(path));
      return bytes instanceof ArrayBuffer ? bytes : undefined;
    },
    async writeAudio(path, bytes) {
      const tx = db.transaction([AUDIO, AUDIO_INFO], 'readwrite');
      tx.objectStore(AUDIO).put(bytes, path);
      tx.objectStore(AUDIO_INFO).put({ path, at: Date.now() } satisfies StoredAudio);
      await finished(tx);
    },
    async removeAudio(path) {
      const tx = db.transaction([AUDIO, AUDIO_INFO], 'readwrite');
      tx.objectStore(AUDIO).delete(path);
      tx.objectStore(AUDIO_INFO).delete(path);
      await finished(tx);
    },
    async listAudio() {
      const rows: unknown[] = await request(
        db.transaction(AUDIO_INFO).objectStore(AUDIO_INFO).getAll(),
      );
      return rows.filter(
        (row): row is StoredAudio =>
          typeof row === 'object' &&
          row !== null &&
          typeof (row as StoredAudio).path === 'string' &&
          typeof (row as StoredAudio).at === 'number',
      );
    },
  };
}

/** The same interface in memory: for tests, and for a page that may not store anything. */
export function createMemoryStore(): ProjectStore & {
  readonly failNextWrite: (error: Error) => void;
  readonly setClock: (now: number) => void;
} {
  const infos = new Map<string, ProjectInfo>();
  const files = new Map<string, string>();
  const sounds = new Map<string, { bytes: ArrayBuffer; at: number }>();
  let autosave: Autosave | undefined;
  let failure: Error | undefined;
  let clock = 0;
  return {
    failNextWrite: (error) => {
      failure = error;
    },
    setClock: (now) => {
      clock = now;
    },
    list: () => Promise.resolve([...infos.values()]),
    read: (id) => Promise.resolve(files.get(id)),
    write: (info, json) => {
      if (failure) {
        const error = failure;
        failure = undefined;
        return Promise.reject(error);
      }
      infos.set(info.id, info);
      files.set(info.id, json);
      return Promise.resolve();
    },
    remove: (id) => {
      infos.delete(id);
      files.delete(id);
      return Promise.resolve();
    },
    readAutosave: () => Promise.resolve(autosave),
    writeAutosave: (value) => {
      autosave = value;
      return Promise.resolve();
    },
    clearAutosave: () => {
      autosave = undefined;
      return Promise.resolve();
    },
    readAudio: (path) => Promise.resolve(sounds.get(path)?.bytes),
    writeAudio: (path, bytes) => {
      if (failure) {
        const error = failure;
        failure = undefined;
        return Promise.reject(error);
      }
      sounds.set(path, { bytes, at: clock });
      return Promise.resolve();
    },
    removeAudio: (path) => {
      sounds.delete(path);
      return Promise.resolve();
    },
    listAudio: () => Promise.resolve([...sounds].map(([path, { at }]) => ({ path, at }))),
  };
}
