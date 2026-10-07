# AGENTS.md

Instructions for coding agents (Claude Code, Codex) and human contributors.

## Read first

1. `docs/DEVELOPMENT_GUIDE.md`: product principles, real-time rules, agent contract (§32–§34).
2. `docs/IMPLEMENTATION_PLAN.md`: roadmap, task backlog (§5), per-PR Definition of Done (§6).
3. `docs/decisions/`: accepted ADRs. Do not contradict an ADR without writing a new one.

## Layout

```text
core/          C++ headless core (model, engine, DSP, shared intent handling in core/host). No UI, no WebView, no windowing.
apps/desktop/  JUCE shell: window, WebView host, audio device manager.
apps/web/      Browser host: the core as WebAssembly, run by an AudioWorklet (ADR-016).
ui/            React + TypeScript UI (shared by desktop and web).
tests/         C++ tests (Catch2).
schema/        JSON Schemas — single source for bridge messages and parameters.
presets/       Built-in presets (JSON).
docs/          Guide, plan, ADRs, third-party licences.
```

## Commands

```bash
# C++ (configure once; JUCE and Catch2 download pinned by SHA-256)
cmake --preset dev                  # add -DFETCHCONTENT_SOURCE_DIR_JUCE=<path> to use a local JUCE 9.0.3
cmake --build --preset dev
ctest --preset dev

# C++ with AddressSanitizer + UBSan (tests only)
cmake --preset asan && cmake --build --preset asan && ctest --preset asan

# RealtimeSanitizer (Clang >= 20; runs in CI — Apple Clang does not ship it)
cmake --preset rtsan && cmake --build --preset rtsan && ctest --preset rtsan

# CPU budget of the reference project (8 tracks, 16 voices, every effect; plan §3.2). Release only.
cmake --preset release && cmake --build --preset release --target ap_benchmarks && ./build/release/tests/ap_benchmarks

# Regenerate golden audio references after an intended sound change (review + listen before committing)
AP_UPDATE_GOLDENS=1 ./build/dev/tests/ap_unit_tests "[golden]"

# Browser (needs Emscripten: brew install emscripten, or an emsdk shell)
pnpm web:wasm                       # core -> ui/public/engine/ap_web.wasm
pnpm web:test                       # the module through the same engine.js the worklet loads
pnpm web:build                      # ui/dist-web: the page plus the engine; try it with ?engine=wasm in pnpm ui:dev

# UI
pnpm install
pnpm check                          # typecheck + lint + format:check + test
pnpm ui:build
```

## Non-negotiable rules

- The audio thread never allocates, locks, does I/O, logs or parses (guide §8). Answer the §34 checklist in every audio PR.
- Mark every function reachable from the audio callback `AP_NONBLOCKING` (`ap/core/RealtimeSafety.h`). Clang then rejects violations at compile time (`-Wfunction-effects`) and RTSan catches them at run time.
- Positions are `int64` ticks (PPQ 960) or samples, never floating point (ADR-004).
- Project state changes go through core commands (ADR-003). The UI sends intents only.
- Project files and imported audio are untrusted input (ADR-006).
- Timeline: one track per instrument, clip offsets in flicks, drum patterns are note clips (ADR-007).
- Recording: the input opens only when a track is armed, never monitored; takes are journalled for crash recovery (ADR-008).
- Effects: fixed per-track chain, settings in the project, constant engine latency (`Engine::getOutputLatency`) that offline renders and recordings compensate (ADR-009).
- Bridge integration tests (`tests/desktop/BridgeIntegrationTests.cpp`) build `WebUiHost` without its web view (`AP_HEADLESS_UI=1`): they send intents as JSON and read the emitted events. A new intent that changes the project needs a case there. Keep WebView-only code inside `#if !AP_HEADLESS_UI`.
- A UI intent that only edits the project or drives the engine belongs in `core/host/IntentApplier` (shared by the desktop and the browser), with its test in `tests/unit/IntentApplierTests.cpp`. `WebUiHost` keeps what needs the desktop platform (devices, files, recording, export); the browser host says "not available yet" for those.
- Never rename parameter IDs or change persisted schemas without a migration and an ADR.
- New dependency: justify it (guide §29), pin version and hash, and add it to `docs/THIRD_PARTY_LICENSES.md`.
- First-party C++ builds with warnings as errors. Do not silence warnings to pass CI.
- Do not claim success without running the relevant commands above. If a command cannot run, say exactly why.

## Claude Code / Codex rules

- Compact (/compact) after every PR. If you cannot compact, say exactly why. Use /clear between tasks.
