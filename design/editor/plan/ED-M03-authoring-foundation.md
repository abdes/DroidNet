# ED-M03 Authoring Foundation Detailed Plan

Status: `validated`

## 1. Purpose

Make the editor authoring core reliable enough for later inspector, material,
content-pipeline, and parity milestones. ED-M03 establishes scene document
commands, dirty state, undo/redo, shared selection, scene explorer hierarchy
operations, save/reopen, and scoped operation results.

ED-M03 is not a component-inspector milestone. It creates the command/document
foundation that ED-M04 component inspectors must use.

## 2. PRD Traceability

| ID | Coverage |
| --- | --- |
| `GOAL-001` | The V0.1 editor can create usable scene content through real UI workflows. |
| `GOAL-002` | Scene authoring state is persisted and can be reopened. |
| `GOAL-006` | Authoring failures are visible and diagnosable. |
| `REQ-004` | Scene documents open, activate, save, close, and reopen. |
| `REQ-005` | Partial: command rails support component-bearing primitive/light creation; full component add/remove/edit inspector workflow is ED-M04. |
| `REQ-006` | Dirty state reflects successful authoring commands and save results. |
| `REQ-007` | ED-M03-supported scene data saves and reopens. |
| `REQ-008` | ED-M03-supported mutations request live sync where current engine APIs support it. |
| `REQ-009` | Partial: ED-M03 supports command-created primitive and light nodes; full V0.1 component completion continues in ED-M04. |
| `REQ-022` | Save/command/sync failures produce visible operation results or diagnostics. |
| `REQ-023` | Authoring/runtime sync failures produce useful logs. |
| `REQ-024` | Diagnostics distinguish document, scene authoring, and live-sync failures. |
| `REQ-026` | Partial: command-created scene content requests embedded runtime sync; full preview sync coverage is ED-M04/ED-M08. |
| `REQ-036` | Scene persistence follows the scene authoring model LLD. |
| `REQ-037` | Supported ED-M03 scene data saves and reopens without manual repair. |
| `SUCCESS-002` | Supported scene edits survive save/reopen. |
| `SUCCESS-009` | IMPLEMENTATION_STATUS records concise validation evidence. |

## 3. Required LLDs

Only these LLDs gate ED-M03:

- [documents-and-commands.md](../lld/documents-and-commands.md)
- [scene-authoring-model.md](../lld/scene-authoring-model.md)
- [scene-explorer.md](../lld/scene-explorer.md)
- [diagnostics-operation-results.md](../lld/diagnostics-operation-results.md)

The LLD review loop must close before implementation starts. `property-
inspector.md`, `live-engine-sync.md`, and `runtime-integration.md` are
supporting context only unless implementation touches their contracts.

## 4. Scope

ED-M03 includes:

- `ISceneDocumentCommandService` command-shaped entry point.
- Shared document-scoped scene selection service/model.
- Scene explorer wiring to commands for create, rename, delete, reparent, and
  layout/folder operations.
- Quick-add primitive and light creation through commands.
- Dirty-state update and save/reopen reliability for ED-M03-supported edits.
- Undo/redo for create, rename, delete, and reparent.
- Operation-result/diagnostic coverage for command, save, and sync failure.
- Focused tests for command service, scene mutator/organizer behavior,
  selection model, and save/reopen using the existing test infrastructure
  where practical.

## 5. Non-Scope

ED-M03 does not include:

- Production component inspector UX for all V0.1 component fields.
- Slider/text edit batching for property editors.
- Material editor, material asset identity, material picker, or thumbnails.
- Content import/cook/pak/inspect orchestration.
- Standalone runtime validation.
- Viewport selection highlights, gizmos, node icons, or camera navigation.
- Multi-viewport engine work.
- Cross-document commands.

Existing inspector and live-sync paths may be touched only when required to
consume shared selection or avoid bypassing the new command path for an
ED-M03-owned mutation.

## 6. Implementation Sequence

### ED-M03.1 - Baseline Audit And LLD Lock

Goal: freeze the implementation scope before code changes.

Tasks:

- Review the four gating LLDs and apply review feedback.
- Inventory current direct mutation paths:
  - `SceneEditorViewModel.AddLight` and `AddPrimitive`.
  - `SceneExplorerViewModel` tree event handlers.
  - `SceneExplorerService`, `SceneMutator`, and `SceneOrganizer`.
  - inspector transform/geometry/component messages.
  - `SceneEditorViewModel.SaveAsync`.
- Decide exact `ISceneDocumentCommandService` and `ISceneSelectionService`
  placement in WorldEditor.
- Record any intentionally deferred direct mutation paths in this plan or
  IMPLEMENTATION_STATUS, not as hidden TODOs.

Exit:

- LLDs are accepted for ED-M03.
- Command service placement and test seams are named.

### ED-M03.2 - Scene Document Command Surface

Goal: introduce one command-shaped entry point for ED-M03-supported mutations.

Tasks:

- Add `ISceneDocumentCommandService` in WorldEditor.
- Define typed methods for:
  - create node.
  - create procedural primitive.
  - create light.
  - rename node.
  - delete node hierarchy.
  - reparent node/hierarchy.
  - create/rename/delete scene explorer folder.
  - move node projection into/out of a scene explorer folder.
  - save scene.
