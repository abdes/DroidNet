# Scene Explorer LLD

Status: `review`

## 1. Purpose

Define the ED-M03 scene hierarchy UI: hierarchy projection, shared selection,
node create/delete/rename/reparent UX, folder/layout-only behavior, drag/drop,
undo/redo expectations, dirty state, and command/result integration.

The scene explorer is not the scene graph owner. It is the primary hierarchy UI
for the active scene document.

## 2. PRD Traceability

| ID | Coverage |
| --- | --- |
| `REQ-004` | Scene document hierarchy is visible and usable. |
| `REQ-006` | Hierarchy commands update dirty state. |
| `REQ-007` | ED-M03 hierarchy changes save and reopen through the scene document. |
| `REQ-008` | Hierarchy commands request live sync through the command pipeline when supported. |
| `REQ-022` | Hierarchy command failures are visible operation results. |
| `REQ-024` | Hierarchy diagnostics identify scene authoring vs. live-sync failure. |

## 3. Architecture Links

- `ARCHITECTURE.md`: workspace UI surfaces, command path, diagnostics, runtime
  boundary.
- `DESIGN.md`: scene explorer LLD ownership and cross-LLD selection model.
- `documents-and-commands.md`: shared command and selection contract.
- `scene-authoring-model.md`: scene graph and explorer layout data.
- `diagnostics-operation-results.md`: result/failure-domain vocabulary.

## 4. Current Baseline

Useful existing pieces:

- `SceneExplorerViewModel` derives from `DynamicTreeViewModel` and listens for
  document open/activation events.
- `SceneAdapter`, `SceneNodeAdapter`, `FolderAdapter`, and `LayoutItemAdapter`
  model scene tree and layout-only folders.
- `SceneOrganizer` owns explorer layout/folder operations and persists
  `ExplorerEntryData` into scene data.
- `SceneMutator` owns scene graph create/remove/reparent operations.
- `SceneExplorerService` coordinates mutator, organizer, and
  `ISceneEngineSync`.
- Selection is already multiple-selection capable and published/requested via
  messenger messages.
- `HistoryKeeper` records undo/redo entries for several operations.

Brownfield gaps:

- The view model handles too much orchestration directly through tree events.
- Selection is messenger-based object lists, not a document-scoped identity
  service.
- Some undo entries are inverse delegates rather than typed command records.
- Rename/delete/reparent failures are mostly exceptions/logs, not operation
  results.
- Folder/layout operations and scene graph operations are better separated than
  before, but not yet enforced by one command boundary.
- Drag/drop behavior depends on existing tree control event shape and needs
  clearer validation before mutation.

## 5. Target Design

ED-M03 scene explorer flow:

```text
Document activated
  -> load scene adapter tree
  -> reconcile explorer layout
  -> publish empty/current selection
  -> user action
  -> document command service
  -> scene mutator / organizer
  -> dirty + undo + sync + result
  -> rebuild/reconcile tree without losing selection where possible
```

## 6. Ownership

| Owner | Responsibility |
| --- | --- |
| `SceneExplorerViewModel` | UI state, tree projection, command invocation, selection presentation. |
| `SceneExplorerService` | Hierarchy/layout orchestration behind commands. |
| `SceneMutator` | Scene graph create/delete/reparent invariants. |
| `SceneOrganizer` | Layout-only folders and explorer layout persistence. |
| `documents-and-commands.md` command service | Command result, dirty state, undo/redo, shared selection ownership. |
| `scene-authoring-model.md` | Scene graph/domain data rules. |

## 7. Data Contracts

### Hierarchy Item Model

Explorer items are projections:

- `SceneAdapter`: scene root projection.
- `SceneNodeAdapter`: projection of a `SceneNode`.
- `FolderAdapter`: layout-only folder.
- `LayoutItemAdapter`: layout projection glue.

Only `SceneNodeAdapter` maps to runtime scene graph data. Folders never become
scene nodes.

### Selection Projection

Scene explorer selection publishes:

- document ID.
- ordered selected node IDs.
- primary selected node ID.
- source = `SceneExplorer`.

Scene explorer may keep selected adapter references locally, but shared
selection is ID-based.

