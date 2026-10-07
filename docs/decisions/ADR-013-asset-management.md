# ADR-013: Project audio files: list and remove unused

**Status:** Accepted (2026-10-07). Builds on ADR-006 (project format) and ADR-007 (audio clips).

## Context
Importing and recording add files to the project's `audio/` folder and entries to
`Project::assets`. Nothing ever took one out, so deleted clips left dead entries and files behind,
and the user could not see what a project contained or which files were missing (guide §2.2,
"local asset management").

## Decision
- **Usage is derived, not stored.** `Project::assetUse(id)` counts audio clips, drum pads and the
  sampler that point at an asset. Nothing is persisted, so no schema change.
- **One command, `RemoveAssets`** (ADR-003): removes a set of assets in one undo step. It refuses
  an empty list, an unknown id, a duplicate-only list and any asset still in use, so the UI cannot
  orphan a clip. Undo reinserts each asset at its old index. Ids are never reused
  (`nextAssetId` is untouched).
- **The file stays on disk.** Undo, and history that reaches back through a removal, must still
  find the audio. Deleting files is permanent, so it is not done here (guide §24: never silently
  destroy user data). Orphan files remain in `audio/` and can be deleted by hand.
- **Bridge.** Event `project.assets` lists every asset (id, name, clip/pad counts, sampler flag,
  `missing`), sent on project changes and only when it differs from the last one. File existence
  is re-checked when audio loads or the project opens, not on every edit. Intents `asset.remove`
  (one asset) and `asset.removeUnused` (all unused, one undo step); `asset.locate` already existed.
- **UI.** An "Audio (N)" menu in the project header shows each file, what uses it, "Missing" with
  Locate…, and Remove for unused files, plus "Remove unused (n)".

## Alternatives
- *Delete the file with the asset*: breaks undo and is unrecoverable.
- *Garbage-collect unused assets on save*: surprising, and drops audio the user may want to reuse.
- *Store a reference count in the project*: can drift from the real references; derived is exact.

## Consequences
- Disk space is not reclaimed automatically. A later "delete unreferenced files" action could do it
  once the undo history is cleared (e.g. after save and close).
