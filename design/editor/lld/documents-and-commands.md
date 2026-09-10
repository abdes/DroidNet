# Documents And Commands LLD

Status: `review`

## 1. Purpose

Define the ED-M03 document lifecycle and command foundation for scene
authoring: open/activate/save, dirty state, undo/redo, shared selection,
operation results, and the rule that user-facing mutations enter through a
document command surface instead of directly changing scene state from view
models.

This LLD does not define every component inspector. ED-M03 creates the command
and document rails that ED-M04 component editors must use.

## 2. PRD Traceability

| ID | Coverage |
| --- | --- |
| `REQ-004` | Scene documents can be opened, activated, edited, saved, closed, and reopened. |
| `REQ-005` | Partial: ED-M03 establishes command rails for component-bearing node creation; full component add/remove/edit inspectors are ED-M04. |
| `REQ-006` | Dirty state follows successful authoring commands and save results. |
| `REQ-007` | ED-M03-supported scene data saves and reopens. |
| `REQ-008` | ED-M03-supported mutations request live sync when the embedded engine is available. |
| `REQ-009` | Partial: ED-M03 creates supported primitive/light nodes through commands; production component completion continues in ED-M04. |
| `REQ-022` | Save/command failures produce visible operation results. |
| `REQ-024` | Diagnostics identify document, authoring, and sync failure domains. |
| `SUCCESS-002` | Supported scene edits survive save/reopen. |

## 3. Architecture Links

- `ARCHITECTURE.md`: document lifecycle, command path, live-sync boundary,
  diagnostics, dependency direction.
- `DESIGN.md`: cross-LLD workflow chains and selection model ownership.
- `PROJECT-LAYOUT.md`: `Oxygen.Editor.Documents`,
  `Oxygen.Editor.WorldEditor`, `Oxygen.Editor.World`, and feature editor
  boundaries.
- `scene-authoring-model.md`: scene graph data and mutation invariants.
- `scene-explorer.md`: hierarchy UI consuming this command and selection model.
- `diagnostics-operation-results.md`: operation-result contract.

## 4. Current Baseline

Useful existing pieces:

- `Documents` and `Oxygen.Editor.Documents` provide generic document metadata,
  document service events, and editor document service tests.
- `DocumentManager` opens scene documents and can select an already-open scene.
- `DocumentHostViewModel` resolves the active editor view and now deactivates
  scene views during document switches.
- `SceneEditorViewModel` implements `IAsyncSaveable`, tracks document metadata,
  updates dirty state from the undo stack, and saves through
  `ProjectManagerService.SaveSceneAsync`.
- `DroidNet.TimeMachine.UndoRedo` / `HistoryKeeper` already back scene explorer
  and inspector undo stacks.
- Scene explorer selection is published through messenger messages:
  `SceneNodeSelectionChangedMessage` and `SceneNodeSelectionRequestMessage`.
- Scene explorer operations already use `SceneExplorerService`,
  `SceneMutator`, `SceneOrganizer`, and `ISceneEngineSync`.

Brownfield gaps:

- Command boundaries are inconsistent: scene explorer, inspector, quick-add,
  component add/remove, and save paths use different direct mutation and
  messenger patterns.
- `SceneEditorViewModel.AddPrimitive` is a placeholder; `AddLight` directly
  mutates scene state, calls live sync, and marks dirty without a command
  contract.
- Inspector transform/geometry/component edits record undo and call live sync
  directly from inspector view models.
- Scene explorer undo records inverse delegates, but command result,
  dirty-state update, validation invalidation, and live-sync failure handling
  are not centralized.
- `ProjectManagerService.SaveSceneAsync` persists authoring scene data only;
  explicit scene cook orchestration is deferred to the content pipeline.

## 5. Target Design

ED-M03 introduces `ISceneDocumentCommandService` inside WorldEditor. UI
surfaces request commands through this service; commands coordinate:

```text
User action
  -> scene document command service
  -> validate request
  -> mutate scene authoring model
  -> record undo/redo
  -> update selection if needed
  -> mark document dirty
  -> request live sync when supported
  -> publish operation result on failure/partial success
```

