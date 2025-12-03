# Multi-View Rendering: Implementation Plan

**Status**: Draft Work Plan
**Last Updated**: 2025-12-03

---

## Progress Update (2025-12-03)

- **Work done:** Two-stage ScenePrep (frame-phase + view-phase) implemented in `Renderer::BuildFrame`. `ScenePrepPipeline` now accepts an optional view and `ScenePrepContext` can be constructed without a view. Pipeline guards were added so view-dependent extractors (`mesh_resolver`, `visibility_filter`, `producer`) run only when a view is present. Defensive checks were added in `MeshResolver` to avoid null view dereference.
- **State fixes:** `ScenePrepState::ResetFrameData()` now clears the cached `filtered_scene_nodes_` to prevent unbounded growth. `ResetViewData()` exists for per-view transient clearing.
- **Partial changes:** `DrawMetadataEmitter` had adjustments toward per-view behavior (removed `uploaded_this_frame_` gating and ensured SRV index handling). Further per-view lifecycle work remains.

**Files touched (key):**

- `src/Oxygen/Renderer/Renderer.cpp` (two-stage ScenePrep call)
- `src/Oxygen/Renderer/ScenePrep/ScenePrepPipeline.h/.cpp` (optional view, pipeline guards)
- `src/Oxygen/Renderer/ScenePrep/ScenePrepState.h` (reset fixes + filtered node cache)
- `src/Oxygen/Renderer/ScenePrep/ScenePrepContext.h` (optional observer_ptr view)
- `src/Oxygen/Renderer/ScenePrep/Extractors.h` (defensive early-return in `MeshResolver`)
- `src/Oxygen/Renderer/Resources/DrawMetadataEmitter.*` (small adjustments)


---

---

## Phase 0: Type Renaming (View → ResolvedView)

### 0.1 Rename Current View Class to ResolvedView

**File**: `src/Oxygen/Core/Types/View.h`

- Rename class `View` → `ResolvedView`
- Update all usages throughout codebase (ScenePrepPipeline, RenderContext, etc.)
- ResolvedView contains: view/proj matrices, frustum, viewport, scissor, jitter, camera position

### 0.2 Create New View Type (Future)

**File**: `src/Oxygen/Core/Types/View.h` (or separate file)

- Create `View` struct/class for view configuration only
- Contains: viewport, scissor, jitter settings, flags
- Does NOT contain: camera matrices, frustum, camera position

**Note**: Phase 0.2 can be deferred if ViewResolver directly constructs ResolvedView

### 0.3 Rename PreparedSceneFrame to PreparedScene

**File**: `src/Oxygen/Renderer/PreparedSceneFrame.h`

- Rename `PreparedSceneFrame` → `PreparedScene`
- Update all usages throughout codebase (RenderContext, Renderer, DrawMetadataEmitter, etc.)
- Move from frame-level (`RenderContext.prepared_frame`) to view-level (`ViewSpecific.prepared_scene`)
- PreparedScene represents per-view scene prep results (draw metadata, transforms, partitions)

**Rationale**: Scene prep is now per-view (Mode B), not per-frame. Name should reflect scope.

---

## Phase 1: Core RenderContext Multi-View Support

### 1.1 Add Multi-View State to RenderContext

**File**: `src/Oxygen/Renderer/RenderContext.h`

**Design Decision**: Application owns View (guaranteed alive during render). Renderer receives View by reference and stores observer_ptr (non-owning). No shared_ptr needed (View is a value snapshot, not shared ownership).

Add inner struct for view-specific state:

```cpp
struct ViewSpecific {
    ViewId view_id { kInvalidViewId };
    observer_ptr<const ResolvedView> resolved_view; // Non-owning, application-owned
    observer_ptr<const PreparedScene> prepared_scene; // Per-view scene prep results
};
```

Add to RenderContext:

- `ViewSpecific current_view` - All view-specific iteration state in one place
- `std::unordered_map<ViewId, std::shared_ptr<graphics::Framebuffer>> view_outputs` - Completed view framebuffers (frame-scoped, not view-scoped)

**Rationale**: Clear separation between view-specific and frame-wide state. No clutter. Application lifetime guarantees eliminate need for shared_ptr.

