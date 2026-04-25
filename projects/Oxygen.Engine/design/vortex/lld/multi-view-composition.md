# Multi-View Composition LLD

**Phase:** 5D — Remaining Services
**Deliverable:** D.17
**Status:** `ready`

## Mandatory Vortex Rule

- For Vortex planning and implementation, `Oxygen.Renderer` is legacy dead
  code. It is not production, not a reference implementation, not a fallback,
  and not a simplification path for any Vortex task.
- Every Vortex task must be designed and implemented as a new Vortex-native
  system that targets maximum parity with UE5.7, grounded in
  `F:\Epic Games\UE_5.7\Engine\Source\Runtime` and
  `F:\Epic Games\UE_5.7\Engine\Shaders`.
- No Vortex task may be marked complete until its parity gate is closed with
  explicit evidence against the relevant UE5.7 source and shader references.
- If maximum parity cannot yet be achieved, the task remains incomplete until
  explicit human approval records the accepted gap and the reason the parity
  gate cannot close.

## 1. Scope and Context

### 1.1 What This Covers

Multi-view rendering, per-view `ShadingMode` selection, multi-surface
composition, and PiP (picture-in-picture) / secondary composition. This
deliverable validates that `SceneRenderer` correctly dispatches
heterogeneous views within a single frame and that the composition path
routes each view's output to the correct surface.

### 1.2 Architectural Authority

- [ARCHITECTURE.md §5](../ARCHITECTURE.md) — frame dispatch pipeline
- [ARCHITECTURE.md §6.2](../ARCHITECTURE.md) — per-view vs per-frame stages
- `CompositionView` intent API: ViewId, ZOrder, camera, ShadingMode

## 2. Multi-View Dispatch Model

### 2.1 Per-View vs Per-Frame Stages

| Stage | Iteration | Notes |
| ----- | --------- | ----- |
| 2 (InitViews) | Per frame with per-view prepared outputs | One frame-shared collection pass, then one prepared-scene payload per view |
| 3 (DepthPrepass) | Per view | Separate depth per view |
| 5 (Occlusion) | Per view | Separate HZB per view |
| 6 (LightGrid) | Per frame setup + per-view publication | Shared light-data family, published per view |
| 8 (ShadowDepths) | Per frame setup + per-view publication | Shared allocation work is allowed; published shadow payload remains per view |
| 9 (BasePass) | Per view | Separate GBuffers per view |
| 12 (DeferredLighting) | Per view | Per-view SceneColor accumulation |
| 14 (Environment volumetrics/local fog) | Per view | Per-view local-fog culling/composition inputs and volumetric-fog work when enabled |
| 15 (Environment sky/fog) | Per view | Per-view sky, atmosphere, height-fog, and local-fog composition |
| 18 (Translucency) | Per view | Per-view translucent compositing |
| 22 (PostProcess) | Per view | Per-view tonemap |

### 2.2 View Iteration Pattern

```cpp
void SceneRenderer::OnRender(RenderContext& ctx) {
  // Stage 2 remains a per-frame stage that publishes one prepared-scene payload per view
  if (init_views_) init_views_->Execute(ctx, scene_textures_);          // 2

  // Per-frame stages (shared)
  if (lighting_) lighting_->BuildLightGrid(ctx);        // stage 6
  if (shadows_) shadows_->RenderShadowDepths(ctx);      // stage 8

  // Per-view stages
  for (std::size_t view_index = 0; view_index < ctx.frame_views.size(); ++view_index) {
    ctx.active_view_index = view_index;
    const auto& view_entry = ctx.frame_views[view_index];
    ctx.current_view.view_id = view_entry.view_id;
    ctx.current_view.composition_view = view_entry.composition_view;
    ctx.current_view.shading_mode_override = view_entry.shading_mode_override;
    ctx.current_view.resolved_view = view_entry.resolved_view;
    ctx.pass_target = view_entry.primary_target;
    ctx.current_view.prepared_frame = init_views_
      ? observer_ptr<const PreparedSceneFrame> {
          init_views_->GetPreparedSceneFrame(view_entry.view_id) }
      : observer_ptr<const PreparedSceneFrame> {};

    if (depth_prepass_) depth_prepass_->Execute(ctx, scene_textures_); // 3
    if (occlusion_) occlusion_->Execute(ctx, scene_textures_);       // 5
    if (base_pass_) base_pass_->Execute(ctx, scene_textures_);       // 9

    PublishDeferredBasePassSceneTextures(ctx);                         // 10

    if (lighting_) lighting_->RenderDeferredLighting(ctx, scene_textures_); // 12
    // Current EnvironmentLightingService entrypoint records active Stage 14
    // local-fog culling before Stage 15 sky/fog composition. Split Stage 14
    // into a dedicated service method when runtime volumetric fog passes are
    // implemented and enabled.
    if (environment_) environment_->RenderSkyAndFog(ctx, scene_textures_);  // 14/15
    if (translucency_) translucency_->Execute(ctx, scene_textures_);        // 18

    ResolveSceneColor(ctx);                                             // 21
    if (post_process_) post_process_->Execute(ctx, scene_textures_);   // 22
  }

  PostRenderCleanup(ctx);                                               // 23
}
```

