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
  presets, effects and mixer, buses, timeline editing, undo/redo, the starter song, saving, and
  audio files and export to WAV (below). Not in the browser yet, and each says so with a notice
  instead of failing silently: recording, accompaniment suggestions, device choice.
- **Export to WAV.** The same render as the desktop, off the audio thread: the page asks the live
  engine for the song, decodes the audio files it lists, and gives both to a **worker** that runs a
  second engine instance (the module is compiled once and shared). The worker renders a slice at a
  time (one second of music per step) so that progress is real and Cancel is immediate (the worker is
  terminated), then does what the desktop does: resample to the chosen rate, measure loudness (EBU
  R128, true peak), write the WAV (16-bit with dither, 24-bit or float). To make that possible the
  core's `renderSong` is built on a new stepwise `SongRender`, and the WAV writer has an in-memory
  twin (`encodeWav`) that produces the same bytes as `writeWav`, so the desktop's behaviour is
  unchanged and tested against the new path. The finished file waits behind a "Download" button:
  browsers only start a download from a click, and the render takes longer than a click stays valid.
  Audio files the page could not find are silent in the file and counted in the message. Limits: 30
  minutes, and a size estimate (render, resampled copy and file together) of 640 MB, which the engine
  checks before starting.
- **Audio files.** The browser decodes (`decodeAudioData`: WAV, MP3, AAC, Ogg, FLAC, whatever it can
  play) at the output's sample rate, so the engine needs no resampler; the page keeps the file as
  it was imported, in IndexedDB, under `audio/<random id>.<ext>`, which is what the song file lists
  (so a song file stays the desktop's format, and the id is the only link between a song and its
  files). Choosing a file (`track.importAudio`, `asset.locate`, `sampler.load`, `drums.loadPad`) and
  dropping one on an audio track are answered by the page, which opens the browser's file dialog
  from the click and then hands the decoded samples to the engine with the project edit it implies
  (a clip of the file's length, a relinked asset, the sampler's or a pad's sample) as one undo
  step. The engine keeps the samples, builds the overview and peaks the UI draws, feeds the sampler
  and the pads, and reports each file as loading, loaded or missing, exactly as the desktop does
  (the events come from the same shared code). Opening a song loads the files it lists one at a
  time; any the page cannot find or decode show as missing with Locate. Limits, enforced in the
  engine and again in the page: 10 minutes, two channels (more are cut to two), 200 MB per file,
  256 MB of decoded audio in memory (the engine says so when it is full). Stored files nobody uses
  (not a saved song, not the unsaved work kept for recovery, not the open song) are released, but
  only once an hour old, since another tab may be about to use them; if any saved song predates this
  bookkeeping nothing is released.
- **Saving and opening (IndexedDB).** The core turns the project into the desktop's project file
  text (`ap_project_export`) and back through the same strict parser (`parseProject`: version,
  keys, ranges, size and nesting limits), so a song stored in the browser is exactly a desktop
  project file and anything read back is untrusted. The module has no clock: the page passes the
  timestamp, and anything that is not a plausible time is replaced. The page keeps the songs
  (`ui/src/web`): a listing store and a contents store written in one transaction, so a song is
  saved whole or not at all, plus one autosave slot. `New`, `Open`, `Save` and `Save As` stay
  ordinary intents; the browser bridge answers them itself with the same questions the desktop
  asks ("Save changes to ...?", name, list of songs). Saving to the same name replaces that song;
  deleting asks twice. Work in progress is written to the autosave slot two seconds after the last
  change and when the page is hidden, cleared when saved, and offered back as unsaved work after a
  crash or a closed tab. Leaving a page with unsaved changes asks the browser to confirm. The
  page asks the browser to keep the songs when space is short (`storage.persist`), and says plainly
  that songs live in this browser on this device.

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
- Follow-ups, each small now that the shared layer exists: microphone input (`getUserMedia` into the worklet), Web MIDI, then mobile layouts.
- Latency is the Web Audio output latency plus one render quantum; keys and pads reach the engine in
  about one message-port hop. Measure it on real devices before promising a number.
- The engine ran at about 1 % of real time for the starter song in Node. Heavier projects and phones
  still need measuring.

## Verification
- Thirty Node tests (`apps/web/test`) run the real module through the same `engine.js` the worklet
  loads: starter song, playing, undo/redo, refusals, malformed and oversized messages, unicode,
  determinism across block sizes, bounded memory, and saving: export and reopen gives the same
  project and the same sound, dirty tracking, recovered-as-unsaved, and eleven kinds of untrusted
  project text refused without touching the open song; and audio: a file becomes a clip of its own
  length in one undo step and plays at its level, stereo stays stereo, a song opens before its audio
  arrives, missing and relinked files, the sampler and a pad playing their own samples, refused
  paths, rates, formats and lengths, and the memory cap; and export: each format and rate gives a
  valid WAV of the song's length, the file equals what playing the song produces (sample for sample,
  minus the engine's latency), progress only moves forward, cancel and restart, missing audio is
  silent and counted, the sampler and the pads are in the file. C++ tests check that a render taken in
  slices equals the whole render and that the in-memory WAV equals the file the desktop writes.
- Headless C++ tests of the shared code; UI tests of the bridge and the gate.
- A manual run in a browser (the UI with the real worklet: starter song loaded, playhead and meters
  moving; saving against the real IndexedDB: autosave written, restored after a reload, Save As,
  Open, Cmd+S over an opened song, delete of the open song; and audio: a 44.1 kHz WAV dropped on a
  lane becomes a clip with its waveform, survives a reload through Save and Open, and shows as
  missing with Locate when its stored file is removed; and export: the starter song exported through
  the Export menu in a real worker, with progress, a "Your song is ready" dialog, and a downloaded
  file that is a valid 24-bit stereo 48 kHz WAV with signal in it). It cannot be heard from the test environment, so listening on real browsers and devices
  is still a human check.
