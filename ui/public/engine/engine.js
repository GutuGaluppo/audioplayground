// Runs the core, compiled to WebAssembly (apps/web), from JavaScript. Plain ES module with no
// dependencies, so the AudioWorklet, the page and Node's tests all load the same file.
//
// The module is standalone (no Emscripten glue): its functions take pointers into its own memory.
// This class owns the little buffers it needs and hides the pointers.

const MAX_FRAMES = 4096; // the most one process() call may render

function encodeUtf8(text) {
  if (typeof TextEncoder !== 'undefined') return new TextEncoder().encode(text);
  // The worklet scope may not have TextEncoder.
  const bytes = [];
  for (const symbol of text) {
    let code = symbol.codePointAt(0);
    if (code < 0x80) bytes.push(code);
    else if (code < 0x800) bytes.push(0xc0 | (code >> 6), 0x80 | (code & 63));
    else if (code < 0x10000)
      bytes.push(0xe0 | (code >> 12), 0x80 | ((code >> 6) & 63), 0x80 | (code & 63));
    else
      bytes.push(
        0xf0 | (code >> 18),
        0x80 | ((code >> 12) & 63),
        0x80 | ((code >> 6) & 63),
        0x80 | (code & 63),
      );
  }
  return Uint8Array.from(bytes);
}

function decodeUtf8(bytes) {
  if (typeof TextDecoder !== 'undefined') return new TextDecoder().decode(bytes);
  let text = '';
  for (let i = 0; i < bytes.length;) {
    const b = bytes[i++];
    let code;
    if (b < 0x80) code = b;
    else if (b < 0xe0) code = ((b & 31) << 6) | (bytes[i++] & 63);
    else if (b < 0xf0) {
      code = ((b & 15) << 12) | ((bytes[i] & 63) << 6) | (bytes[i + 1] & 63);
      i += 2;
    } else {
      code =
        ((b & 7) << 18) |
        ((bytes[i] & 63) << 12) |
        ((bytes[i + 1] & 63) << 6) |
        (bytes[i + 2] & 63);
      i += 3;
    }
    text += String.fromCodePoint(code);
  }
  return text;
}

export class WasmEngine {
  /** Instantiates a compiled module. `module` is a WebAssembly.Module (or the .wasm bytes). */
  static async create(module, { sampleRate = 48000, blockSize = 128 } = {}) {
    const compiled =
      module instanceof WebAssembly.Module ? module : await WebAssembly.compile(module);
    const instance = await WebAssembly.instantiate(compiled, {
      env: { emscripten_notify_memory_growth() {} },
    });
    const engine = new WasmEngine(instance.exports);
    if (!engine.exports.ap_create(sampleRate, blockSize))
      throw new Error('The engine refused its settings');
    engine.sampleRate = sampleRate;
    engine.left = engine.exports.ap_alloc(MAX_FRAMES * 4);
    engine.right = engine.exports.ap_alloc(MAX_FRAMES * 4);
    engine.scratchSize = 4096;
    engine.scratch = engine.exports.ap_alloc(engine.scratchSize);
    return engine;
  }

  constructor(exports) {
    this.exports = exports;
    this.sampleRate = 0;
  }

  get memory() {
    return this.exports.memory.buffer;
  }

  /** Sends one UI message ({type, payload}). Returns false if the core dropped it as malformed. */
  intent(message) {
    const bytes = encodeUtf8(JSON.stringify(message));
    if (bytes.length > this.scratchSize) {
      this.exports.ap_free(this.scratch);
      this.scratchSize = Math.max(bytes.length, this.scratchSize * 2);
      this.scratch = this.exports.ap_alloc(this.scratchSize);
    }
    new Uint8Array(this.memory, this.scratch, bytes.length).set(bytes);
    return this.exports.ap_intent(this.scratch, bytes.length) === 1;
  }

  /** Renders left.length frames (at most 4096) of stereo audio into the two arrays. */
  process(left, right) {
    const frames = left.length;
    if (frames > MAX_FRAMES || right.length !== frames) throw new RangeError('Bad block size');
    this.exports.ap_process(this.left, this.right, frames);
    left.set(new Float32Array(this.memory, this.left, frames));
    right.set(new Float32Array(this.memory, this.right, frames));
  }

  /** The events since the last call, as objects: [{type, payload}, ...]. */
  takeEvents() {
    const pointer = this.exports.ap_events();
    const size = this.exports.ap_events_size();
    if (size <= 2) return [];
    return JSON.parse(decodeUtf8(new Uint8Array(this.memory, pointer, size)));
  }

  setOutputLatency(milliseconds) {
    this.exports.ap_set_output_latency(milliseconds);
  }
}

export { MAX_FRAMES };
