# Scene Explorer LLD

Status: `review`

## 1. Purpose

Define the ED-M03 scene hierarchy UI: hierarchy projection, shared selection,
node create/delete/rename/reparent UX, folder/layout-only behavior, drag/drop,
undo/redo expectations, dirty state, and command/result integration.

The scene explorer is not the scene graph owner. It is the primary hierarchy UI
for the active scene document.

## 2. PRD Traceability

| ID        | Coverage                                                                          |
| --------- | --------------------------------------------------------------------------------- |
| `REQ-004` | Scene document hierarchy is visible and usable.                                   |
| `REQ-006` | Hierarchy commands update dirty state.                                            |
| `REQ-007` | ED-M03 hierarchy changes save and reopen through the scene document.              |
| `REQ-008` | Hierarchy commands request live sync through the command pipeline when supported. |
| `REQ-022` | Hierarchy command failures are visible operation results.                         |
| `REQ-024` | Hierarchy diagnostics identify scene authoring vs. live-sync failure.             |

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
- `ISceneDocumentCommandService` is the only authoring mutation authority:
  it drives the mutator and the organizer inside one transaction, records
  history, advances dirty state and requests live sync. The former
  `SceneExplorerService` facade was a second, history-less mutation surface
  and is retired (Explorer plan D-a / C39); nothing may reintroduce it.
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

| Owner                                       | Responsibility                                                          |
| ------------------------------------------- | ----------------------------------------------------------------------- |
| `SceneExplorerViewModel`                    | UI state, tree projection, command invocation, selection presentation.  |
| `SceneDocumentCommandService`               | Hierarchy/layout orchestration behind commands; sole history authority. |
| `SceneMutator`                              | Scene graph create/delete/reparent invariants.                          |
| `SceneOrganizer`                            | Layout-only folders and explorer layout persistence.                    |
| `documents-and-commands.md` command service | Command result, dirty state, undo/redo, shared selection ownership.     |
| `scene-authoring-model.md`                  | Scene graph/domain data rules.                                          |

## 7. Data Contracts

### Hierarchy Item Model

Explorer items are projections:

- `SceneAdapter`: scene root projection.
- `SceneNodeAdapter`: projection of a `SceneNode`.
- `FolderAdapter`: layout-only folder.
- `LayoutItemAdapter`: layout projection glue.

Only `SceneNodeAdapter` maps to runtime scene graph data. Folders never become
scene nodes.

The authored layout is a placement overlay, not a visibility filter. Nodes
without layout seats, including newly created descendants, remain visible under
their scene-graph parent. Already seated nodes retain their authored placement
and appear only once.

Deleting a folder is a layout operation only: its entries re-attach to the
deleted folder's own parent entry, no node is deleted, and scene-graph parentage
and world transforms are unchanged — including where the grouping followed a
lineage-driven reparent. Deleting a node deletes its subtree.

### Selection Projection

The Explorer is one writer to the single document-scoped selection service,
and publishes the full classified context (see
`documents-and-commands.md` § Selection State):

- document ID.
- row kind: scene root, node, folder or mixed.
- ordered selected node IDs and ordered selected folder IDs.
- explicit primary identity: the last activated row, node or folder.
- source = `SceneExplorer`.

The Explorer may keep selected adapter references locally, but shared
selection is ID-based. Selection follows identities, not rows: a command
reveal, viewport pick or restore may name a node whose row is not realized
yet; the Explorer reveals (expanding ancestors as transient view state, never
authored) and re-selects when it resolves, and unresolved IDs remain
authoritative rather than being dropped by a rebuild, switch or filtered
projection. The Explorer subscribes to the service's change notification and
reconciles its rows from foreign writes without echoing its own.

Projection refill suppresses intermediate selection publications and restores
the latest stored context, including foreign writes made during refill. Folder
expansion caused by selection reveal stays transient across adapter replacement,
just like search reveal.

The scene root row is exclusive: selecting it clears node/folder rows and
select-all or range selection never adds it. Folder-only and mixed selections
publish a grouping summary to the Inspector; they never reduce to the node
subset as a component edit nor to an empty list as the scene selection.

