# SceneExplorer Refactor & Test Plan

Scope: Phase-1 refactor to split scene vs layout operations and harden UX/undo. Includes implementation steps and matching tests. Keep code changes small and MVVM-aligned.

## EXPECTATIONS

- User of scene explorer should be able to add a node under root or as a child of anaother non-layout node)
- Removing a node should remove it from layout and from scene. If a node has children the entire hierarchy should be removed. The engine (runtime c++) treats operations differently, see #file:Scene.h for single node, root nodes, hierarchies of nodes. The engine sync service is incomplete and you need to augment it.
- User should be able to move a node, or a hierarchy of nodes, under a new parent (called reparent node), triggers scene mutation, pay attention to the nuances between reparent node vs reparent hierarchy
- User should be able to move a node to a folder - does not trigger scene mutation
- User cannot move a child node to folder that is not under its parent node
- When user moves a hierarchy of nodes to a valid folder (under part of hierarchy), the entire hierarchy is moved - does not trigger scene mutation
- selecting multiple nodes, and invoking the create folder from node will move all the nodes/hierarchies to the new folder. The folder will be added under to highest level common parent (or root)

## Implementation Tasks

- [x] Introduce core abstractions
  - [x] Add `ISceneMutator` + `SceneMutator` with `SceneNodeChangeRecord` (scene graph only, engine-sync required).
  - [x] Add `ISceneOrganizer` + `SceneOrganizer` with `LayoutChangeRecord` (ExplorerLayout only, no engine sync).
  - [x] Register both in DI for WorldEditor module.
- [ ] Introduce layout adapter layer (layout-only tree)
  - [x] Define immutable `SceneAdapter` as layout root that can host folders and layout nodes (no scene mutation APIs).
  - [x] Define `FolderAdapter` as layout-only node (folders and layout nodes as children; no scene payload).
  - [x] Define `LayoutNodeAdapter` wrapping a `SceneNodeAdapter` payload; moving layout nodes never mutates scene.
  - [x] Move layout operations (create/move folder, move layout node) into `SceneOrganizer` against the new adapters.
  - [x] Adjust layout persistence/build to materialize folders and layout nodes separately from scene graph.
- [ ] ViewModel consumes layout tree
  - [x] `SceneExplorerViewModel` binds to layout adapters; delegates layout ops to organizer, scene ops to mutator.
  - [x] Selection, commands, and undo/redo operate on layout adapters; mutator invoked only for scene mutations.
  - [x] Clear path for folder creation/move using layout nodes without touching `SceneNodeAdapter` hierarchy.
- [ ] Staged rollout (incremental slices)
  - [x] Slice 1: introduce new adapters and layout builder; keep VM on old tree behind adapter shim; no behavior change.
  - [x] Slice 2: move `SceneOrganizer` to operate on layout tree; wire persistence and folder ops; add layout adapter tests.
  - [x] Slice 3: switch `SceneExplorerViewModel` to layout adapters; route scene ops via mutator; keep feature parity.
  - [ ] Slice 4: clean up legacy coupling flags and unused pathways; tighten lineage validation and undo/redo on layout tree.
- [x] Refactor ViewModel event routing
  - [x] Update `SceneExplorerViewModel` ctor to accept mutator/organizer deps.
  - [x] `OnItemBeingAdded`: dispatch by parent type (scene root, scene node, folder) to mutator/organizer; capture change records.
  - [x] `OnItemBeingRemoved`: respect delete vs move flags (`isPerformingDelete`, `capturedSceneParentDuringMove`, `adaptersPendingEngineRemoval`); delegate to mutator/organizer.
  - [x] `OnItemAdded/Removed`: record undo via change records; send messages; schedule engine sync only when `RequiresEngineSync`.
- [ ] Scene graph semantics
  - [x] Mutator supports hierarchy-aware ops: remove node hierarchy (not single-node only), reparent hierarchy vs single-node, create child under selected node.
  - [ ] ViewModel and mutator honor "folder == layout only"; creation blocked on folder selection; root fallback when selection invalid.
  - [ ] Reparent to valid targets only; enforce child-to-folder under same lineage (no cross-branch folder drop).
- [ ] Engine sync and messaging pipeline
  - [ ] Centralize engine calls (create/remove/reparent) behind helpers consuming change records.
  - [ ] Log and surface engine failures; no swallowing.
  - [ ] Ensure layout-only paths never call engine.
  - [ ] Add hierarchy sync APIs (create/remove/reparent hierarchies, batch roots) to `ISceneEngineSync` + impl; map to engine capabilities.
  - [ ] Add OxygenWorld interop entry points for hierarchy removal/reparent (C++/CLI bridge) and wire through SceneEngineSync.
