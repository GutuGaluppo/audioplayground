# ADR-016: The browser host (WebAssembly)

**Status:** Accepted (2026-10-07). Builds on ADR-002 (one React UI), ADR-003 (the core is the source
of truth) and ADR-005 (real-time state). First milestone: an instrument you can play in the browser.

## Context
The plan keeps the web as a target after the desktop MVP (guide §19): the same DSP, the same UI,
audio through Web Audio. The desktop app reaches the core through `WebUiHost`, which is tied to JUCE
(timers, dialogs, devices, files). The browser has none of that, and re-implementing the intent
handling in TypeScript would break ADR-003: two engines to keep in agreement.

## Decision
- **The core, unchanged, compiled to a standalone `.wasm`** with Emscripten (`apps/web`, built by
  `tools/web/build.sh`, about 0.5 MB). No JavaScript glue and no WASI: the module imports a single
  no-op function, exports a small C interface (`ap_create`, `ap_intent`, `ap_process`,
  `ap_events`...) and exchanges UTF-8 JSON through memory it hands out. Native WebAssembly
  exceptions (the core reports bad project data by throwing).
- **The engine runs inside an `AudioWorklet`** (`ui/public/engine/worklet.js`), one render quantum
  (128 frames) at a time. Intents go page → worklet and events go worklet → page over the worklet's
  message port; meters and playhead are posted about every 16 blocks, and an edit is answered at
  once. Because nothing is shared between threads, **no `SharedArrayBuffer`, and so no COOP/COEP
  headers**: the page can be hosted anywhere. (The plan assumed shared memory would be needed; it
  is not for this milestone. When recording or large audio buffers need it, it can be added behind
  the same bridge.)
- **One body of intent code for both hosts.** The project-editing intents (transport, parameters,
  presets, tracks, buses, clips, drum patterns, undo) and the events that describe the project moved
  out of `WebUiHost` into the core (`core/host`): `IntentApplier`, `Snapshots`, `ProjectEditor` and
  the JUCE-free `DocumentEditor`. The desktop `Session` is a `ProjectEditor`; the browser uses a
  `DocumentEditor`. The desktop host shrank by about 560 lines and every shared intent now has a
  headless test (`IntentApplierTests`). A new intent that edits the project is added once.
- **One schema, one codec.** The browser compiles the desktop's generated `BridgeCodec.cpp`
  unchanged, against a small stand-in for the part of `juce_core` it uses
  (`apps/web/src/compat`), so a second codec cannot drift. Everything from the page is validated
  exactly as on the desktop (strict keys, ranges, lengths), plus a 1 MiB cap per message.
- **The UI is the same React app.** `Bridge.kind` says which engine answers (`native`, `wasm`,
  `simulated`). `createBridge()` picks the desktop engine inside the app, the browser engine with
  `?engine=wasm` or in a web build (`vite build --mode web`), and the stand-in otherwise. Browsers
  play nothing before a click, so the browser bridge exposes an `audio` gate and the UI shows a
  "Start audio" card until the page may play. The web build adds `'wasm-unsafe-eval'` to the CSP
  (WebAssembly only, not script `eval`) and carries the engine files; the desktop bundle carries
  none of them.
- **Milestone 1 scope:** transport and tempo, synth and drums played from the keyboard and pads,
  presets, effects and mixer, buses, timeline editing, undo/redo, the starter song. Not in the
  browser yet, and each says so with a notice instead of failing silently: saving and opening
  projects, importing audio, recording, the sampler, export, accompaniment suggestions, device
  choice.

## Alternatives
- *Emscripten's own AudioWorklet support (`-sAUDIO_WORKLET`)*: needs Wasm workers and shared
  memory, hence cross-origin isolation, for no benefit at this stage.
- *The engine on the main thread with a `ScriptProcessorNode`*: deprecated and glitches whenever the
  page is busy.
- *A TypeScript port of the intent handling*: two implementations of ADR-003.
- *A second generated codec for the browser*: drifts from the desktop one.

## Consequences
- The browser and the desktop behave the same for everything in the shared layer by construction;
  what differs is only what each platform provides (files, devices, recording).
- Follow-ups, each small now that the shared layer exists: project storage in IndexedDB (the
  project serializer already compiles to WebAssembly), audio import through `decodeAudioData`,
  export, microphone input (`getUserMedia` into the worklet), Web MIDI, then mobile layouts.
- Latency is the Web Audio output latency plus one render quantum; keys and pads reach the engine in
  about one message-port hop. Measure it on real devices before promising a number.
- The engine ran at about 1 % of real time for the starter song in Node. Heavier projects and phones
  still need measuring.

## Verification
- Ten Node tests (`apps/web/test`) run the real module through the same `engine.js` the worklet
  loads: starter song, playing, undo/redo, refusals, malformed and oversized messages, unicode,
  determinism across block sizes, bounded memory.
- Headless C++ tests of the shared code; UI tests of the bridge and the gate.
- A manual run in a browser (the UI with the real worklet: starter song loaded, playhead and meters
  moving). It cannot be heard from the test environment, so listening on real browsers and devices
  is still a human check.
