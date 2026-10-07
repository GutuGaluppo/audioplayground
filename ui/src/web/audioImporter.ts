import { audioPath, audioPathsIn, ImportError, MAX_FILE_BYTES } from './audioFiles';
import type { DecodedAudio, ImportTarget } from './audioFiles';
import type { ProjectStore } from './projectStore';

/** What the importer needs from the page and the engine. */
export interface ImporterDeps {
  readonly store: Pick<ProjectStore, 'readAudio' | 'writeAudio' | 'removeAudio'>;
  /** The browser's decoder, at the output's sample rate. */
  readonly decode: (bytes: ArrayBuffer) => Promise<DecodedAudio>;
  /** A command to the engine, with the buffers to hand over (not copy). */
  readonly send: (
    command: Record<string, unknown>,
    transfer?: Transferable[],
  ) => Promise<Record<string, unknown>>;
  readonly notice: (level: 0 | 1 | 2, message: string) => void;
  readonly newId: () => string;
  /** The browser's file dialog; call it from a click. */
  readonly choose: () => Promise<File | null>;
}

const isQuota = (error: unknown) =>
  error instanceof DOMException && error.name === 'QuotaExceededError';

/**
 * Gets audio into the browser version: a file is decoded by the browser, kept in storage as it was
 * (so a song finds it again), and handed to the engine as samples. Opening a song loads the files it
 * lists. One file at a time, so a big song does not hold several copies in memory at once.
 */
export class AudioImporter {
  /** The paths of the audio files the open song uses. */
  readonly inUse = new Set<string>();

  private chain: Promise<void> = Promise.resolve();

  constructor(private readonly deps: ImporterDeps) {}

  /** Asks for a file with the browser's dialog, then imports it. Call from a click. */
  async choose(target: ImportTarget): Promise<void> {
    const file = await this.deps.choose();
    if (file) await this.importFile(file, target);
  }

  importFile(file: File, target: ImportTarget): Promise<void> {
    return this.queue(async () => {
      const { store, decode, send, notice, newId } = this.deps;
      if (file.size > MAX_FILE_BYTES) {
        notice(2, `“${file.name}” is too large to import (the limit is 200 MB).`);
        return;
      }

      let bytes: ArrayBuffer;
      let decoded: DecodedAudio;
      try {
        bytes = await file.arrayBuffer();
        decoded = await decode(bytes);
      } catch (error) {
        notice(
          2,
          error instanceof ImportError
            ? `“${file.name}”: ${error.message}`
            : `“${file.name}” could not be read. It may be damaged, or in a format this browser cannot play.`,
        );
        return;
      }

      // Kept first: a song that lists a file it cannot find again would lose audio on reload.
      const path = audioPath(file.name, newId());
      try {
        await store.writeAudio(path, bytes);
      } catch (error) {
        notice(
          2,
          isQuota(error)
            ? 'The browser has no room left to keep that file. Delete songs you do not need and try again.'
            : 'That file could not be kept in the browser.',
        );
        return;
      }

      const reply = await send(
        {
          kind: 'audio',
          path,
          sampleRate: decoded.sampleRate,
          channels: decoded.channels,
          use: target,
          name: file.name,
        },
        decoded.channels.map((channel) => channel.buffer),
      );
      if (reply['ok'] !== true) {
        await store.removeAudio(path).catch(() => undefined);
        notice(
          2,
          typeof reply['error'] === 'string' ? reply['error'] : 'The audio could not be added.',
        );
        return;
      }
      this.inUse.add(path);
    });
  }

  /** A song was opened: load the audio files it lists, and tell the engine about any it cannot get. */
  loadSong(songJson: string): Promise<void> {
    return this.queue(async () => {
      const { store, decode, send, notice } = this.deps;
      const paths = audioPathsIn(songJson);
      this.inUse.clear();
      paths.forEach((path) => this.inUse.add(path));

      let missing = 0;
      for (const path of paths) {
        const bytes = await store.readAudio(path).catch(() => undefined);
        let loaded = false;
        if (bytes) {
          try {
            const decoded = await decode(bytes);
            const reply = await send(
              {
                kind: 'audio',
                path,
                sampleRate: decoded.sampleRate,
                channels: decoded.channels,
                use: { kind: 'existing' },
              },
              decoded.channels.map((channel) => channel.buffer),
            );
            loaded = reply['ok'] === true;
          } catch {
            loaded = false; // not decodable any more: shown as missing, like a file that is gone
          }
        }
        if (!loaded) {
          missing++;
          await send({ kind: 'audioMissing', path });
        }
      }
      if (missing > 0)
        notice(
          1,
          missing === 1
            ? 'One audio file in this song could not be found. Use Locate… on its clip to replace it.'
            : `${String(missing)} audio files in this song could not be found. Use Locate… on their clips to replace them.`,
        );
    });
  }

  /** The audio files a song lists, decoded, for an export; and the paths that could not be had. */
  async collect(
    paths: readonly string[],
    progress: (done: number) => void = () => undefined,
  ): Promise<{ audio: { path: string; decoded: DecodedAudio }[]; missing: string[] }> {
    const audio: { path: string; decoded: DecodedAudio }[] = [];
    const missing: string[] = [];
    for (const [index, path] of paths.entries()) {
      const bytes = await this.deps.store.readAudio(path).catch(() => undefined);
      try {
        if (!bytes) throw new Error('not stored');
        audio.push({ path, decoded: await this.deps.decode(bytes) });
      } catch {
        missing.push(path);
      }
      progress(index + 1);
    }
    return { audio, missing };
  }

  /** A new, empty song: none of the audio is in use any more. */
  newSong(): void {
    this.inUse.clear();
  }

  private queue(work: () => Promise<void>): Promise<void> {
    const run = this.chain.then(work).catch(() => {
      this.deps.notice(2, 'Something went wrong with that audio file.');
    });
    this.chain = run;
    return run;
  }
}
