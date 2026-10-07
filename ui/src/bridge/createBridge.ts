import { createNativeBridge, createSimulatedBridge } from './bridge';
import type { Bridge } from './bridge';
import { createWasmBridge } from './wasmBridge';

/**
 * The desktop app's engine inside its window; otherwise the browser's WebAssembly engine when asked
 * for (?engine=wasm, or a web build, VITE_ENGINE=wasm); otherwise the stand-in used to develop the
 * interface.
 */
export function createBridge(): Bridge {
  if (window.__JUCE__) return createNativeBridge(window.__JUCE__);
  const requested =
    new URLSearchParams(window.location.search).get('engine') ?? import.meta.env.VITE_ENGINE;
  return requested === 'wasm' ? createWasmBridge() : createSimulatedBridge();
}
