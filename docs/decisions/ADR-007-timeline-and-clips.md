# ADR-007: Timeline, tracks and clips

**Status:** Accepted (2026-10-06)

## Context
Task 017 turns the instruments into songs: tracks on a timeline holding audio clips and note
clips. The three instruments (synth, sampler, drum machine) exist once each, with their sound
settings stored as project parameters. Making them per-track instances would need track-scoped
parameters, which is a larger change than the MVP needs.

## Decision
- **Tracks.** An audio track holds audio clips. An instrument track holds note clips and plays one
  of the three instruments (`InstrumentKind`). The MVP allows **at most one track per
  instrument**, so the instrument's sound settings stay global and the instrument's output is the
  track's output (volume, pan, mute and solo apply to it). Live playing goes through the same
  track; an instrument without a track plays at unity gain.
- **Clips.** Every clip has a timeline `start` and `length` in ticks (ADR-004).
  - *Audio clips* reference a project asset and a `sourceOffset` in **flicks**
    (1/705 600 000 s, which divides every common sample rate exactly). Audio is not time-stretched:
    changing the tempo moves where clips start, never which audio they play.
  - *Note clips* hold notes in content time (ticks from the content start), a `contentOffset`
    (left trim) and an optional `loopLength`: when set, the content repeats for the whole clip
    length. This is how a one-bar drum pattern fills four bars.
- **The drum pattern is a note clip.** The 16-step grid edits the notes of a drum clip
  (sixteenth-note steps, GM pad notes 36–51). The project no longer stores a global pattern.
- **Edits are non-destructive and undoable.** `AddClip`, `RemoveClip`, `SetClip` (move, trim,
  resize, loop, note edits; gestures merge) and `SplitClip`. Edits that need several commands
  (e.g. creating the drum track and its first clip) are grouped into one undo step.
- **Overlapping clips** on one track all play. Replacing or truncating overlaps is post-MVP.
- **Playback.** The render graph snapshot (ADR-005) carries each track's clips. Note events are
  scheduled sample-accurately inside each block, and instruments render in chunks between events.
  Audio clips get short fades at their edges and wherever playback jumps (start, stop, seek,
  loop), so nothing clicks.

## Consequences
- Two synth tracks with different sounds need per-track instruments: a later, additive change
  (the model already stores the instrument per track).
- Schema v1 changes (still a draft, ADR-006): `drums.steps` is removed, and tracks gain
  `instrument` and `clips`.