The primary selected row is the last explicitly activated row. If a tree
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

| User Action                   | Command                                                                    |
| ----------------------------- | -------------------------------------------------------------------------- |
| Add empty node                | `Scene.Node.Create`                                                        |
| Rename node                   | `Scene.Node.Rename`                                                        |
| Rename scene asset            | `Scene.Rename`                                                             |
| Delete selected node(s)       | `Scene.Node.Delete`                                                        |
| Drag node to node/root/folder | `Scene.Node.Reparent` plus optional layout update                          |
| Create folder                 | `Scene.ExplorerFolder.Create`                                              |
| Rename folder                 | `Scene.ExplorerFolder.Rename`                                              |
| Delete folder                 | `Scene.ExplorerFolder.Delete`                                              |
| Move node into folder         | `Scene.ExplorerLayout.MoveNode` plus scene reparent if lineage requires it |

Folder commands are layout commands. They update explorer layout and dirty
state but do not call live sync unless they also trigger scene graph mutation.
Selection changes go through `ISceneSelectionService`, not the undoable command
surface. Selection success does not mark dirty, does not enter undo, and does
not publish an operation result; stale/no-such-node selection requests may
publish a scoped `SceneAuthoring` diagnostic.

Scene-root inline, toolbar and context-menu rename use
`ISceneDocumentCommandService.RenameSceneAsync`. The root remains protected from
cut/delete; that protection does not prohibit its dedicated asset rename.
`IProjectManagerService.RenameSceneAssetAsync` owns the reusable persistence
operation, separate from Explorer presentation. It renames the saved
`Content/Scenes/<name>.oscene.json` file and authored scene name, preserving the
scene GUID and GUID-based default-scene selection. It repairs matching
project-local scene `References.ExtraAssets` paths and updates acknowledged
source versions. Read-only mounted assets and arbitrary string fields are not
rewritten. Cooked identity is path-derived; a subsequent cook uses the new path.

Rename changes saved data without silently saving unrelated live edits. A clean
document stays clean, and existing/newer unsaved edits stay dirty. One history
step reverses the persisted rename and reference repair; Redo reapplies them.
Invalid names, occupied destinations and external-write conflicts produce
visible failures without discarding an unapplied Undo/Redo step. If the rename
commits but editor refresh fails, feedback reports the completed rename with a
warning rather than claiming it was rejected.

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

Scene graph and layout edits persist through the scene document:

- scene graph mutations persist through `Scene` serialization.
- folder/layout mutations persist through `Scene.ExplorerLayout`.
- selection is not persisted in ED-M03.

The dedicated scene-asset rename persists immediately through its Projects
owner, not through a save of the live scene graph. Scene writes are serialized
per project; individual replacements use compare-and-swap baselines. Observed
write/move failures roll back completed replacements. This is not a multi-file
crash-atomic transaction. A save snapshot captured before rename is rejected
rather than recreating the old source file.

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

| Failure                                  | Domain           |
| ---------------------------------------- | ---------------- |
| stale node/folder ID                     | `SceneAuthoring` |
| invalid reparent cycle                   | `SceneAuthoring` |
| empty/invalid name                       | `SceneAuthoring` |
| folder layout corruption                 | `SceneAuthoring` |
| live sync create/delete/reparent failure | `LiveSync`       |

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
- Copy/paste/duplicate are in ED-M03: snapshot Copy transfers within the
  project, Cut is scene-bound, and `Scene.Node.Duplicate` carries the
  duplicate intent (see the Explorer plan, section 4).
  Copy captures immutable authored values, not source adapters or source-only
  IDs; successful Paste retains the snapshot for repeated use. A successful
  same-project scene switch retains Copy and cancels Cut; a successful project
  switch clears only the Oxygen clipboard. Cancelled switches retain the
  prior payload/staging. These lifetime rules share the command/history owner;
  guarded-switch implementation and verification are closed under C30/T8 in
  the plan. Node/folder/mixed snapshots preserve visual hierarchy and share
  atomic destination validation, authoring history and lineage-driven reparenting.
- Preserve-world-transform reparenting is not ED-M03 default.