The implementation may use the existing `HistoryKeeper` underneath. The target
contract is not "every command is a new class"; it is that every supported
mutation has one command-shaped entry point with consistent side effects.
Undo/redo entries for ED-M03-supported scene and explorer mutations are
recorded only by this command service; view models must stop writing direct
`History.AddChange` entries for those paths.

## 6. Ownership

| Owner | Responsibility |
| --- | --- |
| `Oxygen.Editor.Documents` | Generic document metadata/service lifecycle and events; no ED-M03 changes are expected unless generic document tests reveal a gap. |
| `Oxygen.Editor.WorldEditor` | Scene document command service, scene document selection service, command orchestration, dirty-state wiring, save workflow, hierarchy command UX. |
| `Oxygen.Editor.World` | Scene graph and component domain objects, serialization DTOs, domain invariants. |
| `SceneExplorerService` / successors | Hierarchy command implementation details for create/delete/rename/reparent/folder layout. |
| `ISceneEngineSync` | Live sync adapter consumed by commands; it does not own authoring state. |
| Inspector view models | ED-M04 command consumers; in ED-M03 they remain brownfield except where needed for selection/dirty/save reliability. |

## 7. Data Contracts

### Scene Document Context

Workspace-scoped context for an open scene document:

- document ID.
- scene ID/name.
- scene authoring model reference.
- document metadata.
- command history key.
- selection state key.
- save state.

The context references the scene model; it must not duplicate scene graph data.

### Scene Document Command Request

Minimum fields:

- `CommandId`: stable operation kind, e.g. `Scene.Node.Create`.
- `DocumentId`.
- command-specific payload.
- optional expected selection/revision facts for stale UI detection.
- cancellation token where the workflow is async.

### Scene Document Command Result

Minimum fields:

- success/failure/cancelled state.
- affected scope: document, scene, node/component IDs where known.
- undo label/change handle when an undo entry was produced.
- live-sync result when sync was attempted.
- operation result ID when a visible result was published.

### Selection State

Document-scoped selection model:

- selected scene node IDs in stable order.
- primary selected node ID, if any.
- selection source (`SceneExplorer`, `Inspector`, later `Viewport`).
- timestamp/revision for stale event filtering.

Selection is identity-based. UI adapters may hold object references, but shared
selection state is node ID based so it survives tree rebuilds.

The primary selected node is the last explicitly selected node. If a tree
rebuild removes it, the selection service uses the first surviving selected ID
in the previous stable order.

Selection changes are not scene mutations. They do not mark the document dirty,
do not create undo/redo entries, and do not publish operation results on the
success path. Stale/no-such-node selection requests may publish a scoped
`SceneAuthoring` diagnostic.

## 8. Commands, Services, Or Adapters

`ISceneDocumentCommandService` is the ED-M03 command contract consumed by scene
editor and scene explorer UI. Its typed methods map one-to-one to the operation
kinds below, for example:

- `CreateNodeAsync`.
- `CreatePrimitiveAsync`.
- `CreateLightAsync`.
- `RenameNodeAsync`.
- `DeleteNodeHierarchyAsync`.
- `ReparentNodeAsync`.
- `CreateExplorerFolderAsync`.
- `RenameExplorerFolderAsync`.
- `DeleteExplorerFolderAsync`.
- `MoveExplorerLayoutItemAsync`.
- `SaveSceneAsync`.

`ISceneSelectionService` is a separate document-scoped service for selection
state. It is not an undoable command service.

ED-M03 command surface:

| Command | ED-M03 Scope |
| --- | --- |
| `Scene.Node.Create` | Create root/child node with default transform. |
| `Scene.Node.CreatePrimitive` | Create supported procedural primitive node from the quick-add menu. |
| `Scene.Node.CreateLight` | Create directional light node from the quick-add menu; point/spot remain best effort in ED-M03. |
| `Scene.Node.Rename` | Rename node and update hierarchy/document state. |
| `Scene.Node.Delete` | Delete selected node hierarchy and support undo restore. |
| `Scene.Node.Reparent` | Reparent node/hierarchy; local-transform preservation is the ED-M03 default. |
| `Scene.ExplorerFolder.Create` | Create a layout-only scene explorer folder. |
| `Scene.ExplorerFolder.Rename` | Rename a layout-only scene explorer folder. |
| `Scene.ExplorerFolder.Delete` | Delete a layout-only scene explorer folder and reconcile contained layout items. |
| `Scene.ExplorerLayout.MoveNode` | Move a node projection into/out of a folder; scene reparent is a separate `Scene.Node.Reparent` command when lineage changes. |
| `Scene.Save` | Persist a coherent snapshot and acknowledge its revision; newer changes remain dirty. |