**Changes to existing RenderContext:**

- Remove `observer_ptr<const PreparedSceneFrame> prepared_frame` (frame-level, obsolete)
- Replaced by `ViewSpecific.prepared_scene` (view-level)

**Dependencies**: Phase 0.3 (PreparedSceneFrame → PreparedScene rename)

### 1.2 Update RenderContext::Reset()

Clear multi-view state at frame boundaries:

- `current_view = {}` - Reset ViewSpecific to default state
- `view_outputs.clear()` - Clear completed view framebuffers

---

## Phase 2: Dual-Mode Scene Prep Pipeline

### 2.1 Refactor ScenePrepPipeline::Collect Signature

**File**: `src/Oxygen/Renderer/ScenePrep/ScenePrepPipeline.h`

Change from:

```cpp
auto Collect(const scene::Scene& scene, const ResolvedView& view, ...) -> void;
```

To:

```cpp
auto Collect(const scene::Scene& scene, std::optional<ResolvedView> view, ...) -> void;
```

**Rationale**: Enable Mode A (Frame Phase, `view == nullopt`) and Mode B (View Phase, `view.has_value()`)

### 2.2 Implement ScenePrepState::ResetViewData()

**File**: `src/Oxygen/Renderer/ScenePrep/ScenePrepState.cpp`

- Clear per-view transient data (draw metadata, retained indices)
- Preserve `collected_items_` across views (populated in Frame Phase)

### 2.3 Update Collection Logic for Dual Mode

**File**: `src/Oxygen/Renderer/ScenePrep/ScenePrepPipeline.cpp`

**Mode A (Frame Phase, no view)**:

- Traverse all active renderables
- Skip `SubMeshVisibilityFilter` (no frustum culling)
- Run `TransformResolve`, `MaterialBinder`
- Populate `collected_items_` (existing member)
- Finalize: Upload Transforms/Materials via `TransientStructuredBuffer`

**Mode B (View Phase, view provided)**:

- Iterate `collected_items_` (fast, no scene traversal - already populated in Frame Phase)
- Run `SubMeshVisibilityFilter` with view frustum
- Populate `retained_indices_` with visible items
- Run `DrawMetadataEmitter` for retained items
- Finalize: Sort and upload `DrawMetadata` via `TransientStructuredBuffer`

---

## Chosen Minimal Implementation Path — Option 1 (ViewResolver parameter)

Decision: implement Option 1 only — the renderer will accept a `ViewResolver` parameter to `RenderFrame` rather than adding a resolver into `FrameContext`. No changes will be made to `FrameContext` as part of this change set.

Rationale: Passing the resolver into `RenderFrame` keeps `FrameContext` focused on view registration and metadata, avoids adding longer-lived per-frame callables to the context, and explicitly scopes the resolver lifetime to the render call.

The steps below are the prioritized, minimal-surface edits to implement Option 1.

1) Add per-view state to `RenderContext` (low risk)
    - Files: `src/Oxygen/Renderer/RenderContext.h`
    - Add `struct ViewSpecific { ViewId view_id; observer_ptr<const ResolvedView> resolved_view; observer_ptr<const PreparedSceneFrame> prepared_frame; };`
    - Add `ViewSpecific current_view; std::unordered_map<ViewId, std::shared_ptr<graphics::Framebuffer>> view_outputs;`
    - Rationale: Keep the existing `PreparedSceneFrame` type to avoid a large rename; treat it as view-specific for now. This is a minimal change that allows per-view wiring without repo-wide refactors.

2) Add `Renderer::RenderFrame(FrameContext&, RenderContext&, const oxygen::ViewResolver&)` (medium risk)
    - Files: `src/Oxygen/Renderer/Renderer.cpp`, `Renderer.h`
    - Semantics: `RenderFrame` will:
      - run Frame-phase ScenePrep once (`scene_prep_->Collect(scene, std::nullopt, ...)` + `Finalize()`)
      - iterate `FrameContext`'s registered view ids (use existing `views_` map)
      - for each `view_id`, call `resolver(view_id)` to obtain a `ResolvedView`, run per-view ScenePrep (`Collect` with view + `Finalize()`), bind per-view prepared data into `RenderContext::current_view`, execute render graph for that view, and publish the output via `FrameContext::SetViewOutput(view_id, fb)`.
    - Note: `BuildFrame` will be retained as an internal view-phase helper and moved to `private` scope in `Renderer` to avoid leaking internals to callers.

