# ADR-012: Delay tempo sync

**Status:** Accepted (2026-10-07). Extends ADR-009 (fixed per-track effect chain).

## Context
The delay's time is in milliseconds (guide §12.4: "add tempo sync after the basic version is
stable"). Musicians think in note values, and a repeat that drifts off the beat when the tempo
changes is the first thing they notice.

## Decision
- **One new parameter, `delay.sync`**, appended after `mix` (existing parameter ids and order are
  untouched). Values 0–8: 0 = free (the Time knob, milliseconds), then 1/16, 1/8 triplet, 1/8,
  1/8 dotted, 1/4 triplet, 1/4, 1/4 dotted, 1/2. Default 0, so every existing project and preset
  sounds the same.
- **Resolved in the engine, not in the DSP.** `Delay` still takes milliseconds. `TrackChain::set`
  receives the transport tempo each block and `delaySettings` turns a note value into
  `beats × 60000 / bpm`, clamped to the delay line (1 ms – 2 s). A slow tempo with a long note
  therefore plays at 2 s rather than failing.
- **Tempo changes glide.** The delay already glides its time over 150 ms, so a tempo change bends
  the repeats like tape instead of clicking. No new smoothing is needed.
- **Offline == real time.** Export configures the engine with the project tempo (same function as
  live), so exports match.
- **Persistence.** `sync` is stored with the other delay values; files without it load with 0. The
  schema is still a draft (ADR-006), so no migration is needed; after release a new parameter is
  additive in the same way.
- **UI.** The effect card shows a Sync menu; when a note value is chosen it replaces the Time knob
  (the effective time follows the tempo). A "Dotted eighth (follows tempo)" preset is added; the
  old fixed 120 BPM preset keeps its id.

## Alternatives
- *Sync as a separate boolean plus a note value*: two parameters for one idea, and an invalid
  state (sync on, no note).
- *Resolve the tempo inside `fx::Delay`*: couples the DSP to the transport; DSP stays headless
  (guide §5).
- *Per-clip or per-position tempo*: the project has one constant tempo in the MVP (decision D4).

## Consequences
- Tempo automation, if added later, flows through the same per-block call.
- A synced delay at a tempo below 30 BPM (1/2 note) is capped at 2 s and stops following the beat.