Adapters:

- Scene explorer calls hierarchy commands instead of mutating scene graph
  directly.
- Quick-add calls create commands instead of direct scene mutation.
- Inspector mutation remains brownfield unless ED-M03 touches it; any touched
  path must move to the command surface.
- Live sync is requested by commands after authoring state changes. A sync
  failure does not roll back the authoring command in ED-M03; it produces a
  visible/diagnostic result and leaves the document dirty state accurate.

## 9. UI Surfaces

ED-M03 surfaces:

- document tabs/host: active document, dirty marker, save command.
- scene editor quick-add menu: primitive/light creation through commands.
- scene explorer: hierarchy create/delete/rename/reparent and selection.
- inspector: consumes shared selection; no longer owns the selection source of
  truth.
- output/log/result surfaces: command/save/sync failures.

## 10. Persistence And Round Trip

### Saving While Editing

A successful save acknowledges the revision captured with its immutable source
snapshot. It does not clear newer edits or replace current authoring state.
Ordinary save reports "Saved; newer changes remain unsaved" when appropriate;
save-and-close additionally requires that no newer revision remains dirty.

Scene commands record authoring changes before live synchronization awaits,
including changes to an already-dirty document and undo/redo. Multi-node property
application updates all model targets synchronously before pushing engine work.
Scene Explorer services emit their authoring-change notification before live
sync; deletion updates scene graph and explorer layout before synchronization.
Transform previews retain their existing commit/cancel semantics.

Scene saves are serialized per scene across command-service instances. Snapshot
capture serializes the graph, environment, and explorer layout into an owned
JSON string before resolving storage folders or awaiting writes. Project-level
writes are also serialized per destination. Successful completion advances only
the captured saved version; failure preserves authoring state and the previous
saved version. Save gates for destinations are removed after all users finish.

