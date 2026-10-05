# ADR-004: Time representation

**Status:** Accepted (2026-10-05)

## Context
Floating-point positions drift and produce off-by-one-sample bugs in snapping, looping and clip edits.

## Decision
- Musical positions and durations: `int64` **ticks**, PPQ = 960.
- Audio positions: `int64` **samples**. Offsets inside an audio asset are in samples **at the asset's own sample rate**.
- Tempo and time signature are **fixed per project** for the MVP (decision D4). The schema stores them so that tempo/meter changes can be added later through a migration.
- All tick ↔ sample conversion goes through one tested function with explicit rounding.
- Persisted positions are never `float`/`double`.

## Consequences
- Rendering at any device sample rate is deterministic.
- Adding tempo changes later is limited to the conversion function plus a schema migration.
