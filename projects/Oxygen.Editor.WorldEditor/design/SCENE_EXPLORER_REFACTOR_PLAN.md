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
  - [x] Slice 4: clean up legacy coupling flags and unused pathways; tighten lineage validation and undo/redo on layout tree.
- [x] Refactor ViewModel event routing
  - [x] Update `SceneExplorerViewModel` ctor to accept mutator/organizer deps.
  - [x] `OnItemBeingAdded`: dispatch by parent type (scene root, scene node, folder) to mutator/organizer; capture change records.
  - [x] `OnItemBeingRemoved`: respect delete vs move flags (`isPerformingDelete`, `capturedSceneParentDuringMove`, `adaptersPendingEngineRemoval`); delegate to mutator/organizer.
  - [x] `OnItemAdded/Removed`: record undo via change records; send messages; schedule engine sync only when `RequiresEngineSync`.
- [ ] Scene graph semantics
  - [x] Mutator supports hierarchy-aware ops: remove node hierarchy (not single-node only), reparent hierarchy vs single-node, create child under selected node.
  - [x] ViewModel and mutator honor "folder == layout only"; creation blocked on folder selection; root fallback when selection invalid.
  - [x] Reparent to valid targets only; enforce child-to-folder under same lineage (no cross-branch folder drop).
- [ ] Engine sync and messaging pipeline
  - [x] Centralize engine calls (create/remove/reparent) behind helpers consuming change records.
  - [x] Log and surface engine failures; no swallowing.
  - [x] Ensure layout-only paths never call engine.
  - [x] Add hierarchy sync APIs (create/remove/reparent hierarchies, batch roots) to `ISceneEngineSync` + impl; map to engine capabilities.
  - [x] Add OxygenWorld interop entry points for hierarchy removal/reparent (C++/CLI bridge) and wire through SceneEngineSync.
  - [x] Implement `RemoveSelectedItems` override to batch engine calls using `RemoveNodeHierarchiesAsync`.
- [ ] Layout/UX reliability
  - [ ] Replace full rebuilds with in-place `RefreshLayoutAsync` reconciliation; preserve scroll/expansion.
  - [x] Capture/restore expansion state around layout-affecting ops and undo/redo.
  - [ ] Ensure nested folder creation/lookup initializes children; use recursive adapter builder.
  - [ ] Validate layout before applying (especially undo); safe fallback to flat layout if invalid.
  - [x] Folder creation from multi-select: compute highest common ancestor (or root) for folder placement; move selected hierarchies intact into folder.
  - [x] Moving node/hierarchy to folder keeps hierarchy intact and only when folder under same lineage; move adapters accordingly.
  - [ ] Update `SceneAdapter.LoadChildren` to build folders within node branches (not only root) so lineage-aware folder placement is possible.
- [ ] Undo/redo hardening
  - [ ] Use atomic change recording; remove double reconstruction during undo.
  - [ ] Wrap undo actions with try/log; optional notification on failure.

## Testing Tasks (Oxygen.Editor.WorldEditor.SceneExplorer.Tests)

- [x] **Infrastructure**
  - [x] Fixtures for `Scene`, `SceneNode`, `ExplorerEntryData`, adapters; DI/mocks.

- [x] **Unit Tests (Logic)**
  - [x] `SceneMutator`: Create/Remove/Reparent (Single & Hierarchy), Root node management.
  - [x] `SceneOrganizer`: Folder Create/Move, Lineage constraints, Layout integrity.

- [ ] **ViewModel Integration Tests (Orchestration)**
  - [x] **Scene Operations**
    - [x] Add Node: Routes to mutator, syncs engine.
    - [x] Remove Node: Routes to mutator, syncs engine, handles flags.
    - [x] Reparent Node: Routes to mutator, syncs engine.
    - [x] Batch Remove: `RemoveSelectedItems` batches engine calls (`RemoveNodeHierarchiesAsync`).
  - [ ] **Layout Operations**
    - [x] Create Folder from Selection: Moves items, updates layout.
    - [x] Move to Folder: Updates layout, enforces lineage.
    - [ ] **Layout-Only Guard**: Verify folder operations (create, move) do *not* trigger `ISceneEngineSync`.
  - [ ] **Undo/Redo**
    - [x] Basic Add/Remove.
    - [ ] **Complex Undo**: Verify Undo/Redo of "Create Folder from Selection" restores original layout and selection.

- [ ] **Layout Adapter Tests**
  - [x] Tree construction from `ExplorerLayout`.
  - [x] Adapter state (Expansion/Selection) preservation.

## Mental Notes

- Solve the issue with MVVM messenger channel tokens. What token should be used for the world editor, how tokens are managed.