- Reuse `SceneMutator`, `SceneOrganizer`, `HistoryKeeper`, and
  `ISceneEngineSync`; do not create a parallel mutator.
- Add operation kinds and diagnostic codes for ED-M03 command/save failures.
  Prefixes are allocated in `Oxygen.Core` before producer code uses them:
  `OXE.SCENE.*`, `OXE.DOCUMENT.*`, and `OXE.LIVESYNC.*`.
- Publish `OperationResult` for user-triggered command failures.

Exit:

- At least one command path proves the full chain:
  validation -> model mutation -> undo entry -> dirty state -> optional sync ->
  result handling.

### ED-M03.3 - Shared Selection Model

Goal: replace ad hoc object-list selection as the authoritative shared state.

Tasks:

- Add a document-scoped scene selection service/model in WorldEditor.
- Store selected node IDs and primary node ID.
- Primary selection is the last explicitly selected node; after tree rebuild,
  use the first surviving selected ID in previous stable order.
- Update Scene Explorer selection changes to publish through this service.
- Keep messenger compatibility for current inspector consumers by adapting
  service changes to existing selection messages.
- Ensure stale selection is cleared or reconciled after delete/reopen.
- Do not route selection success through command results, dirty state, or
  undo/redo. Stale/no-such-node selection requests may publish a scoped
  diagnostic.

Exit:

- Scene Explorer selection drives inspector consumers through the shared
  selection model.
- Selection survives tree rebuilds by ID where the selected nodes still exist.

### ED-M03.4 - Scene Explorer Command Wiring

Goal: move ED-M03 hierarchy actions behind the command surface.

Tasks:

- Route create, rename, delete, reparent, folder create/rename/delete, and
  layout moves through the command service.
- Keep folder/layout-only implementation delegated to `SceneOrganizer`.
- Ensure drag/drop validates before model mutation.
- Ensure invalid reparent and stale node IDs fail without partial scene graph
  mutation.
- Update undo/redo labels and behavior for the command surface.
- Remove direct `History.AddChange` writes from `SceneExplorerViewModel` paths
  that are migrated to the ED-M03 command service. Undo is recorded only by the
  command service for these operations.

Exit:

- Scene Explorer no longer bypasses commands for ED-M03-supported hierarchy
  mutations.

### ED-M03.5 - Quick-Add Foundation

Goal: make the visible quick-add scene authoring actions real.

Tasks:

- Implement `AddPrimitive` through `Scene.Node.CreatePrimitive`.
- Move `AddLight` direct mutation into `Scene.Node.CreateLight`.
- Use safe procedural defaults already supported by the model/runtime.
- Select newly created nodes through the shared selection service.
- Mark document dirty only through the command result.
- Directional light creation is an ED-M03 closure gate. Point and spot light
  creation remain best effort unless their current domain save/reopen path
  works without extra runtime/component-editor scope.

Exit:

- User can create supported primitive nodes and a directional light node from
  the scene editor UI. Point/spot light creation is recorded as pass/skip
  evidence, not an ED-M03 blocker.

### ED-M03.6 - Save, Dirty State, And Reopen

Goal: make supported ED-M03 edits durable.

Tasks:

- Ensure dirty state is set by successful commands and cleared only after
  successful scene save.
- Ensure failed save leaves dirty state set and publishes a `Scene.Save` /
  `Document` result.
- Reconcile legacy `ProjectManagerService.SaveSceneAsync` cook side effect:
  tolerated for ED-M03, but not treated as the command contract.
- Treat authoring-data save success independently from any legacy cook side
  effect. `Scene.Save` status and dirty clearing are based on scene persistence;
  a cook side-effect failure is logged and may publish a separate
  `ContentPipeline` diagnostic without changing the `Scene.Save` result.
- Validate save/reopen for create primitive, create light, rename, delete, and
  reparent.

Exit:

- Supported ED-M03 edits survive save/reopen without manual file edits.

### ED-M03.7 - Tests And Manual Validation

Goal: close the milestone with evidence.

Tasks:

- Add or update unit tests for:
  - command service create primitive success.
  - command service rename failure for empty/invalid name.
  - delete undo/redo restore for a node hierarchy.
  - selection model set/clear/reconcile after delete or tree rebuild.
  - `SceneMutator` invalid reparent/stale node behavior if uncovered.
  - scene explorer folder create/rename/delete and layout move if touched.
  - save/reopen round-trip for create primitive plus rename.
- Prepare manual validation instructions for the user.
- Update IMPLEMENTATION_STATUS only after user validation.

Exit:

- Tests that are straightforward in the existing infrastructure are present.
- Skipped tests are recorded with a concrete reason.

## 7. Project/File Touch Points

Likely implementation files:

