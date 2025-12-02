# Multi-View Implementation: Per-View Culling

This document details the implementation strategy for per-view culling and draw generation in a multi-view rendering context.

## 1. Philosophy & Impact

### 1.1. View-Dependent Execution

While resource uploads are frame-centric, **Culling** and **Draw Generation** are inherently **View-Centric**.

- **PRINCIPLE-03**: Renderer executes render graph once per view.
- **PRINCIPLE-06**: Per-view culling produces different draw lists; underlying uploaded resources remain shared.

### 1.2. Per-View Transient Data

Each view generates a unique set of `DrawMetadata` (Draw Commands).

- **Impact**: We need efficient storage for these high-frequency, per-view command lists.
- **Synchronization**: The "Barrier Problem" implies that View 1 and View 2 might execute in parallel or sequence. Their data must be distinct.

## 2. Implementation Strategy: "Single Pipeline, Dual Mode"

We reuse the existing `ScenePrepPipeline` but adapt it to be **Context-Aware** (Frame vs. View) to avoid duplicating pipeline logic.

### 2.1. Pipeline Modes

The `ScenePrepPipeline::Collect` and `Finalize` methods will accept an optional `View*`.

#### Mode A: Frame Phase (`View == nullptr`)

- **Goal**: Upload all shared resources (Transforms, Materials).
- **Logic**:
  - Iterate **all** active renderables.
  - **Skip** Visibility/Culling (we want everything).
  - Run `TransformResolve` & `MaterialBinder`.
  - **Finalize**: Upload Transforms/Materials to GPU (using `TransientStructuredBuffer`).
- **Optimization**: Populate a `GlobalRenderableList` in `ScenePrepState` to avoid re-traversing the scene graph in the View Phase.

#### Mode B: View Phase (`View != nullptr`)

- **Goal**: Generate draw commands for a specific camera.
- **Logic**:
  - Iterate `GlobalRenderableList` (fast iteration).
  - Run **VisibilityFilter** (Frustum Culling against `View`).
  - Run `DrawMetadataEmitter` for visible items.
  - **Finalize**: Sort and Upload `DrawMetadata` to a per-view `TransientStructuredBuffer`.

### 2.2. Concrete Culling Loop

This logic belongs in `Renderer::RenderView` (or equivalent).

```cpp
// 1. Frame Start (Once)
scene_prep_->Collect(scene, nullptr, ...); // View = null
scene_prep_->Finalize(); // Uploads Transforms/Materials

// 2. View Loop
for (const auto& view : views) {
    // Reset per-view state (clears previous draw metadata)
    scene_prep_state_->ResetViewData();

    // Collect & Cull for this view
    scene_prep_->Collect(scene, &view, ...);
    scene_prep_->Finalize(); // Uploads DrawMetadata for this view

    // Bind the new DrawMetadata SRV
    render_context.draw_metadata_srv = scene_prep_state_->GetDrawMetadataSRV();

    // Execute Graph
    ExecuteRenderGraph(render_context);
}
```

## 3. Component Updates

### 3.1. `DrawMetadataEmitter`

The `DrawMetadataEmitter` must be updated to use `TransientStructuredBuffer` instead of `AtlasBuffer`.

- **Why**: Draw commands are regenerated every frame/view. `AtlasBuffer` is for persistent data.
- **Mechanism**:
  - **OnFrameStart**: Reset internal state.
  - **OnFinalize (View Phase)**:
    - `transient_buffer_->Allocate(draw_count)`.
    - `memcpy(transient_buffer_->GetMappedPtr(), sorted_draws.data(), size)`.
    - Store SRV (`transient_buffer_->GetBinding().srv`) for the renderer.

### 3.2. `ScenePrepState`

- **New Member**: `GlobalRenderableList` (vector of pointers/indices) to cache the results of the Frame Phase traversal.
- **New Member**: `TransientStructuredBuffer` for DrawMetadata.
- **Reset Logic**: `ResetViewData()` must clear the `DrawMetadata` list but keep the `GlobalRenderableList`.

## 4. Synchronization

### 4.1. The Barrier Problem

If we upload transforms at Frame Start, we need a resource transition (CopyDest -> ShaderResource) before *any* view reads them.

- **Solution**: The Renderer aggregates tickets.
  - `shared_ticket = Upload(Transforms)`
  - `view_ticket = Upload(DrawMetadata)`
- **Graph Execution**: Waits for `shared_ticket` AND `view_ticket`.

This ensures that by the time a View executes, both the shared scene data and its specific draw commands are resident and ready.

---

## Implementation status

| Component/Feature | Status | Location | Notes |
|-------------------|--------|----------|-------|
| **ScenePrepPipeline** | ⚠️ Partial | `src/Oxygen/Renderer/ScenePrep/ScenePrepPipeline.h` | Exists but `Collect()` requires `const View&` (not optional) - needs Mode A/B support |
| **ScenePrepPipeline::Collect with optional View** | ❌ Missing | N/A | Design requires `Collect(scene, View* = nullptr)` for dual-mode operation |
| **ScenePrepState** | ✅ Implemented | `src/Oxygen/Renderer/ScenePrep/ScenePrepState.h` | Core state management exists |
| **GlobalRenderableList** | ❌ Missing | N/A | Caching mechanism for Frame Phase traversal not present |
| **ScenePrepState::ResetViewData()** | ❌ Missing | N/A | Method to clear per-view data while keeping global list |
| **DrawMetadataEmitter** | ✅ Implemented | `src/Oxygen/Renderer/Resources/DrawMetadataEmitter.h` | Exists but may need TransientStructuredBuffer migration |
| **DrawMetadataEmitter using TransientStructuredBuffer** | ⚠️ Partial | N/A | Currently uses AtlasBuffer pattern; needs per-view transient allocation |
| **TransientStructuredBuffer** | ✅ Implemented | `src/Oxygen/Renderer/Upload/TransientStructuredBuffer.h` | Core component exists and ready |
| **VisibilityFilter (SubMeshVisibilityFilter)** | ✅ Implemented | `src/Oxygen/Renderer/ScenePrep/Extractors.h` | Frustum culling against View working |
| **Renderer::RenderView()** | ❌ Missing | N/A | Per-view rendering loop with culling not implemented |
| **Dual-Mode Pipeline (Frame vs View Phase)** | ❌ Missing | N/A | Context-aware pipeline switching not present |
| **Per-View Draw Metadata Upload** | ❌ Missing | N/A | View-specific draw command generation not wired up |
| **Barrier Problem Synchronization** | ❌ Missing | N/A | Shared ticket + view ticket coordination not implemented |

**Summary**: 3/13 components fully implemented, 2/13 partially implemented. Core building blocks exist (TransientStructuredBuffer, SubMeshVisibilityFilter, DrawMetadataEmitter, ScenePrepState) but the dual-mode pipeline architecture and per-view rendering loop are missing. The design requires significant refactoring to support optional View parameter and separate Frame/View phases.

**Critical Path**:

1. Refactor `ScenePrepPipeline::Collect()` to accept `optional<View>` or `View*`
2. Implement `GlobalRenderableList` caching in Frame Phase
3. Add `ScenePrepState::ResetViewData()` for per-view state management
4. Implement `Renderer::RenderView()` with per-view culling loop
5. Migrate DrawMetadataEmitter to per-view TransientStructuredBuffer allocation

---

**Document Status**: Draft
**Last Updated**: 2025-12-02
**Part of**: Multi-View Rendering Design Series
