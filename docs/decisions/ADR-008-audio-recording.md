# ADR-008: Audio recording

**Status:** Accepted (2026-10-06)

## Context
Task 015 records the microphone (or any audio input) into clips. ADR-005 already says the audio
thread writes to a ring buffer and a disk thread writes the file. This ADR fixes the rest: when the
input opens, how a take is placed on the timeline, and what survives a crash.

## Decision
- **Arming.** An audio track is armed with its ● button; at most one at a time. Arming opens up to
  two input channels (the device restarts); disarming closes them. The input is never opened at
  launch, so the microphone permission is only asked for when the user wants to record (guide
  §25). Record with an armed track records the input as well as the played notes.
- **No input monitoring.** The input is metered but never reaches the output (no feedback through
  speakers). Monitoring with a headphone warning can come later.
- **Capture.** `engine::InputCapture` copies the input into a lock-free SPSC ring (20 s) while the
  transport plays. A take is one contiguous stretch of musical time: it ends at stop, and also when
  playback jumps (loop wrap, seek), the ring overflows (the disk is too slow), or the device
  restarts. What was captured until then is kept. Takes are limited to 10 minutes (what the app
  loads).
- **Placement.** A frame captured at transport position `p` was played when the musician heard
  `p − latency`, with latency = input latency + output latency + two buffers (the round trip the
  device reports). The clip starts on the first whole tick at or after that point (or at 0) and the
  sub-tick remainder, plus anything recorded before 0 (count-in), is skipped with the clip's
  `sourceOffset` in flicks. Every frame plays exactly where it was heard. A manual offset
  (loopback calibration) can be added later.
- **Files.** Takes are 32-bit float WAV at the device rate in `audio/<id>-take.wav`, named
  "Take N". The take becomes an asset and a clip in one undo step. Overlapping takes all play
  (overdub, ADR-007).
- **Crash safety.** The disk thread rewrites the WAV header twice a second, and a journal
  (`audio/<file>.wav.journal`, JSON: file, track, start, sourceOffset, name) is written as soon as
  the take begins. At the next start, each journal's WAV gets its header repaired from the file
  length and the take is put back on the timeline ("Recover recording"). Journals are untrusted
  input: strict parsing, safe asset paths only, and a journal is removed once handled, whatever the
  outcome. A normal quit first adds the take to the project, so the "save changes?" prompt covers it.

## Consequences
- Arming while playing stops playback (the device restarts). Acceptable for the MVP.
- Device latency reports are approximate on some hardware (e.g. Bluetooth); the acceptance test
  (< 1 ms error) needs a loopback measurement, and a manual offset if a device misreports.
- Journals are a new file next to recordings, not part of `project.json`, so the project schema is
  unchanged.