- `projects/Oxygen.Editor.WorldEditor/src/SceneEditor/SceneEditorViewModel.cs`
- `projects/Oxygen.Editor.WorldEditor/src/SceneExplorer/SceneExplorerViewModel.cs`
- `projects/Oxygen.Editor.WorldEditor/src/SceneExplorer/Services/SceneExplorerService.cs`
- `projects/Oxygen.Editor.WorldEditor/src/SceneExplorer/Operations/SceneMutator.cs`
- `projects/Oxygen.Editor.WorldEditor/src/SceneExplorer/Operations/SceneOrganizer.cs`
- `projects/Oxygen.Editor.WorldEditor/src/Documents/DocumentManager.cs`
- `projects/Oxygen.Editor.WorldEditor/src/Documents/DocumentHostViewModel.cs`
- `projects/Oxygen.Editor.WorldEditor/src/Services/ISceneEngineSync.cs`
- `projects/Oxygen.Editor.WorldEditor/src/Services/SceneEngineSync.cs`
- `projects/Oxygen.Editor.World/src/Scene.cs`
- `projects/Oxygen.Editor.World/src/SceneNode.cs`
- `projects/Oxygen.Editor.World/src/Components/*`
- `projects/Oxygen.Core/src/Diagnostics/*`
- WorldEditor and World test projects.

New files should live under existing WorldEditor ownership, for example:

- `src/Documents/Commands/`
- `src/Documents/Selection/`
- `src/Diagnostics/`

Do not add a new project for ED-M03 unless implementation proves the existing
ownership is insufficient.

New ED-M03 WorldEditor files use `Oxygen.Editor.WorldEditor.*` namespaces. Some
existing WorldEditor files still use `Oxygen.Editor.World.*`; aligning those
legacy namespaces is not ED-M03 scope.

## 8. Dependency And Migration Risks

| Risk | Mitigation |
| --- | --- |
| Command service duplicates `SceneExplorerService` and creates two mutation paths. | Reuse/wrap `SceneExplorerService`, `SceneMutator`, and `SceneOrganizer`; do not fork hierarchy logic. |
| Inspector still mutates directly. | ED-M03 only moves touched ED-M03-owned mutations; ED-M04 completes inspector migration. Document any remaining direct inspector paths. |
| Live-sync failure rolls back good authoring state. | ED-M03 commands keep successful model mutation and report sync failure as warning/partial success. |
| Save still triggers brownfield cook behavior. | Tolerate existing behavior but keep scene save command contract separate from cook success. |
| Selection service breaks existing inspector messenger consumers. | Add adapter messages until inspector consumers migrate. |
| Undo inverse delegates can recurse or create dirty-state drift. | Centralize dirty updates after command success and cover undo/redo in tests. |
| DynamicTree in-place rename currently commits by directly setting `ItemAdapter.Label`, so Scene Explorer must observe label changes to keep undo/redo and persistence working. | Accepted bridge for ED-M03 because it tracks loaded adapters only and does not block the milestone. Proper solution is deferred: add a DynamicTree rename-commit hook/override so feature view models can route in-place rename through their command path before mutation. |
| Existing WorldEditor view models reference `Oxygen.Interop` managed value types. | ED-M03 forbids direct interop behavior calls from UI view models; existing value-type references are acknowledged brownfield and not expanded. |
| ED-M02 is landed but multi-viewport validation is deferred. | ED-M03 may proceed, but live preview is not used as ED-M03 closure evidence until ED-M02 validation status allows it. |

## 9. Validation Gates

Manual validation expectations:

- Open Vortex, open/restore a scene, and see hierarchy in Scene Explorer.
- Create a primitive from quick-add; it appears in hierarchy and scene model.
- Create a directional light from quick-add; it appears in hierarchy and
  save/reopen.
- Point and spot lights are best-effort in ED-M03: verify if present, but do
  not block closure on their runtime/component-editor gaps.
- Select a node in Scene Explorer; inspector selection updates.
- Rename a node; dirty marker appears; save clears dirty marker; reopen keeps
  the new name.
- Rename through the toolbar/dialog and through DynamicTree in-place edit;
  both paths support undo/redo. The in-place edit implementation is accepted
  as an ED-M03 bridge and does not block closure.
- Delete a node hierarchy; undo restores it; redo removes it again.
- Reparent a node; undo/redo restores prior/new hierarchy.
- Create/rename/delete an explorer folder or move a node into/out of a folder;
  it remains layout-only and save/reopen where supported.
- Invalid operation failure is visible and does not leave partial hierarchy
  changes.
- Save failure path, where safely testable, produces a visible result.
- If the brownfield save path reports a cook side-effect failure after scene
  data saved, `Scene.Save` still reflects authoring-data persistence and the
  cook issue is logged/reported separately.

Validation evidence:

- concise user validation notes.
- targeted test results or skipped-test reasons.
- one IMPLEMENTATION_STATUS ED-M03 ledger row.

## 10. Status Ledger Hook

Before implementation:

- mark ED-M03 LLDs reviewed in IMPLEMENTATION_STATUS after review acceptance.
- mark this plan accepted after review acceptance.

After implementation/user validation:

- mark ED-M03 implementation checklist items complete.
- add one validation ledger row recording command/dirty/undo behavior,
  save/reopen result, selection behavior, diagnostics coverage, and deferred
  non-scope items.
