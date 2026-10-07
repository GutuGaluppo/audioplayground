// The AudioWorklet that runs the engine in the browser (ADR-016). It receives the compiled
// WebAssembly module and the page's intents over its message port, renders audio one block at a
// time, and posts the events the core produced back to the page.
import { WasmEngine } from './engine.js';

const EVENTS_EVERY_BLOCKS = 16; // meters and playhead, about 60 Hz at 48 kHz and 128-frame blocks

class ApEngineProcessor extends AudioWorkletProcessor {
  constructor(options) {
    super();
    this.engine = null;
    this.queue = [];
    this.blocks = 0;
    this.spare = new Float32Array(128);

    this.port.onmessage = ({ data }) => {
      if (data.intent) {
        if (this.engine) {
          this.engine.intent(data.intent);
          this.flush(); // an edit is answered at once, not at the next meter tick
        } else if (this.queue.length < 1000) {
          this.queue.push(data.intent);
        }
      } else if (typeof data.latencyMs === 'number' && this.engine) {
        this.engine.setOutputLatency(data.latencyMs);
        this.flush();
      }
    };

    WasmEngine.create(options.processorOptions.module, { sampleRate, blockSize: 128 })
      .then((engine) => {
        this.engine = engine;
        for (const intent of this.queue) engine.intent(intent);
        this.queue = [];
        this.flush();
        this.port.postMessage({ ready: true });
      })
      .catch((error) => {
        this.port.postMessage({ failed: String(error && error.message ? error.message : error) });
      });
  }

  flush() {
    const events = this.engine.takeEvents();
    if (events.length > 0) this.port.postMessage({ events });
  }

  process(_inputs, outputs) {
    const output = outputs[0];
    if (!this.engine || output.length === 0) return true;

    const left = output[0];
    let right = output[1];
    if (!right) {
      if (this.spare.length < left.length) this.spare = new Float32Array(left.length);
      right = this.spare.subarray(0, left.length);
    }
    this.engine.process(left, right);

    if (++this.blocks >= EVENTS_EVERY_BLOCKS) {
      this.blocks = 0;
      this.flush();
    }
    return true;
  }
}

registerProcessor('ap-engine', ApEngineProcessor);
