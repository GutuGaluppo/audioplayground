/** The most one imported file may take, as stored (the engine has its own, tighter, limits). */
export const MAX_FILE_BYTES = 200 * 1024 * 1024;
/** Longer sounds are refused: the engine keeps all audio in memory. */
export const MAX_SECONDS = 600;

/** What a file decoded to: one or two channels at the output's sample rate, ready for the engine. */
export interface DecodedAudio {
  readonly channels: Float32Array[];
  readonly sampleRate: number;
  readonly seconds: number;
}

/** What an imported file is for. */
export type ImportTarget =
  | { readonly kind: 'clip'; readonly track: number; readonly ticks: number }
  | { readonly kind: 'relink'; readonly asset: number }
  | { readonly kind: 'sampler' }
  | { readonly kind: 'pad'; readonly pad: number };

/** A reason an import cannot go ahead, worded for the person. */
export class ImportError extends Error {}

/**
 * The name a file is kept under inside songs: `audio/<id><extension>`. The id is random, so the
 * name says nothing about the file, collides with nothing and stays within what the core accepts
 * for an asset path (letters, digits, '-', '_', '.').
 */
export function audioPath(fileName: string, id: string): string {
  const extension = /\.([A-Za-z0-9]{1,5})$/.exec(fileName)?.[1]?.toLowerCase();
  const safeId = id.replace(/[^A-Za-z0-9-]/g, '').slice(0, 64) || 'audio';
  return `audio/${safeId}${extension ? `.${extension}` : ''}`;
}

/** A random id for a stored file. `crypto.randomUUID` needs a secure page; elsewhere a random string does. */
export function randomId(): string {
  const random = globalThis.crypto as Crypto | undefined;
  return typeof random?.randomUUID === 'function'
    ? random.randomUUID()
    : `${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 12)}`;
}

/** The paths of the audio files a song file lists. Anything that is not a song file gives none. */
export function audioPathsIn(songJson: string): string[] {
  try {
    const song: unknown = JSON.parse(songJson);
    const assets =
      typeof song === 'object' && song !== null ? (song as { assets?: unknown }).assets : null;
    if (!Array.isArray(assets)) return [];
    return (assets as unknown[]).flatMap((asset) => {
      const path =
        typeof asset === 'object' && asset !== null ? (asset as { path?: unknown }).path : null;
      return typeof path === 'string' ? [path] : [];
    });
  } catch {
    return [];
  }
}

/**
 * Decodes a file with the browser's own decoder (whatever it can play: WAV, MP3, AAC, Ogg, FLAC...),
 * at the output's sample rate. Keeps at most two channels, as the desktop does.
 */
export async function decodeAudio(
  context: Pick<BaseAudioContext, 'decodeAudioData'>,
  bytes: ArrayBuffer,
): Promise<DecodedAudio> {
  // decodeAudioData takes ownership of what it is given; the caller still needs the file as it was.
  const buffer = await context.decodeAudioData(bytes.slice(0));
  const seconds = buffer.duration;
  if (buffer.length < 1 || buffer.numberOfChannels < 1)
    throw new ImportError('The file has no sound in it.');
  if (seconds > MAX_SECONDS) throw new ImportError('The file is longer than 10 minutes.');
  const channels = Array.from({ length: Math.min(buffer.numberOfChannels, 2) }, (_, index) =>
    buffer.getChannelData(index).slice(),
  );
  return { channels, sampleRate: buffer.sampleRate, seconds };
}

/**
 * Asks for an audio file with the browser's file dialog. Call it straight from a click or key press
 * (browsers refuse to open the dialog otherwise). Resolves null if the dialog is cancelled.
 */
export function chooseAudioFile(): Promise<File | null> {
  return new Promise((resolve) => {
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = 'audio/*,.wav,.mp3,.m4a,.aac,.ogg,.oga,.opus,.flac,.aif,.aiff';
    input.style.display = 'none';
    const done = (file: File | null) => {
      input.remove();
      resolve(file);
    };
    input.addEventListener('change', () => {
      done(input.files?.[0] ?? null);
    });
    input.addEventListener('cancel', () => {
      done(null);
    });
    document.body.append(input);
    input.click();
  });
}