The primary selected node is the last explicitly selected node. If a tree
rebuild removes it, the selection service keeps the first surviving selected ID
in the previous stable order.

### Drag/Drop Payload

Minimum payload:

- moved item IDs.
- item kinds: node or folder.
- destination item ID/kind.
- insertion index where known.
- requested transform policy: ED-M03 default is preserve local transform.

### Context Menu Payload

Every menu action resolves to a document command request with:

- active document ID.
- target node/folder/scene IDs.
- command kind.
- optional user-provided text such as new name.

## 8. Commands, Services, Or Adapters

ED-M03 hierarchy commands:

| User Action | Command |
| --- | --- |
| Add empty node | `Scene.Node.Create` |
| Rename node | `Scene.Node.Rename` |
| Delete selected node(s) | `Scene.Node.Delete` |
| Drag node to node/root/folder | `Scene.Node.Reparent` plus optional layout update |
| Create folder | `Scene.ExplorerFolder.Create` |
| Rename folder | `Scene.ExplorerFolder.Rename` |
| Delete folder | `Scene.ExplorerFolder.Delete` |
| Move node into folder | `Scene.ExplorerLayout.MoveNode` plus scene reparent if lineage requires it |

Folder commands are layout commands. They update explorer layout and dirty
state but do not call live sync unless they also trigger scene graph mutation.
Selection changes go through `ISceneSelectionService`, not the undoable command
surface. Selection success does not mark dirty, does not enter undo, and does
not publish an operation result; stale/no-such-node selection requests may
publish a scoped `SceneAuthoring` diagnostic.

## 9. UI Surfaces

ED-M03 scene explorer surface includes:

- active scene hierarchy tree.
- empty scene state with create action.
- context menu for create/rename/delete.
- inline rename.
- drag/drop reparent or layout move.
- visible selection state.
- undo/redo availability inherited from document command state.
- operation result presentation for failed commands where practical.

## 10. Persistence And Round Trip

Scene explorer persists only through the scene document:

- scene graph mutations persist through `Scene` serialization.
- folder/layout mutations persist through `Scene.ExplorerLayout`.
- selection is not persisted in ED-M03.

Save/reopen must preserve:

- node names.
- hierarchy parent/child relationships.
- root order where command semantics define it.
- layout folders where the scene explorer already persists them.

## 11. Live Sync / Cook / Runtime Behavior

Scene explorer does not call runtime or interop directly.

Hierarchy commands request live sync through the command pipeline. Layout-only
folder commands do not request sync. If live sync fails after authoring state
changed, the command remains applied and the result is warning/partial success.

## 12. Operation Results And Diagnostics

Failure mapping:

| Failure | Domain |
| --- | --- |
| stale node/folder ID | `SceneAuthoring` |
| invalid reparent cycle | `SceneAuthoring` |
| empty/invalid name | `SceneAuthoring` |
| folder layout corruption | `SceneAuthoring` |
| live sync create/delete/reparent failure | `LiveSync` |

User-triggered hierarchy failures must produce visible operation results or
output/log diagnostics with document and node/folder scope where known.

## 13. Dependency Rules

Allowed:

- Scene explorer depends on WorldEditor command services, World domain data,
  document service, and diagnostics contracts.

Forbidden:

- Scene explorer must not call `Oxygen.Editor.Interop` directly.
- Scene explorer must not persist scene data except through scene document
  save.
- Scene explorer must not treat folders as scene nodes.
- Inspector and viewport must not own scene explorer selection state.

## 14. Validation Gates

ED-M03 scene explorer is complete when:

- opening/restoring a scene shows the hierarchy.
- selecting one or more nodes updates shared selection and inspector consumers.
- create, rename, delete, and reparent commands update hierarchy and dirty
  state.
- undo/redo restores create, rename, delete, and reparent.
- folder/layout operations remain layout-only and save/reopen where supported.
- invalid operations fail before partial scene graph mutation.
- command/live-sync failures publish operation results or scoped diagnostics.

## 15. Open Issues

- Multi-select command semantics beyond delete and selection are post-ED-M03
  unless already implemented safely.
- Copy/paste/duplicate are out of ED-M03 unless explicitly re-scoped.
- Preserve-world-transform reparenting is not ED-M03 default.