See [issue #4 implementation plan](../plan/issue-004-save-revisions.md) for the
deterministic storage-gated validation scenarios.

### Unsaved Document Closure

`EditorDocumentService` prepares closure before removing documents or raising
`DocumentClosed`. Its existing asynchronous `DocumentClosing` vetoes apply to
both individual closes and every document in a workspace close. Scene replacement
uses this same path with `force: false`.

`DocumentCloseCoordinator` owns editor close preparation and the registered
`IDocumentCloseParticipant` authoring owners. Participants drain pending saves
and material edits, expose save success, and close only after an explicit
decision. View-model disposal releases resources; it does not authorize discard.
The material participant maps the tab identity to the material service identity.

The WorldEditor `DocumentClosePrompt` supplies two presentations:

- Individual tab closure and scene replacement: Save, Discard, Cancel.
- Workspace/window closure: one list of dirty documents, all selected by
  default, with Save Selected, Discard All, and Cancel in a Close workspace dialog.
  The dialog explains that unchecked documents' changes will be discarded.

Selected saves run while the dialog remains open. Failed saves show a per-document
message and publish an operation result. Retry preserves already successful
saves; Cancel preserves all open documents, unsaved authoring state, history,
selection, and active context. Successful saves are not rolled back by Cancel.
No unchecked document is discarded while any selected save is failing.

`WorkspaceCloseCoordinator` prepares all documents during Aura's cancelable
`WindowClosing` event. Aura awaits every handler before running completion
callbacks: approval commits the document transaction; a veto disposes preparation
without closing documents. Duplicate close requests cannot bypass a pending
decision. Document opening, detaching, and activation are rejected while the
window's close transaction is pending. Edits detected during a selected save
remain dirty and prevent that close from proceeding.

Automated coverage exercises cancellation and scene replacement through the real
document service, selected workspace saves and discards, late vetoes, overlapping
requests, material save I/O failure/retry and disposal, and native window/dialog
asynchronous behavior. Full editor workflow validation remains a separate gate.

### Scene Round Trip

`Scene.Save` persists supported scene graph state through the existing scene
serializer path. ED-M03 must prove:

- create/rename/delete/reparent survives save/reopen.
- primitive and light nodes created through ED-M03 commands survive
  save/reopen.
- dirty state is cleared only after successful save.
- failed save leaves dirty state set.

Scene save has no cook side effect. `Scene.Save` reports authoring-data
persistence only. Explicit scene cook orchestration and any related
`ContentPipeline` diagnostics are owned by the content pipeline.

## 11. Live Sync / Cook / Runtime Behavior

Commands may request live sync through `ISceneEngineSync`. They must not call
native interop directly.

ED-M03 live sync scope is command-triggered best effort for hierarchy,
primitive, and light creation/deletion/reparent where current engine APIs
support it. Complete component property sync is ED-M04.

## 12. Operation Results And Diagnostics

Operation kinds:

| Operation Kind | Failure Domain |
| --- | --- |
| `Scene.Node.Create` | `SceneAuthoring` / `LiveSync` |
| `Scene.Node.CreatePrimitive` | `SceneAuthoring` / `LiveSync` |
| `Scene.Node.CreateLight` | `SceneAuthoring` / `LiveSync` |
| `Scene.Node.Rename` | `SceneAuthoring` |
| `Scene.Node.Delete` | `SceneAuthoring` / `LiveSync` |
| `Scene.Node.Reparent` | `SceneAuthoring` / `LiveSync` |
| `Scene.ExplorerFolder.Create` | `SceneAuthoring` |
| `Scene.ExplorerFolder.Rename` | `SceneAuthoring` |
| `Scene.ExplorerFolder.Delete` | `SceneAuthoring` |
| `Scene.ExplorerLayout.MoveNode` | `SceneAuthoring` |
| `Scene.Save` | `Document` |

Command failures must publish an operation result when triggered by direct user
action. Async live-sync failures after a successful command are reported as
partial success or warning, not hidden in logs.

## 13. Dependency Rules

Allowed:

- WorldEditor command services depend on `Oxygen.Editor.World`,
  `Oxygen.Editor.Documents`, diagnostics contracts, and runtime sync services.
- Scene explorer and inspector depend on the WorldEditor command/selection
  surface.
- World domain exposes synchronous model methods and serialization.

Forbidden:

- `Oxygen.Editor.World` must not depend on WorldEditor, runtime, routing, or UI.
- Generic document contracts must not depend on WorldEditor.
- UI view models must not invoke `Oxygen.Editor.Interop` behavior directly:
  engine, surface, view, or native runtime calls must go through runtime/sync
  services. Existing managed value-type references from interop are brownfield
  and are not expanded by ED-M03.
- Feature UI must not bypass commands for any ED-M03-supported mutation.
- Save success must not be inferred from cook success.

## 14. Validation Gates

ED-M03 documents/commands are complete when:

- opening a project opens/restores a scene document and selection starts empty.
- create primitive and directional light produce scene nodes through commands.
- rename, delete, and reparent update the hierarchy, dirty state, undo stack,
  and persisted scene data.
- undo/redo restores authoring state for create/delete/rename/reparent.
- selection changes in scene explorer are reflected by inspector consumers.
- folder/layout operations are command-shaped, layout-only, dirty tracked, and
  save/reopen where current explorer layout persistence supports them.
- save clears dirty state only on success.
- save/reopen preserves ED-M03-supported hierarchy and created nodes.
- command/save/sync failure paths produce operation results or output/log
  diagnostics with document/node scope where known.

## 15. Open Issues

- Cross-document commands are out of ED-M03.
- Cross-document selection and cross-document undo are out of ED-M03; selection
  and history are isolated per scene document.
- Command batching for sliders/text fields is ED-M04.
- Full validation invalidation model is scaffolded in ED-M03 and expanded in
  ED-M04/ED-M09.
