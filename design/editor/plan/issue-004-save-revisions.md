# Issue #4: Preserve Edits During Save

Status: `implemented; automated validation complete`

Issue: [#4](https://github.com/abdes/DroidNet/issues/4).

Design owners: [documents-and-commands.md](../lld/documents-and-commands.md)
and [material-editor.md](../lld/material-editor.md).

## Original Findings

Issue #2 added a material source-identity check and a view-model edit/save gate.
Those changes prevented some stale completion paths, but service-level saves
could still overlap. Scene save completion cleared dirty state unconditionally.

The metadata change counter alone was insufficient: scene `MarkDirtyAsync`
returned immediately when the document was already dirty, and many commands
marked dirty only after awaiting live synchronization. Revision tracking must describe
authoring mutations, including edits made to an already-dirty document.

Scene persistence awaited folder/file resolution before serialization.
The dehydrated scene also retains the mutable explorer-layout collection.
Snapshot capture and its revision therefore need an explicit coherent boundary.

## Required Behavior

- Every committed authoring mutation advances its document revision, including
  undo/redo and edits to an already-dirty document. Revision advancement occurs
  with the mutation, before asynchronous live synchronization or notifications.
- Save captures a stable serialized snapshot and its revision before storage
  awaits. Disk I/O must not read mutable authoring collections later.
- Saves for the same document/persistence identity are serialized in the owning
  service, rather than depending on a particular UI caller. A queued save takes
  its snapshot when it acquires the save gate.
- Successful persistence acknowledges the captured revision. It never replaces
  current authoring data with the saved snapshot. Newer revisions remain dirty.
- Save failure leaves current data, history, and the last saved revision intact.
- Editing can continue while disk I/O runs. Save serialization must not require
  holding the material UI edit gate throughout the write.
- Save-and-close requires that the current authoring revision is saved. A
  successful write of an older revision does not authorize discarding newer edits.

Approved reporting decision: when snapshot A was written successfully but newer
edit B remains unsaved, report "Saved; newer changes remain unsaved" and retain
the dirty indicator. Treat actual persistence failure separately. This changes
the material behavior introduced for issue #2, which reported the whole
operation as failed.

## Implementation Work

1. Establish authoring/saved revision state at the material document and scene
   document ownership boundaries. Keep metadata and view-model dirty indicators
   consistent with those authoritative states.
2. Make material edit read/modify/commit atomic against other edits and close.
   Update both scalar and schema-property edit paths; neither may publish a
   source derived from an obsolete document snapshot.
3. Add per-document save coordination and a narrow injectable persistence
   boundary. Preserve the existing material atomic file replacement behavior.
   Ensure close/reopen cannot allow a prior save to acknowledge a new session.
4. Capture scene persistence data and revision coherently before storage work.
   Update command mutation/revision ordering and protect overlapping saves
   through the persistence owner. Check the explorer layout and other nested
   data for mutable references during snapshot capture.
5. Update scene/material save outcomes and UI completion according to the
   selected reporting policy. Preserve issue #2 close protection, save-failure
   diagnostics, authoring history, and explicit discard behavior.
6. Update the owning LLDs and implementation status with implemented behavior and
   verified evidence.

Likely projects: `Oxygen.Editor.MaterialEditor`, `Oxygen.Editor.Documents`,
`Oxygen.Editor.WorldEditor`, `Oxygen.Editor.Projects`, and the scene serialization
boundary in `Oxygen.Editor.World` as required by snapshot ownership.

## Validation

Use deterministic storage gates, not delays or filesystem timing assumptions.
Tests must inspect persisted data as well as current authoring state and dirty
indicators. Cover both material and scene paths:

- Capture A, hold the write, commit B, finish A: B remains in memory and dirty;
  persisted data matches A.
- Repeat with a document that was already dirty before B.
- Hold save A, request another save after edit B: writes cannot overlap or finish
  with an older snapshot overwriting the newer saved revision.
- Fail the held write after B: B and the previous saved revision remain intact.
- Save B afterward: persistence and dirty state converge correctly.
- Save-and-close remains vetoed when newer edits are unsaved.
- Scene mutations awaiting live sync and mutable explorer-layout edits do not
  escape revision/snapshot tracking.

Run relevant existing save/close tests and standard Debug editor MSBuild. Audit
compiler, analyzer, and IDE diagnostics, including informational suggestions, in
every modified C# file. Use only existing repository output paths. No engine
build is expected for this managed persistence change.

## Validation Results

- Standard Debug editor build passed with `MSBuild.exe /m` and the existing
  repository output paths; no engine build was needed.
- 232 tests passed: 27 material, 83 scene, 64 document/close, 13 schema, and
  45 project tests.
- Deterministic write gates cover edit/save, save/save, write failure, retry,
  material UI status and close preparation, immutable scene layout snapshots,
  multi-node property commits, and hierarchy changes pending live sync.
- Compiler/analyzer/IDE SARIF reports have no active diagnostics in any changed
  C# source or test file, including informational suggestions.
- Manual editor interaction replay was not performed for this change.
