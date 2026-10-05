# ADR-003: The C++ core is the single source of truth

**Status:** Accepted (2026-10-05)

## Context
With a React UI and a C++ engine, project state could live in two places. Divergence causes inconsistent undo, lost edits and hard-to-reproduce bugs.

## Decision
- The project model, commands and undo/redo live in `core/` (C++).
- The UI sends typed **intents** (e.g. `MoveClip{clipId, startTick}`) and receives **patches/events**. Zustand holds UI state and a read-only mirror of project state.
- Bridge messages and parameter descriptors are generated from JSON Schemas in `schema/` into both C++ and TypeScript. They are never hand-duplicated.
- Undo uses explicit commands (`apply` / `revert`). Continuous gestures (knob drags) coalesce into one undo step.

## Consequences
- Every user-visible state change goes through a command. That makes it testable headlessly and undoable by construction.
- The web target runs the same core compiled to WASM.