3) Make `DrawMetadataEmitter` per-view friendly (low-medium risk)
    - Files: `src/Oxygen/Renderer/Resources/DrawMetadataEmitter.*`
    - Changes: clear CPU list at the start of each view-phase, ensure `EnsureFrameResources()` is safe to call per-view, and return per-view SRV index used so the renderer can bind the per-view SRV.

4) Minimal upload coordination (medium risk)
    - Files: `ScenePrepPipeline.*`, `Renderer.cpp`, upload/InlineTransfersCoordinator integration points
    - Changes: For correctness, block on per-view upload completion before executing that view's render graph. Later replace with ticket-based non-blocking waits.

5) Examples & tests (low risk)
    - Update Async example to set up a `ViewResolver` (lambda) and call `renderer.RenderFrame(frame_ctx, render_ctx, resolver)`.
    - Add unit tests for Frame-phase-only vs View-phase behavior and for `ResetFrameData()` clearing `filtered_scene_nodes_`.

These edits keep the change surface small, avoid modifying `FrameContext`, and make `RenderFrame` the single authoritative entry point for rendering.

---

## Longer-term Improvements (after minimal path)

- Rename `PreparedSceneFrame` → `PreparedScene` and update all usages for clear per-view semantics.
- Introduce explicit per-view `PreparedScene` lifecycle and ownership types instead of the temporary pointer-in-context approach.
- Migrate `DrawMetadataEmitter` to a per-view allocation model with explicit per-view upload tickets and non-blocking retire semantics.
- Add comprehensive multi-view benchmarks and profiling (memory/upload/per-view cost).

---

## Phase 3: Per-View Draw Metadata Allocation

### 3.1 Migrate DrawMetadataEmitter to Per-View Allocation

**File**: `src/Oxygen/Renderer/Resources/DrawMetadataEmitter.cpp`

**Current**: Single `TransientStructuredBuffer` allocated once per frame
**Target**: Allocate per view to support distinct draw lists

**Changes**:

- Call `draw_metadata_buffer_.Allocate()` in View Phase (not Frame Start)
- Coordinate with `ScenePrepState::ResetViewData()` to clear CPU draw list between views
- Ensure `InlineTransfersCoordinator` tracks per-view writes correctly

---

## Phase 4: Renderer View Iteration Loop

### 4.1 Implement Renderer::RenderView()

**File**: `src/Oxygen/Renderer/Renderer.h`, `Renderer.cpp`

**Signature**:

```cpp
auto RenderView(ViewId view_id, const ResolvedView& resolved_view,
                RenderGraphCoroutine&& graph) -> void;
```

**Logic**:

1. Update `RenderContext::current_view`:
   - `current_view.view_id = view_id`
   - `current_view.resolved_view.reset(&resolved_view)` (observer_ptr)
2. Reset per-view scene prep data via `ScenePrepState::ResetViewData()`
3. Run Scene Prep in View Phase: `scene_prep_->Collect(scene, resolved_view, ...)`
4. Finalize per-view uploads: `scene_prep_->Finalize()`
5. Build PreparedScene and wire to context: `current_view.prepared_scene.reset(&prepared_scene_)`
6. Bind draw metadata SRV: `render_context.draw_metadata_srv = ...`
7. Execute render graph: `ExecuteRenderGraph(render_context)`
8. Capture output framebuffer to `render_context.view_outputs[view_id]`

### 4.2 Implement Multi-View Frame Loop

**File**: `src/Oxygen/Renderer/Renderer.cpp`

**Pseudo-code**:

