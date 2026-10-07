# ADR-015: Synth presets and drum pattern presets

**Status:** Accepted (2026-10-07). Extends guide §17 (presets are plain, source-controlled data).

## Context
Presets existed only for effects (`presets/effects.json`). Someone who opens the synth gets one
sound, and an empty step grid is a blank page. Guide §17 asks for plain structured presets and the
product principle is "immediate sound".

## Decision
- **Two files, same shape as effects:** `presets/synth.json` (`processor: "synth"`, `parameters`
  by short name, units of `schema/parameters.json`, a missing value takes its default) and
  `presets/drums.json` (`processor: "drums"`, `pattern`: pad name → 16 characters, `x` hit, `.`
  rest; pads left out are silent). `version: 1`, ids are stable once released.
- **A synth preset is one undo step.** The intent `synth.setPreset` carries the ten synth values in
  a fixed order (`ui/src/params/synthPreset.ts`, mirrored in `WebUiHost`) and the host applies them
  as one command group after checking every range. Ten `param.set` messages would have been ten
  undo steps (merging only joins edits of the same parameter).
- **A drum preset replaces the pattern.** `drums.setPattern` carries 16 bitmasks (bit n = step n).
  `model::withDrumPattern` removes the notes on the 16 pad pitches and writes the new ones; other
  notes stay. Like `drums.setStep` it targets the clip given, else the pattern at the playhead's
  bar, creating the drum track and clip when there is none (one undo step).
- **Pad names, not numbers, in the file** (kick, snare, closedHat…), so a pattern reads like a
  drum chart and survives a reordering mistake. The names follow the 16 factory kit slots, which
  every kit shares (ADR on kits), so patterns keep their meaning across kits.
- **Nothing is persisted new.** Presets only write ordinary project state (parameters, clip notes).
- **Licensing.** All values and patterns are original and use common rhythmic idioms; no sample or
  third-party preset content (guide §30).

## Alternatives
- *Ten `param.set` messages from the UI*: ten undo steps.
- *Pattern presets as step-by-step `drums.setStep` messages*: up to 256 messages, and the
  playhead can move while a pattern is being created.
- *Presets inside the code*: against §17.

## Consequences
- Presets are not tied to a kit, so "808 trap" is a pattern, not a sound; kit choice stays separate.
- Velocity is fixed at 1.0 in patterns. Accents would need a richer format (a version bump).