- [ ] Layout/UX reliability
  - [ ] Replace full rebuilds with in-place `RefreshLayoutAsync` reconciliation; preserve scroll/expansion.
  - [ ] Capture/restore expansion state around layout-affecting ops and undo/redo.
  - [ ] Ensure nested folder creation/lookup initializes children; use recursive adapter builder.
  - [ ] Validate layout before applying (especially undo); safe fallback to flat layout if invalid.
  - [x] Folder creation from multi-select: compute highest common ancestor (or root) for folder placement; move selected hierarchies intact into folder.
  - [x] Moving node/hierarchy to folder keeps hierarchy intact and only when folder under same lineage; move adapters accordingly.
  - [ ] Update `SceneAdapter.LoadChildren` to build folders within node branches (not only root) so lineage-aware folder placement is possible.
- [ ] Undo/redo hardening
  - [ ] Use atomic change recording; remove double reconstruction during undo.
  - [ ] Wrap undo actions with try/log; optional notification on failure.
- [ ] Naming clarity & guards
  - [ ] Keep explicit flag names (`capturedSceneParentDuringMove`, `adaptersPendingEngineRemoval`, `suppressUndoRecording`).
  - [ ] Assert mutator never touches layout; organizer never touches `RootNodes`.

## Testing Tasks (Oxygen.Editor.WorldEditor.SceneExplorer.Tests)

- [ ] Harness/setup
  - [ ] Builders/fixtures for `Scene`, `SceneNode`, `ExplorerEntryData`, adapters; DI/mocks via Moq for `ISceneEngineSync`, dispatcher, messenger.
- [ ] SceneMutator unit tests (pure logic)
  - [x] Create node at root: roots updated, parent cleared, change record flags set, engine sync required.
  - [x] Create node under parent: parent set, removed from `RootNodes` if present.
  - [x] Remove node: throws when missing; clears parent; updates `RootNodes` flags.
  - [x] Reparent node: root↔node transitions adjust `RootNodes`; ids captured in change record.
- [ ] SceneOrganizer unit tests (layout-only)
  - [x] Move node to folder: layout updates, no `RootNodes` change, folder parent constraints enforced.
  - [x] Move folder to parent/root: valid nesting only; rejects invalid parents.
  - [x] Remove folder with/without promotion: children handled per flag; layout integrity validated.
  - [x] Create folder from selection: ids placed correctly; returns new layout + folder reference.
- [ ] ViewModel integration tests
  - [x] `OnItemBeingAdded`: routes correctly; engine sync invoked only when required; change records consumed.
  - [ ] `OnItemBeingRemoved`: delete vs move semantics via flags; mutator vs organizer path selection.
  - [x] Undo/redo: uses change records, preserves expansion/selection, no double rebuild.
- [ ] Layout adapter tests
  - [x] Layout tree build from ExplorerLayout yields expected `SceneAdapter`/`FolderAdapter`/`LayoutNodeAdapter` structure.
  - [x] Moving layout nodes/folders updates layout only; scene graph untouched.
  - [x] Folder creation from selection uses highest common ancestor and preserves hierarchies in layout nodes.
  - [x] Lineage guard: moving a child to a folder outside its lineage is rejected.
- [ ] Hierarchy and folder regression
  - [ ] Removing node with children removes entire hierarchy (scene + layout) and calls hierarchy engine removal.
  - [ ] Reparent node/hierarchy updates scene + engine; rejects invalid targets; preserves hierarchy integrity.
  - [ ] Move node/hierarchy to folder leaves scene graph untouched; enforces lineage constraint; layout updated and adapters moved.
  - [ ] Create folder from multi-select places folder at highest common ancestor (or root), moves whole hierarchies, layout + adapters updated.
- [ ] Layout/UX regression
  - [ ] Nested folder creation/lookup works; children initialized before search.
  - [ ] Expansion state preserved across folder ops and undo/redo (no collapse/flicker simulation).
  - [ ] Layout validation on undo: invalid layouts fall back safely.
  - [x] SceneAdapter layout fallback uses root nodes when no ExplorerLayout; FolderAdapter add/remove keeps realized children in sync when expanded.
- [ ] Messaging & engine sync validation
  - [ ] Messages emitted per operation (add/remove/reparent) after successful engine sync.
  - [ ] Engine sync errors logged; layout-only ops never call engine.

## Success Criteria

- Mutator/organizer are fully unit tested and UI-free.
- ViewModel delegates operations through change records; engine sync only on scene mutations.
- Undo/redo preserves expansion/selection; no tree flicker or collapse.
- Nested folders and layout operations are reliable; invalid layouts handled safely.