```cpp
auto Renderer::RenderFrame(FrameContext& frame_ctx,
                           RenderGraphCoroutine&& graph,
                           ViewResolverCallback&& resolver) -> void {
    // Frame Phase: Upload shared resources once
    scene_prep_->Collect(scene, std::nullopt, ...); // Mode A
    scene_prep_->Finalize(); // Upload Transforms/Materials

    // View Loop: Iterate registered views
    for (auto view_id : frame_ctx.GetRegisteredViews()) {
        auto resolved_view = resolver(view_id); // Application resolves camera
        RenderView(view_id, resolved_view, std::move(graph));
    }
}
```

---

## Phase 5: Application Integration Points

### 5.1 Define ViewResolver Callback Type

**File**: `src/Oxygen/Renderer/Types.h` (or appropriate location)

```cpp
using ViewResolver = std::function<ResolvedView(ViewId)>;
```

**Purpose**: Application-provided callback to resolve ViewId to ResolvedView with current camera transforms and jitter.

### 5.2 Update Renderer Public API

**File**: `src/Oxygen/Renderer/Renderer.h`

Add multi-view entry point:

```cpp
auto RenderFrame(FrameContext& frame_ctx,
                 RenderGraphCoroutine&& graph,
                 ViewResolver&& resolver) -> void;
```

### 5.3 Optional: Define ViewOutput Type Alias

**File**: `src/Oxygen/Renderer/Types.h`

```cpp
using ViewOutput = std::shared_ptr<graphics::Framebuffer>;
```

**Note**: Currently using `shared_ptr<Framebuffer>` directly; formalize as `ViewOutput` for clarity.

---

## Phase 6: Synchronization & Barriers

### 6.1 Implement Barrier Coordination

**File**: `src/Oxygen/Renderer/Renderer.cpp`

**Approach**: Aggregate upload tickets and coordinate transitions

```cpp
// Frame Phase uploads
auto shared_ticket = scene_prep_->Finalize(); // Transforms/Materials

// Per-view uploads
for (auto view_id : views) {
    auto view_ticket = PerViewFinalize(); // DrawMetadata
    // Graph execution waits for both shared_ticket AND view_ticket
}
```

**Note**: Leverage existing `InlineTransfersCoordinator` retirement mechanism; may require minimal changes.

---

## Phase 7: Testing & Validation

### 7.1 Unit Tests

- `RenderContext` multi-view state management
- `ScenePrepPipeline` dual-mode operation (nullopt vs. View)
- `ScenePrepState::ResetViewData()` correctness
- `DrawMetadataEmitter` per-view allocation

### 7.2 Integration Tests

- Single view rendering (regression test)
- Multi-view rendering (2-3 views with different cameras)
- View-independent pass execution (should run once, not per-view)
- DrawMetadata uniqueness per view (verify distinct draw lists)

### 7.3 Example Application

Update or create example demonstrating:

- Multiple editor viewports (e.g., perspective + top + side views)
- Per-view culling (different objects visible in each view)
- View-to-surface presentation

---

## Summary of Missing Components

| Component | Priority | Complexity | Depends On |
|-----------|----------|------------|------------|
| Rename View → ResolvedView | P0 | Medium | None |
| Rename PreparedSceneFrame → PreparedScene | P0 | Low | None |
| RenderContext multi-view state | P0 | Low | Phase 0 |
| ScenePrepPipeline optional View | P0 | Medium | Phase 1 |
| ScenePrepState::ResetViewData() | P1 | Low | Phase 2.1 |
| Dual-mode collection logic | P0 | High | Phase 2.1-2.2 |
| DrawMetadataEmitter per-view | P1 | Medium | Phase 2 |
| Renderer::RenderView() | P0 | Medium | Phase 1-3 |
| Multi-view frame loop | P0 | Medium | Phase 4.1 |
| ViewResolver callback | P0 | Low | None |
| Barrier coordination | P2 | Medium | Phase 4 |

**Estimated Effort**: 3-5 days for experienced developer familiar with codebase

**Critical Path**: Phase 0 → Phase 1 → Phase 2 → Phase 4 (Phases 3, 5, 6 can be done in parallel once Phase 2 complete)

**Note**: Phase 2 leverages existing `collected_items_` infrastructure - no new data structures needed.

---

**Document Status**: Draft
**Part of**: Multi-View Rendering Design Series
