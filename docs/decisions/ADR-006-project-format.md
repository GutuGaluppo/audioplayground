# ADR-006: Project format v1

**Status:** Accepted (2026-10-05)

## Decision
```text
MySong.playground/
├── project.json   # schemaVersion (int), model data
├── audio/         # assets referenced by ID + content hash
├── cache/         # disposable (waveform peaks, resampled audio)
└── autosave/      # rotating, last 5
```
- `schemaVersion` is an integer. Migrations are pure `vN → vN+1` functions tested against fixtures of every released version. A failed migration never overwrites the original.
- Saves are atomic: write a temp file, `fsync`, rename, and keep a `.bak` of the previous version.
- `project.json` is **untrusted input**: strict schema validation, size and count limits, `NaN`/`Inf` rejection, and asset paths that must resolve inside the project folder.
- The project's `sampleRate` is only the default export rate. Assets keep their native rate and are resampled off the audio thread.
- No SQLite in the MVP.
- No codename in persisted identifiers (guide §37).
