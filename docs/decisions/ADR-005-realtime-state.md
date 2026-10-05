# ADR-005: Real-time state sharing

**Status:** Accepted (2026-10-05)

## Decision
| Data | Mechanism |
|---|---|
| Continuous parameters | `std::atomic<float>` target per parameter; smoothing inside the processor |
| Structural changes (tracks, clips, loaded samples) | Immutable graph **snapshot** built on the message thread and published through an atomic pointer. Retired snapshots go to a release queue drained off the audio thread |
| Audio → UI (meters, playhead) | Lock-free SPSC FIFO, read by the UI at 30–60 Hz |
| Recording | Audio thread writes to a ring buffer; a disk thread writes the file incrementally |

- No mutex, allocation, I/O or logging on the audio thread. This is enforced with RealtimeSanitizer (`-fsanitize=realtime`) in CI from Task 003.

## Consequences
- The audio thread never waits on the UI. Structural edits cost one allocation off-thread per change, which is acceptable at UI rates.
