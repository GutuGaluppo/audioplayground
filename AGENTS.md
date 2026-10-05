# AGENTS.md

Instructions for coding agents (Claude Code, Codex) and human contributors.

## Read first
1. `docs/DEVELOPMENT_GUIDE.md`: product principles, real-time rules, agent contract (§32–§34).
2. `docs/IMPLEMENTATION_PLAN.md`: roadmap, task backlog (§5), per-PR Definition of Done (§6).
3. `docs/decisions/`: accepted ADRs. Do not contradict an ADR without writing a new one.

## Layout
```text
core/          C++ headless core (model, engine, DSP). No UI, no WebView, no windowing.
apps/desktop/  JUCE shell: window, WebView host, audio device manager.
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

# UI
pnpm install
pnpm check                          # typecheck + lint + format:check + test
pnpm ui:build
```

## Non-negotiable rules
- The audio thread never allocates, locks, does I/O, logs or parses (guide §8). Answer the §34 checklist in every audio PR.
- Positions are `int64` ticks (PPQ 960) or samples, never floating point (ADR-004).
- Project state changes go through core commands (ADR-003). The UI sends intents only.
- Project files and imported audio are untrusted input (ADR-006).
- Never rename parameter IDs or change persisted schemas without a migration and an ADR.
- New dependency: justify it (guide §29), pin version and hash, and add it to `docs/THIRD_PARTY_LICENSES.md`.
- First-party C++ builds with warnings as errors. Do not silence warnings to pass CI.
- Do not claim success without running the relevant commands above. If a command cannot run, say exactly why.
