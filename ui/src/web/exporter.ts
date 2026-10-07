import { audioPathsIn } from './audioFiles';
import type { DecodedAudio } from './audioFiles';

/** What the page tells the engine about an export: WAV format (0 16-bit, 1 24-bit, 2 float) and rate. */
export interface ExportOptions {
  readonly format: number;
  readonly sampleRate: number;
}

/** A finished export, waiting to be saved by the person. */
export interface ExportResult {
  readonly fileName: string;
  readonly bytes: Uint8Array;
  readonly seconds: number;
  readonly lufs: number;
  readonly truePeak: number;
  /** Audio files the song lists that could not be found: they are silent in the file. */
  readonly missingAudio: number;
}

/** The part of the export the interface uses. */
export interface ExportDownload {
  result(): ExportResult | null;
  subscribe(listener: () => void): () => void;
  /** Hands the file to the browser's download. Call from a click. */
  download(): void;
  dismiss(): void;
}

/** What the page can do with the worker that renders (a Worker, or a stand-in in tests). */
export interface RenderWorker {
  postMessage(message: unknown, transfer?: Transferable[]): void;
  terminate(): void;
  onmessage: ((event: { data: Record<string, unknown> }) => void) | null;
  onerror: ((event: { message?: string }) => void) | null;
}

export interface ExporterDeps {
  /** The open song as project-file text, from the live engine. */
  readonly exportProject: () => Promise<string>;
  /** The audio files of a song, decoded, and the paths that could not be found. */
  readonly collectAudio: (
    paths: readonly string[],
  ) => Promise<{ audio: { path: string; decoded: DecodedAudio }[]; missing: string[] }>;
  readonly createWorker: () => RenderWorker;
  readonly module: () => Promise<WebAssembly.Module>;
  readonly sampleRate: () => number;
  readonly projectName: () => string;
  readonly state: (running: boolean, progress: number) => void;
  readonly notice: (level: 0 | 1 | 2, message: string) => void;
  /** Hands bytes to the browser's download. */
  readonly save: (fileName: string, bytes: Uint8Array) => void;
}

/** A name for the file that every system accepts. */
export function exportFileName(projectName: string): string {
  const forbidden = '\\/:*?"<>|';
  const cleaned = Array.from(projectName, (char) =>
    char.charCodeAt(0) < 32 || forbidden.includes(char) ? '-' : char,
  )
    .join('')
    .replace(/\s+/g, ' ')
    .trim();
  const stem = cleaned.replace(/^\.+/, '').slice(0, 100).trim();
  return `${stem || 'Song'}.wav`;
}

/** "3:07", "0:42". */
export function clock(seconds: number): string {
  const whole = Math.max(0, Math.round(seconds));
  return `${String(Math.floor(whole / 60))}:${String(whole % 60).padStart(2, '0')}`;
}

/** What is said about a finished export: length, loudness, peak. */
export function describeExport(result: ExportResult): string {
  return `${clock(result.seconds)} · ${result.lufs.toFixed(1)} LUFS · peak ${result.truePeak.toFixed(1)} dBTP`;
}

/**
 * Exporting the song as a WAV file in the browser. The engine that plays is not touched: the song and
 * its audio go to a worker with an engine of its own, which renders a slice at a time (so progress
 * and Cancel work). The result waits for a click, since browsers only start a download from one.
 */
export class WebExporter implements ExportDownload {
  private readonly listeners = new Set<() => void>();
  private finished: ExportResult | null = null;
  private worker: RenderWorker | null = null;
  private abandonRender: (() => void) | null = null;
  private running = false;
  private cancelled = false;

  constructor(private readonly deps: ExporterDeps) {}

  result = () => this.finished;

  subscribe = (listener: () => void) => {
    this.listeners.add(listener);
    return () => {
      this.listeners.delete(listener);
    };
  };

  // Read through a method: it changes during the awaits below, which the compiler cannot see.
  private isCancelled(): boolean {
    return this.cancelled;
  }

  isRunning(): boolean {
    return this.running;
  }

  download(): void {
    if (this.finished) this.deps.save(this.finished.fileName, this.finished.bytes);
  }

  dismiss(): void {
    this.set(null);
  }

  /** The preparing, rendering and writing of one export. Resolves when it has finished or stopped. */
  async start(options: ExportOptions): Promise<void> {
    if (this.running) return;
    this.running = true;
    this.cancelled = false;
    this.set(null);
    this.deps.state(true, 0);
    try {
      const name = exportFileName(this.deps.projectName());
      const json = await this.deps.exportProject();
      const { audio, missing } = await this.deps.collectAudio(audioPathsIn(json));
      if (this.isCancelled()) return;
      this.deps.state(true, 0.05);

      const module = await this.deps.module();
      const outcome = await this.render({ module, json, audio, missing, options });
      if (this.isCancelled()) return;
      if ('failed' in outcome) {
        this.deps.notice(2, `Export failed. ${outcome.failed}`);
        return;
      }
      this.set({ ...outcome.done, fileName: name });
    } catch (error) {
      if (!this.isCancelled())
        this.deps.notice(
          2,
          `Export failed. ${error instanceof Error ? error.message : 'Something went wrong.'}`,
        );
    } finally {
      this.running = false;
      this.worker = null;
      this.deps.state(false, this.isCancelled() ? 0 : 1);
    }
  }

  /** Stops the render at once. */
  cancel(): void {
    if (!this.running) return;
    this.cancelled = true;
    this.worker?.terminate();
    this.worker = null;
    this.abandonRender?.(); // a terminated worker says nothing more: let the waiting export end
    this.deps.notice(0, 'Export cancelled.');
    this.running = false;
    this.deps.state(false, 0);
  }

  private render(job: {
    module: WebAssembly.Module;
    json: string;
    audio: { path: string; decoded: DecodedAudio }[];
    missing: string[];
    options: ExportOptions;
  }): Promise<{ done: Omit<ExportResult, 'fileName'> } | { failed: string }> {
    return new Promise((resolve) => {
      const worker = this.deps.createWorker();
      this.worker = worker;
      this.abandonRender = () => {
        resolve({ failed: 'cancelled' });
      };
      worker.onmessage = ({ data }) => {
        if (typeof data['progress'] === 'number') {
          // 0.05 was preparing; the engine's own progress runs to 0.85 and then it writes the file.
          this.deps.state(true, Math.min(0.95, 0.05 + 0.9 * (data['progress'] / 0.85)));
        } else if (data['done']) {
          resolve({ done: data['done'] as Omit<ExportResult, 'fileName'> });
        } else if (typeof data['failed'] === 'string') {
          resolve({ failed: data['failed'] });
        }
      };
      worker.onerror = (event) => {
        resolve({ failed: event.message || 'The export could not be started.' });
      };
      const files = job.audio.map(({ path, decoded }) => ({
        path,
        sampleRate: decoded.sampleRate,
        channels: decoded.channels,
      }));
      worker.postMessage(
        {
          module: job.module,
          sampleRate: this.deps.sampleRate(),
          json: job.json,
          audio: files,
          missing: job.missing,
          options: job.options,
        },
        files.flatMap((file) => file.channels.map((channel) => channel.buffer)),
      );
    });
  }

  private set(result: ExportResult | null) {
    this.finished = result;
    this.listeners.forEach((listener) => {
      listener();
    });
  }
}
