/// <reference types="vite/client" />

interface ImportMetaEnv {
  /** 'wasm' in the browser build (see .env.web). */
  readonly VITE_ENGINE?: string;
}
