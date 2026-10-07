// Exports the song as a WAV file in a worker of its own, so the music keeps playing meanwhile
// (ADR-016). The page sends the song, the audio it uses and the options; the worker opens them in
// a second engine, renders a slice at a time and reports progress.
import { WasmEngine } from './engine.js';

const yieldToWorker = () => new Promise((resolve) => setTimeout(resolve, 0));

self.onmessage = async ({ data }) => {
  try {
    const engine = await WasmEngine.create(data.module, { sampleRate: data.sampleRate });
    const opened = engine.importProject(data.json, {});
    if (!opened.ok) throw new Error(opened.error);

    for (const file of data.audio) {
      const loaded = engine.loadAudio({ ...file, use: { kind: 'existing' } });
      if (!loaded.ok) engine.audioMissing(file.path);
    }
    for (const path of data.missing) engine.audioMissing(path);

    const begun = engine.exportBegin(data.options);
    if (!begun.ok) throw new Error(begun.error);

    for (;;) {
      const step = engine.exportStep();
      if (step.state === 'failed') throw new Error(step.error);
      if (step.state === 'done') break;
      self.postMessage({ progress: step.progress });
      await yieldToWorker(); // lets the progress reach the page, and a cancel take effect
    }

    const result = engine.exportResult();
    engine.exportRelease();
    self.postMessage({ done: result }, [result.bytes.buffer]);
  } catch (error) {
    self.postMessage({ failed: String(error && error.message ? error.message : error) });
  }
};
