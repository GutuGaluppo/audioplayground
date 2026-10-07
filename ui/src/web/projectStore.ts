/** What the browser keeps about a saved song, without its contents. */
export interface ProjectInfo {
  readonly id: string;
  readonly name: string;
  /** Milliseconds since the epoch. */
  readonly updatedAt: number;
  readonly bytes: number;
}

/** The last unsaved state, kept as it changes so a closed tab or a crash loses almost nothing. */
export interface Autosave {
  readonly name: string;
  readonly json: string;
  readonly at: number;
  /** The saved song it was a change of, if any. */
  readonly forId: string | null;
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
}

const DATABASE = 'audio-playground';
const VERSION = 1;
const INFO = 'projects';
const DATA = 'files';
const AUTOSAVE = 'autosave';
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
      const created = open.result;
      created.createObjectStore(INFO, { keyPath: 'id' });
      created.createObjectStore(DATA);
      created.createObjectStore(AUTOSAVE);
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
  };
}

/** The same interface in memory: for tests, and for a page that may not store anything. */
export function createMemoryStore(): ProjectStore & {
  readonly failNextWrite: (error: Error) => void;
} {
  const infos = new Map<string, ProjectInfo>();
  const files = new Map<string, string>();
  let autosave: Autosave | undefined;
  let failure: Error | undefined;
  return {
    failNextWrite: (error) => {
      failure = error;
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
  };
}