### 2.3 Per-View SceneTextures

Each view requires its own set of SceneTextures (GBuffers, SceneColor,
depth). For multi-view scenarios:

**Option A (Phase 5D baseline):** Reallocate SceneTextures per view within
the same frame. Simple but wastes bandwidth.

**Option B (optimization):** Pool SceneTextures per view. Each view gets
its own pre-allocated set. More memory, better GPU utilization.

Phase 5D starts with Option A. Optimization to Option B happens if
profiling shows allocation is a bottleneck.

## 3. Per-View ShadingMode Selection

### 3.1 ShadingMode Per CompositionView

```cpp
struct CompositionViewDescriptor {
  ViewId view_id;
  int32_t z_order;
  Camera camera;
  ShadingMode shading_mode{ShadingMode::kDeferred};  // Per-view mode
  // ...
};
```

### 3.2 Branching in BasePass

When `shading_mode == kDeferred`:

- BasePass writes GBuffers + SceneColor emissive (standard deferred path)
- Deferred lighting runs at stage 12

When `shading_mode == kForward`:

- BasePass writes directly to SceneColor (forward lighting, no GBuffers)
- Stage 12 deferred lighting is skipped for this view
- Forward lighting uses the published forward-light package from LightingService

### 3.3 Mixed-Mode Frame

Within a single frame, one view can be deferred and another forward:

```text
View 0 (kDeferred): InitViews → Depth → BasePass(GBuffer) → DeferredLight → PostProcess
View 1 (kForward):  InitViews → Depth → BasePass(Forward)  → [skip deferred] → PostProcess
```

## 4. Composition and Surface Routing

### 4.1 CompositionPlanner

The `CompositionPlanner` (Internal/) resolves views by ZOrder and routes
each view's post-processed output to the correct back buffer or offscreen
surface.

### 4.2 Multi-Surface Output

| Surface | Source | Notes |
| ------- | ------ | ----- |
| Primary back buffer | Highest Z-order view | Main display output |
| PiP overlay | Secondary view | Composited over primary |
| Offscreen target | Offscreen view | Render-to-texture |

### 4.3 PiP Composition

For picture-in-picture:

1. Render primary view (full resolution, deferred or forward)
2. Render secondary view (reduced resolution, typically forward for
   performance)
3. Composite secondary view's output as a sub-rect over the primary view's
   final output

## 5. Interface Contracts

No new classes for D.17. Multi-view dispatch is a validation of existing
architecture:

- `SceneRenderer::OnRender()` — view iteration loop
- `CompositionView` — per-view descriptor with ShadingMode
- `CompositionPlanner` — surface routing
- `SceneTextures` — per-view allocation

### 5.1 Validation Test Entry Points

```cpp
// Test fixture creates SceneRenderer with two views:
void TestMultiViewDeferred() {
  auto view0 = CreateCompositionView(ShadingMode::kDeferred, z_order=0);
  auto view1 = CreateCompositionView(ShadingMode::kDeferred, z_order=1);
  renderer.PublishViews({view0, view1});
  renderer.OnRender(ctx);
  // Verify both views produced correct GBuffer + lighting output
}

void TestMixedModeShadingPerView() {
  auto view0 = CreateCompositionView(ShadingMode::kDeferred, z_order=0);
  auto view1 = CreateCompositionView(ShadingMode::kForward, z_order=1);
  renderer.PublishViews({view0, view1});
  renderer.OnRender(ctx);
  // Verify view0 has GBuffer, view1 has no GBuffer but correct SceneColor
}
```

## 6. Stage Integration

### 6.1 View Lifecycle

1. `CompositionView` descriptors published before frame start
2. `OnPreRender` resolves view list and allocates per-view SceneTextures
3. `OnRender` iterates views for per-view stages
4. `OnCompositing` composites view outputs to surfaces
5. `OnFrameEnd` extracts per-view SceneTextureExtracts

### 6.2 Capability-Gated Per-View Service Presence

Services can be present for some views and absent for others based on
per-view capability overrides:

```cpp
// Example: view 1 has no shadows
view1_descriptor.capability_overrides = ~kShadowing;
```

When a service is disabled for a view, that view receives no publication from
that service and the consuming stages behave as null-safe no-ops for that view
iteration.

## 7. Testability Approach

1. **Two-view deferred:** Two cameras pointing at different parts of scene →
   verify both views produce correct independent GBuffer + lighting.
2. **Mixed mode:** One deferred view + one forward view → verify deferred
   view has GBuffer, forward view has direct SceneColor.
3. **PiP composition:** Primary + secondary view → verify secondary appears
   as sub-rect overlay.
4. **Capability override:** Disable shadows for secondary view → verify
   the secondary view receives empty shadow bindings while the primary view
   still receives valid shadow data.
5. **RenderDoc:** Frame 10, verify correct stage ordering per view in event
   list.

## 8. Open Questions

1. **Per-view SceneTexture pooling:** Phase 5D uses per-view reallocation.
   If profiling shows this is a bottleneck, a texture pool per view ID
   should be added. Not blocking for Phase 5D validation.
