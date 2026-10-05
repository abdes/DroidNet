# D8 — "Show in Editor" and viewport picking: runtime capability analysis

Status: `decided by owner (2026-10-05)`. Contract approved; implementation pending.

| Outcome                                                                                                                                                                                                                        | Remaining                                                                                       | Evidence                                                                                                                                                     |
| ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ----------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| D8 is partitioned into an engine/interop capability (D8a, editing-view hide mask) and an ED-M09 dependency (D8b, viewport picking). Architectural decisions DD1–DD4 are established with a simple "not visible in view" model. | Engine (E1–E3), Interop/Runtime (I1–I4), and Editor (W1–W5) task implementation; qualification. | Source reading of Scene, Vortex, Interop and Runtime boundaries (2026-10-05); owner directives on shadow elimination, warning fallback and gizmo visibility. |

**Summary.** The editor can build the complete "Show in Editor" workspace state,
persistence and row UI immediately ([C49](../plan/scene-explorer-dynamictree-design.md#L1144)).
The eye button maintains state and persistence immediately, logging a diagnostic warning
while native viewport suppression is pending runtime integration.

In the editor viewport, **a node not shown in the editor does not render and does not
participate in shadows** — it behaves as if it is not visible in the scene for that
viewport, without writing `SceneNodeFlags::kVisible` or dirtying the scene document.
In-game rendering, game viewports, scene serialization and cooked assets are completely
unaffected. Viewport picking cannot target editor-hidden nodes, but an editor-hidden node
stays in the scene graph and, when selected (individually or in a multi-selection),
its transform gizmo appears in the viewport for manipulation.

Contents: [1 Terminology](#1-the-boundary-that-must-not-blur) ·
[2 Scope split](#2-what-d8-actually-contains) ·
[3 Current state](#3-current-state-source-verified) ·
[4 Target behaviour contract](#4-target-behaviour-contract) ·
[5 Design decisions](#5-design-decisions) ·
[6 Action plans](#6-action-plans) ·
[7 Sequencing](#7-sequencing) ·
[8 Qualification](#8-qualification)

## 1. The boundary that must not blur

Two unrelated states share the word "visible". Conflating them is a defect
(see [visibility contract §6](ED-M08-node-light-visibility-review.md)).

| Dimension                                         | **Show in Editor** (eye, workspace state)                                                                    | **Scene Visibility** (authored, in-game)          |
| ------------------------------------------------- | ------------------------------------------------------------------------------------------------------------ | ------------------------------------------------- |
| Meaning                                           | Presentation of the node in the _editing viewport_                                                           | Whether the node renders at runtime               |
| Owner                                             | WorldEditor workspace service                                                                                | Scene document / authoring commands               |
| Storage                                           | `WorldEditor/SceneVisibility`, user-local, project-scoped ([settings §5.1](../lld/settings-architecture.md)) | Scene document, cooked output                     |
| History / dirty / cook                            | Undo step; no document dirty; no cook                                                                        | Undo step, dirties document, cooks                |
| Native representation                             | Per-view filter in `CompositionView`                                                                         | `SceneNodeFlags::kVisible` (tri-state, inherited) |
| Effect on editor viewport                         | Node is **not visible in the editor viewport**: no geometry, no shadows cast, no viewport picking            | Node dropped from every pass and view             |
| In-game / game viewports / cooked assets          | **100% visible and unaffected**                                                                              | Node is hidden in-game and in cooked assets       |
| Other views (capture, validation, preview parity) | Unaffected (renders fully)                                                                                   | Affected (hidden)                                 |
| Existing transport                                | None (the D8a capability gap)                                                                                | `RuntimeSetVisibility` → `SetVisibilityCommand`   |

> [!WARNING]
> Never route the eye button through `RuntimeSetVisibility` /
> [`SetVisibilityCommand`](../../../projects/Oxygen.Editor.Interop/src/Commands/SetVisibilityCommand.h).
> That command mutates `SceneNodeFlags::kVisible` on the live scene graph, which
> permanently changes saved content, dirties the document, removes in-game visibility
> and breaks standalone validation and capture parity.

The **Lock** state from the same Explorer contract is purely editor-side and is
not part of D8.

## 2. What D8 actually contains

Decision D8 bundles two distinct capabilities with different owners:

| ID      | Capability                                                                                                                            | Status today         | Owner                                                              | Scope / Dependency                                                                        |
| ------- | ------------------------------------------------------------------------------------------------------------------------------------- | -------------------- | ------------------------------------------------------------------ | ----------------------------------------------------------------------------------------- |
| **D8a** | Editing-view hide mask: suppress geometry, shadows and viewport picking for hidden nodes (and descendants) in editing viewports only. | Absent at all layers | Engine (Vortex) → Interop → Runtime                                | Viewport suppression of the eye; unblocks SE-03 native qualification.                     |
| **D8b** | Viewport picking with Mesh/Light/Camera category filtering and hidden-node exclusion.                                                 | Absent at all layers | ED-M09 ([plan step 2](../plan/ED-M09-viewport-authoring-tools.md)) | Viewport interaction tools; category toggles are filters carried on ED-M09 pick requests. |

D8a is a small, focused engine/interop task. D8b is owned by ED-M09; the Scene Explorer
only provides category preferences and hidden-set inputs to ED-M09 pick requests.

## 3. Current state (source-verified)

### 3.1 Engine — Scene

- [`SceneNodeFlags`](../../../projects/Oxygen.Engine/src/Oxygen/Scene/Types/Flags.h):
  `kVisible`, `kStatic`, `kCastsShadows`, `kReceivesShadows`,
  `kRayCastingSelectable`, `kIgnoreParentTransform`. These are scene-wide authored
  flags with inheritance; there is no per-view concept. `kRayCastingSelectable` has no
  consumer outside tests and string conversion.

### 3.2 Engine — Vortex render path

- [`ScenePrepPipeline::BeginFrameCollection`](../../../projects/Oxygen.Engine/src/Oxygen/Vortex/ScenePrep/ScenePrepPipeline.cpp):
  Performs scene traversal once per frame, caching renderable nodes surviving
  [`ExtractionPreFilter`](../../../projects/Oxygen.Engine/src/Oxygen/Vortex/ScenePrep/Extractors.h)
  (which checks authored `SceneNodeFlags::kVisible`).
- [`ScenePrepPipeline::PrepareView`](../../../projects/Oxygen.Engine/src/Oxygen/Vortex/ScenePrep/ScenePrepPipeline.cpp#L121):
  Executes per view over the cached nodes. This is the natural, clean seam:
  filtering out editor-hidden nodes here simply drops them from that view's item collection.
  When dropped for that view, they produce no geometry draws, no depth prepass, no base pass,
  no translucency and no shadow-caster sources for that view.
- [`CompositionView`](../../../projects/Oxygen.Engine/src/Oxygen/Vortex/CompositionView.h):
  Represents per-view rendering intent; currently has coarse feature masks
  (`ViewFeatureMask`) but no per-view node exclusion filter.

### 3.3 Interop

- [`EditorModule::OnPublishViews`](../../../projects/Oxygen.Editor.Interop/src/EditorModule/EditorModule.cpp):
  Builds `CompositionView::ForScene` for each registered
  [`EditorView`](../../../projects/Oxygen.Editor.Interop/src/EditorModule/EditorView.h)
  and calls `PublishRuntimeCompositionView`. `EditorView::Show/Hide` toggle the whole
  view window, not individual scene nodes.
- [`OxygenWorld`](../../../projects/Oxygen.Editor.Interop/src/World/OxygenWorld.h):
  Exposes `SetVisibility` (authored flag write) and has no view-mask API. Node identity
  crosses the interop boundary as authored `Guid`s and resolves to `NodeHandle` through
  [`NodeRegistry`](../../../projects/Oxygen.Editor.Interop/src/EditorModule/NodeRegistry.h).
- No gizmo, light/camera icon, selection-highlight or picking implementation exists in
  `Oxygen.Editor.Interop/src`.

### 3.4 Managed runtime

- [`IRuntimeWorldCommands`](../../../projects/Oxygen.Editor.Runtime/src/Engine/IRuntimeWorldCommands.cs):
  Dispatches immutable `RuntimeWorldRequest(OperationId, RuntimeSceneTarget, Command)`
  with activation staleness checks. `RuntimeSetVisibility` is the only visibility command.
- [`IRuntimeInputCommands`](../../../projects/Oxygen.Editor.Runtime/src/Engine/IRuntimeInputCommands.cs):
  Dispatches mouse/keyboard events for a view; has no pick query or category filter.

### 3.5 Editor

- Owning contracts exist: [settings §5.1](../lld/settings-architecture.md),
  [live-engine-sync §8.1](../lld/live-engine-sync.md) ("Editor Hide"),
  [visibility contract §1](ED-M08-node-light-visibility-review.md), and the owner's
  "Show in Editor & Lock" contract in the
  [Explorer plan](../plan/scene-explorer-dynamictree-design.md).
- `WorldEditor/SceneVisibility` persistence and the workspace interaction service are
  currently unbuilt (tracked under task C49).

## 4. Target behaviour contract

Normative contract for D8a and D8b:

| ID  | Requirement                                                                                                                                                                                                                                                                                                                                         |
| --- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| R1  | A node is _editor-hidden_ when it, or any **actual scene ancestor**, is in the scene's local hidden set. Logical folders never contribute.                                                                                                                                                                                                          |
| R2  | In every editing viewport of the active scene, editor-hidden nodes **do not render and do not cast shadows**. They are simply dropped from that view.                                                                                                                                                                                               |
| R3  | Editor-hidden nodes remain **100% visible in-game, in game viewports, in cooked assets, and in capture/validation views**.                                                                                                                                                                                                                          |
| R4  | Changing editor hide never writes `SceneNodeFlags::kVisible`, never dirties the scene document, and never requests cooking. The eye toggle records an undo step.                                                                                                                                                                                    |
| R5  | The mask is bound to (run, project, scene activation). Applying it to an inactive scene is rejected; unknown node IDs have no rendering effect but are retained so (re)created nodes with that ID are correctly hidden.                                                                                                                             |
| R6  | One mask applies to all editing viewports of the active scene. Non-editing views (capture, thumbnail, standalone validation) remain unmasked.                                                                                                                                                                                                       |
| R7  | Mask updates take effect no later than the next frame presented. Ordering is consistent with preceding world commands.                                                                                                                                                                                                                              |
| R8  | **Picking exclusion**: Editor-hidden nodes cannot be targeted or picked in viewports. Explorer selection of hidden nodes remains fully functional.                                                                                                                                                                                                  |
| R9  | **Gizmo on Explorer selection**: An editor-hidden node stays in the scene graph and is selectable and transformable. When selected (individually or in a multi-selection), its transform gizmo (translate, rotate, scale per active viewport mode) **must appear** in the viewport at its world transform, even though its geometry remains hidden. |

## 5. Design decisions

### DD1 — Where the engine applies the mask

- **Decision (Approved):** Filter in `CompositionView`, dropped during `PrepareView`.
- **Implementation:** An editing view passes its list of hidden `NodeHandle`s in `CompositionView`. In `ScenePrepPipeline::PrepareView`, matching nodes are skipped. They produce no geometry, no depth, and no shadow-caster sources for that view. Other views (capture, game) remain unaffected.
- **Industry Parity Basis:** This follows standard industry architecture. In Unreal Engine (UE5.7), editor viewport actor hiding (`bHiddenEd` / "Hide Selected") completely removes the actor from that editor viewport, including its direct shadows, without mutating runtime `bHidden` or dirtying the level package, while remaining undoable. In Oxygen, an editor-hidden node simply does not render and does not cast shadows in the editing viewport.
- **Alternatives Evaluated:**
  - _Node tag / component in scene graph:_ Rejected. Pollutes scene data with transient editor state; leaks into capture views and game runtime.
  - _Late filtering in mesh processors:_ Rejected. Unnecessarily complex; requires plumbing exclusion lists through multiple rendering passes.

### DD2 — Descendant closure resolution

- **Decision (Approved):** The editor transmits the set of explicitly hidden root `Guid`s. The engine resolves the descendant closure once per frame when the set is non-empty.
- **Rationale:** If a child is created or reparented under a hidden node, it is automatically hidden in the engine on the next frame without requiring re-synchronization from the editor.

### DD3 — Transport and scope

- **Decision (Approved):** Revisioned full snapshot (`RuntimeSetEditingViewHiddenNodes`) targeted at the active scene. The interop layer applies this mask to all editing views belonging to that scene activation.
- **Rationale:** One mask applies to all editing viewports of the active scene. Avoids delta drift; guarantees all viewport splits/panes show consistent hide state without per-pane synchronization overhead.

### DD4 — Category picking filters and gizmos (D8b)

- **Decision (Approved):** Viewport picking is owned by ED-M09. Category toggles (Mesh, Light, Camera) and hidden-node exclusion are passed as filter arguments on each pick request, rather than stored as native engine state.
- **Interaction Contract:** Editor-hidden nodes cannot be picked in viewports. They stay in the scene graph and transform normally; when selected (individually or in a multi-selection), the viewport renders their transform gizmo at their world transform, allowing spatial manipulation while geometry stays hidden.

## 6. Action plans

### 6.1 Engine team (D8a)

| ID  | Task                                                                                                                                                                                    | Files / Area                                  | Acceptance                                                                                                         |
| --- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------- | ------------------------------------------------------------------------------------------------------------------ |
| E1  | Add a per-view hidden-node filter to `CompositionView` (borrowed `std::span<const scene::NodeHandle>` of hidden roots plus revision).                                                   | `Vortex/CompositionView.h`, `FrameViewPacket` | Zero overhead when empty; no regression in existing tests.                                                         |
| E2  | In `ScenePrepPipeline::PrepareView`, resolve descendant closure of hidden roots and skip matching nodes. Dropped nodes emit no render items and no shadow-caster sources for that view. | `ScenePrepPipeline.cpp`, `Extractors.h`       | Unit test: hidden mesh produces no draw calls and no shadow casters in the view; unmasked view still renders them. |
| E3  | Document the per-view hidden filter in Vortex design documentation, citing the UE5.7 parity basis.                                                                                      | `design/vortex/`                              | Documentation review.                                                                                              |

### 6.2 Interop and Runtime team (D8a)

| ID  | Task                                                                                                                                                                                                                                    | Files / Area                        | Acceptance                                                                         |
| --- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------- | ---------------------------------------------------------------------------------- |
| I1  | Native: `EditorModule::SetEditingViewHiddenNodes(revision, ids)` enqueued on the command queue. Store authored IDs per scene activation; discard on scene teardown.                                                                     | `EditorModule.h/.cpp`, `Commands/`  | Preserves order with scene mutation commands.                                      |
| I2  | Resolve authored `Guid`s → `NodeHandle` via `NodeRegistry` and populate E1's filter in `OnPublishViews` for editing views.                                                                                                              | `EditorModule.cpp`, `EditorView.h`  | Translates valid nodes; retains unresolved IDs; leaves non-editing views unmasked. |
| I3  | C++/CLI: Expose `OxygenWorld::SetEditingViewHiddenNodes(UInt64 revision, array<Guid>^ ids)`.                                                                                                                                            | `OxygenWorld.h/.cpp`                | Compiles with installed SDK consumer profile.                                      |
| I4  | Managed: Add `RuntimeSetEditingViewHiddenNodes(long Revision, IReadOnlyList<Guid> HiddenRoots) : RuntimeWorldCommand` dispatched via `NativeRuntimeCommandTransport`. Expose `bool SupportsEditingViewMask` on `IRuntimeWorldCommands`. | `Oxygen.Editor.Runtime/src/Engine/` | `RuntimeDependencyTests` pass; no interop types leaked.                            |

### 6.3 Editor team

**Workable now (C49 — completely unblocked):**

| ID  | Task                                                                                                                                                                                                                                                                                                                                                                    | Files / Area                        | Acceptance                                                                                                 |
| --- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------- | ---------------------------------------------------------------------------------------------------------- |
| W1  | Implement workspace interaction service owning Show in Editor and Lock state per project/scene; typed `WorldEditor/SceneVisibility` persistence in SQLite via `IEditorSettingsManager` ([settings §5.1](../lld/settings-architecture.md)). The eye toggle is recorded through the document command owner so it is undoable; Lock stays workspace-only and non-undoable. | `WorldEditor/src/Workspace/`        | Persist/reload survives restart; project-scoped; eye toggle undoes and redoes; no scene document dirtying. |
| W2  | Compute effective hide state from actual scene hierarchy (R1); Show All clears only the current scene's set.                                                                                                                                                                                                                                                            | `SceneExplorer/` adapters           | Unit tests covering reparenting, folders, and nested hidden roots.                                         |
| W3  | Wire Explorer eye slot in DynamicTree per interaction contract (hover/permanent visual matrix).                                                                                                                                                                                                                                                                         | `DynamicTree/`, `SceneExplorerView` | Visual states match contract.                                                                              |
| W4  | Viewport binding with warning fallback: On mask change, attempt dispatch to `IRuntimeWorldCommands`. If `SupportsEditingViewMask` is false, log a warning: `"Editor hide applied in workspace; viewport suppression pending runtime capability."`                                                                                                                       | `WorldEditor/src/Services/`         | Eye works cleanly in UI and settings without crashing or throwing.                                         |

**Gated on D8a runtime availability (I4):**

| ID  | Task                                                                        | Files / Area   | Acceptance                                     |
| --- | --------------------------------------------------------------------------- | -------------- | ---------------------------------------------- |
| W5  | Connect W4 to live native dispatch; verify viewport suppression end-to-end. | `WorldEditor/` | Viewport hides geometry and shadows on toggle. |

**Gated on ED-M09 (D8b):**

| ID  | Task                                                                                                                             | Files / Area         | Acceptance                                           |
| --- | -------------------------------------------------------------------------------------------------------------------------------- | -------------------- | ---------------------------------------------------- |
| W6  | Pass category filters (Mesh, Light, Camera) and hidden set into ED-M09 pick requests. Hidden nodes cannot be picked in viewport. | `Viewport/`          | Viewport picking ignores hidden nodes.               |
| W7  | When an editor-hidden node is selected in Scene Explorer, display its transform gizmo in the viewport at its world transform.    | `Viewport/Overlays/` | Transform gizmo renders and manipulates hidden node. |

## 7. Sequencing

```mermaid
flowchart TD
  subgraph Editor_C49 [Editor Team: C49 (Immediate)]
    W1["W1 Workspace Service & Settings"] --> W2["W2 Hierarchy Calculation"]
    W2 --> W3["W3 Explorer Eye UI Slot"]
    W3 --> W4["W4 Binding + Warning Fallback"]
  end

  subgraph Engine_D8a [Engine & Runtime: D8a]
    E1["E1 CompositionView Filter"] --> E2["E2 ScenePrep PrepareView Culling"]
    E2 --> I1["I1 EditorModule Command"]
    I1 --> I2["I2 NodeRegistry Lookup"]
    I2 --> I3["I3 OxygenWorld Interop"]
    I3 --> I4["I4 Managed Command + Flag"]
  end

  subgraph Integration [End-to-End Validation]
    W4 -.->|When I4 lands| W5["W5 Live Viewport Suppression"]
  end

  subgraph ED_M09 [ED-M09 Scope: D8b]
    M1["ED-M09 Viewport Picking"] --> W6["W6 Picking Category Filter"]
    M2["ED-M09 Transform Gizmos"] --> W7["W7 Gizmo on Hidden Node Selection"]
  end
```

## 8. Qualification

| Test Case                                 | Expected Behavior                                                                                                                      |
| ----------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------- |
| Hide a mesh node in editor                | Mesh geometry and its shadows are absent in the editing viewport. Game viewports and in-game renders retain both.                      |
| Hide a parent node                        | Entire descendant subtree and its shadows are absent in editing viewports.                                                             |
| Show parent of hidden child               | Parent reappears; child remains hidden.                                                                                                |
| Select hidden node in Explorer            | Geometry remains absent in viewport; transform gizmo appears at its location and allows manipulation.                                  |
| Multi-select transform with a hidden node | Hidden node's gizmo participates in the shared multi-selection transform; its geometry stays hidden while other selected nodes render. |
| Viewport click on hidden node             | Raycast passes through to objects behind it; hidden node is not picked.                                                                |
| Toggle authored Scene Visibility          | Scene document dirties, undo step created, in-game visibility toggled. Operates completely independently of editor hide.               |
| Standalone validation / capture           | Capture view ignores editor hide; renders exact authored scene state.                                                                  |
| Close and reopen project                  | Stored editor-hide state is restored from SQLite settings without marking scene document dirty.                                        |
