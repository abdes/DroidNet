# Multi-View Implementation: Per-View Culling

This document details the implementation strategy for per-view culling and draw generation in a multi-view rendering context.

## 1. Philosophy & Impact

### 1.1. View-Dependent Execution
While resource uploads are frame-centric, **Culling** and **Draw Generation** are inherently **View-Centric**.
-   **PRINCIPLE-03**: Renderer executes render graph once per view.
-   **PRINCIPLE-06**: Per-view culling produces different draw lists; underlying uploaded resources remain shared.

### 1.2. Per-View Transient Data
Each view generates a unique set of `DrawMetadata` (Draw Commands).
-   **Impact**: We need efficient storage for these high-frequency, per-view command lists.
-   **Synchronization**: The "Barrier Problem" implies that View 1 and View 2 might execute in parallel or sequence. Their data must be distinct.

## 2. Implementation Strategy: "Single Pipeline, Dual Mode"

We reuse the existing `ScenePrepPipeline` but adapt it to be **Context-Aware** (Frame vs. View) to avoid duplicating pipeline logic.

### 2.1. Pipeline Modes
The `ScenePrepPipeline::Collect` and `Finalize` methods will accept an optional `View*`.

#### Mode A: Frame Phase (`View == nullptr`)
-   **Goal**: Upload all shared resources (Transforms, Materials).
-   **Logic**:
    -   Iterate **all** active renderables.
    -   **Skip** Visibility/Culling (we want everything).
    -   Run `TransformResolve` & `MaterialBinder`.
    -   **Finalize**: Upload Transforms/Materials to GPU (using `TransientStructuredBuffer`).
-   **Optimization**: Populate a `GlobalRenderableList` in `ScenePrepState` to avoid re-traversing the scene graph in the View Phase.

#### Mode B: View Phase (`View != nullptr`)
-   **Goal**: Generate draw commands for a specific camera.
-   **Logic**:
    -   Iterate `GlobalRenderableList` (fast iteration).
    -   Run **VisibilityFilter** (Frustum Culling against `View`).
    -   Run `DrawMetadataEmitter` for visible items.
    -   **Finalize**: Sort and Upload `DrawMetadata` to a per-view `TransientStructuredBuffer`.

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
-   **Why**: Draw commands are regenerated every frame/view. `AtlasBuffer` is for persistent data.
-   **Mechanism**:
    -   **OnFrameStart**: Reset internal state.
    -   **OnFinalize (View Phase)**:
        -   `transient_buffer_->Allocate(draw_count)`.
        -   `memcpy(transient_buffer_->GetMappedPtr(), sorted_draws.data(), size)`.
        -   Store SRV (`transient_buffer_->GetBinding().srv`) for the renderer.

### 3.2. `ScenePrepState`
-   **New Member**: `GlobalRenderableList` (vector of pointers/indices) to cache the results of the Frame Phase traversal.
-   **New Member**: `TransientStructuredBuffer` for DrawMetadata.
-   **Reset Logic**: `ResetViewData()` must clear the `DrawMetadata` list but keep the `GlobalRenderableList`.

## 4. Synchronization

### 4.1. The Barrier Problem
If we upload transforms at Frame Start, we need a resource transition (CopyDest -> ShaderResource) before *any* view reads them.
-   **Solution**: The Renderer aggregates tickets.
    -   `shared_ticket = Upload(Transforms)`
    -   `view_ticket = Upload(DrawMetadata)`
-   **Graph Execution**: Waits for `shared_ticket` AND `view_ticket`.

This ensures that by the time a View executes, both the shared scene data and its specific draw commands are resident and ready.

---

**Document Status**: Draft
**Last Updated**: 2025-12-02
**Part of**: Multi-View Rendering Design Series
