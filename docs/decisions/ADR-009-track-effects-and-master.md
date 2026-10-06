# ADR-009: Track effect chains, engine latency and the master limiter

**Status:** Accepted (2026-10-06)

## Context
Task 024 puts the effects of Tasks 018–023 on tracks and protects the output with a limiter. The
global parameter system (Task 006) holds one value per parameter, but effects are per track. The
distortion's oversampling (38 samples) and the limiter's lookahead add latency, which must never
pull tracks out of time with each other or with recordings.

## Decision
- **Fixed chain per track** (guide §9): EQ → Compressor → Filter → Distortion → Delay → Reverb,
  each with an on/off switch. No generic routing in the MVP.
- **Settings live in the project** as `Track::effects`: per effect, `enabled` plus values in the
  order of `schema/parameters.json` → `effects`, which also generates the C++ and TypeScript
  descriptors (ranges, steps, units). Ids (`eq`, `eq.lowGain`, ...) are persistence contracts.
  One command, `SetTrackEffect`, changes an effect; a gesture is one undo step. Project files
  without `effects` load with every effect off (schema v1 is still a draft, ADR-006).
- **Processor state outlives graphs.** The engine keeps one `TrackChain` per track id (created on
  the message thread, prepared in `Engine::prepare`), and every published graph points at it.
  Chains of deleted tracks are freed with the last graph that used them, off the audio thread
  (ADR-005). Graph updates carry only settings; the audio thread hands them to the chain each
  block (cheap: targets of smoothed values).
- **Switching** crossfades over 20 ms. A switched-off effect costs nothing and is reset when it
  comes back (a delay never replays stale echoes). Effect tails keep running when the transport
  stops.
- **Constant engine latency.** Every chain always runs the distortion's latency-matched dry path,
  so each track is delayed by exactly 38 samples whatever is switched on; sources without a track
  (metronome, test tone, an instrument without a track) go through the same delay. Then the master
  limiter adds its lookahead (1 ms + 4 samples). Nothing ever shifts in time when effects change.
  `Engine::getOutputLatency()` reports the total (~90 samples at 48 kHz, ~1.9 ms); offline renders
  drop it from the front, live notes and audio recordings add it to the device latency.
- **Master limiter**: stereo-linked, true peak estimated at 4x, ceiling −1 dBTP, lookahead with
  hold-then-average gain (no overshoot, no stepping), 100 ms release. Always on; the full-scale
  clamp after it stays as a last safety.
- **Presets** are plain JSON in `presets/effects.json` (guide §17), validated by a test; the UI
  applies them as ordinary `SetTrackEffect` edits.

## Consequences
- Output latency grows by ~1.9 ms at 48 kHz, for everything, always.
- Memory: about 1 MB per track (delay line and reverb), allocated when the track first appears in a
  graph.
- Loud mixes are now limited instead of clipped (the drum groove golden was regenerated: same
  timing, gain reduced only where it used to clip).
- Effect parameters are not automatable yet, and each settings change publishes a new graph;
  fine at UI rates.
