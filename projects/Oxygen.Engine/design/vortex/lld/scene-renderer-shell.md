# SceneRenderer Shell and SceneRenderBuilder LLD

**Phase:** 2 — SceneTextures and SceneRenderer Shell
**Deliverables:** D.2 (SceneRenderBuilder), D.3 (SceneRenderer shell dispatch)
**Status:** `ready`

## 1. Scope and Context

### 1.1 What This System Is

The SceneRenderer is the thin desktop frame dispatcher that owns stage ordering,
`SceneTextures`, and shading-mode selection. `SceneRenderBuilder` is the
bootstrap helper that constructs a SceneRenderer from a `CapabilitySet`.

This document started as the Phase 2 shell contract. On the current retained
Phase 03 branch, the shell has progressed into a real deferred-core runtime:
Renderer Core materializes the eligible frame views and selects the current
scene-view cursor in `RenderContext`, while `SceneRenderer` owns the stage
chain for that selected current view (currently Stage 2/3/9/10/12 plus
resolve/cleanup). The later reserved stage/service slots remain future-facing.

### 1.2 What It Replaces

| Legacy | Vortex |
| ------ | ------ |
| `ForwardPipeline` + `RenderingPipeline` | `SceneRenderer` |
| Pipeline construction in Renderer | `SceneRenderBuilder` |
| `PipelineFeature`, `PipelineSettings` | Capability-gated service presence |

### 1.3 Architectural Authority

- [ARCHITECTURE.md §5.1.1](../ARCHITECTURE.md) — SceneRenderer positioning
- [ARCHITECTURE.md §5.1.3](../ARCHITECTURE.md) — detailed flow mapping
- [ARCHITECTURE.md §6.2](../ARCHITECTURE.md) — 23-stage runtime contract
- [ARCHITECTURE.md §6.3](../ARCHITECTURE.md) — runtime rules

## 2. Interface Contracts

### 2.1 ShadingMode

```cpp
namespace oxygen::vortex {

enum class ShadingMode : std::uint8_t {
  kDeferred,  // GBuffer base pass + deferred lighting (default)
  kForward,   // Forward shading via shared light data
};

} // namespace oxygen::vortex
```

**File:** `SceneRenderer/ShadingMode.h`

### 2.2 SceneRenderBuilder

Bootstrap helper invoked by Renderer Core during initialization.

```cpp
namespace oxygen::vortex {

class SceneRenderBuilder {
public:
  /// Constructs a SceneRenderer configured for the given capability set.
  /// Phase 2 always constructs the desktop SceneRenderer shell for the Vortex
  /// runtime path. Capabilities control mode defaults and future service/module
  /// presence; they do not remove the SceneRenderer layer itself.
  OXGN_VRTX_NDAPI static auto Build(
    Renderer& renderer,
    graphics::IGraphics& gfx,
    const CapabilitySet& capabilities,
    glm::uvec2 initial_viewport_extent)
    -> std::unique_ptr<SceneRenderer>;
};

} // namespace oxygen::vortex
```

**File:** `SceneRenderer/SceneRenderBuilder.h`

**Construction logic:**

1. Read `CapabilitySet` to determine default shading policy and future
   subsystem-service presence.
2. Create `SceneTexturesConfig` from viewport extent and capability flags.
3. Construct `SceneTextures`.
4. Initialize empty `SceneTextureBindings` routing state owned by the
   `SceneRenderer`.
5. Construct stage modules based on capabilities (all null in Phase 2 shell).
6. Construct subsystem services based on capabilities (all null in Phase 2).
7. Return assembled `SceneRenderer` instance.

**Capability interpretation (Phase 2 shell):**

| Capability | Effect |
| ---------- | ------ |
| `kDeferredShading` | Sets default `ShadingMode::kDeferred` |
| `kScenePreparation` | Enables InitViews (future) |
| `kLightingData` | Enables LightingService (future) |
| `kShadowing` | Enables ShadowService (future) |
| `kEnvironmentLighting` | Enables EnvironmentLightingService (future) |
| `kDiagnosticsAndProfiling` | Enables DiagnosticsService (future) |

In Phase 2, all capability checks populate null unique_ptrs. The shell
validates that capabilities are recognized but does not construct services.
`Build(...)` still returns a `SceneRenderer` for the desktop runtime path even
when the capability set is empty; an empty set yields the shell with no domain
systems active and the default shading mode derived from the capability policy.

### 2.3 SceneRenderer

The desktop frame orchestrator.

```cpp
namespace oxygen::vortex {

class SceneRenderer {
public:
  OXGN_VRTX_API explicit SceneRenderer(
    Renderer& renderer,
    graphics::IGraphics& gfx,
    SceneTexturesConfig config,
    ShadingMode default_shading_mode);

  OXGN_VRTX_API ~SceneRenderer();

  // Non-copyable, non-movable
  SceneRenderer(const SceneRenderer&) = delete;
  auto operator=(const SceneRenderer&) -> SceneRenderer& = delete;

  // --- Frame-phase hooks (called by Renderer) ---

  OXGN_VRTX_API void OnFrameStart(const FrameContext& frame);
  OXGN_VRTX_API void OnPreRender(const FrameContext& frame);
  /// Executes the scene-view stage chain for the current view selected by
  /// Renderer Core in `RenderContext`.
  OXGN_VRTX_API void OnRender(RenderContext& ctx);
  OXGN_VRTX_API void OnCompositing(RenderContext& ctx);
  OXGN_VRTX_API void OnFrameEnd(const FrameContext& frame);

  // --- Query / publication ---

  OXGN_VRTX_NDAPI auto GetSceneTextures() const
    -> const SceneTextures&;
  OXGN_VRTX_NDAPI auto GetSceneTextures()
    -> SceneTextures&;
  OXGN_VRTX_NDAPI auto GetDefaultShadingMode() const
    -> ShadingMode;
  OXGN_VRTX_API void PrimePreparedView(RenderContext& ctx);

private:
  Renderer& renderer_;
  graphics::IGraphics& gfx_;
  SceneTextures scene_textures_;
  SceneTextureSetupMode setup_mode_;
  SceneTextureBindings scene_texture_bindings_;
  ShadingMode default_shading_mode_;

  // --- Stage modules (Phase 3+) ---
  // std::unique_ptr<InitViewsModule> init_views_;
  // std::unique_ptr<DepthPrepassModule> depth_prepass_;
  // std::unique_ptr<OcclusionModule> occlusion_;
  // std::unique_ptr<BasePassModule> base_pass_;
  // std::unique_ptr<TranslucencyModule> translucency_;

  // --- Subsystem services (Phase 4+) ---
  // std::unique_ptr<LightingService> lighting_;
  // std::unique_ptr<ShadowService> shadows_;
  // std::unique_ptr<EnvironmentLightingService> environment_;
  // std::unique_ptr<PostProcessService> post_process_;
  // std::unique_ptr<DiagnosticsService> diagnostics_;

  // --- File-separated methods ---
  void RefreshSceneTextureBindings(RenderContext& ctx);
  [[nodiscard]] auto ResolveShadingModeForCurrentView(
    const RenderContext& ctx) const -> ShadingMode;
  void RenderDeferredLighting(
    RenderContext& ctx,
    const SceneTextures& scene_textures);
  void ResolveSceneColor(RenderContext& ctx);
  void PostRenderCleanup(RenderContext& ctx);
};

} // namespace oxygen::vortex
```

**Files:**

- `SceneRenderer/SceneRenderer.h` — declaration
- `SceneRenderer/SceneRenderer.cpp` — frame hooks + dispatch skeleton
- `SceneRenderer/ResolveSceneColor.cpp` — file-separated method
- `SceneRenderer/PostRenderCleanup.cpp` — file-separated method

### 2.4 Stage-21 / Stage-22 Handoff Rule

`SceneRenderer` owns the authoritative Stage-21 -> Stage-22 handoff. That
handoff is not just a texture pointer. It is the coherent bundle of:

- selected scene-signal texture
- shader-visible SRV for that exact texture
- selected scene-depth texture
- shader-visible SRV for that exact texture
- the composition-facing post target for the active view

Stage-22 consumers (`PostProcessService` and its passes) must consume that
bundle; they must not reinterpret or replace it.

## 3. Frame Dispatch Design

### 3.1 OnFrameStart

Called once per frame before any rendering.

```cpp
void SceneRenderer::OnFrameStart(const FrameContext& frame) {
  // 1. Check viewport resize
  auto viewport = /* get viewport from frame context */;
  if (viewport != scene_textures_.GetExtent()) {
    scene_textures_.Resize(viewport);
  }

  // 2. Reset setup state for new frame
  setup_mode_.Reset();
  scene_texture_bindings_ = {};

  // 3. Notify active services (future)
  // if (lighting_) lighting_->OnFrameStart(frame);
  // if (shadows_) shadows_->OnFrameStart(frame);
  // if (environment_) environment_->OnFrameStart(frame);
  // if (post_process_) post_process_->OnFrameStart(frame);
  // if (diagnostics_) diagnostics_->OnFrameStart(frame);
}
```

### 3.2 OnRender — 23-Stage Dispatch Skeleton

This is the core of the SceneRenderer. In Phase 2, all stages are no-ops.

```cpp
void SceneRenderer::OnRender(RenderContext& ctx) {
  // === Stage 2: InitViews ===
  // Visibility, culling, command generation
  // if (init_views_) init_views_->Execute(ctx, scene_textures_);

  // === Stage 3: Depth prepass + early velocity ===
  // if (depth_prepass_) depth_prepass_->Execute(ctx, scene_textures_);

  // === Stage 4: reserved — GeometryVirtualizationService ===

  // === Stage 5: Occlusion / HZB ===
  // if (occlusion_) occlusion_->Execute(ctx, scene_textures_);

  // === Stage 6: Forward light data / light grid ===
  // if (lighting_) lighting_->BuildLightGrid(ctx);

  // === Stage 7: reserved — MaterialCompositionService::PreBasePass ===

  // === Stage 8: Shadow depth ===
  // if (shadows_) shadows_->RenderShadowDepths(ctx);

  // === Stage 9: Base pass — GBuffer MRT + velocity completion ===
  // if (base_pass_) base_pass_->Execute(ctx, scene_textures_);

  // === Stage 10: SceneRenderer-owned rebuild/publish boundary ===
  // PublishDeferredBasePassSceneTextures(ctx);

  // === Stage 11: reserved — MaterialCompositionService::PostBasePass ===

  // === Stage 12: Deferred direct lighting ===
  // RenderDeferredLighting(ctx, scene_textures_);

  // === Stage 13: reserved — IndirectLightingService ===

  // === Stage 14: reserved — EnvironmentLightingService volumetrics ===

  // === Stage 15: Sky / atmosphere / fog ===
  // if (environment_) environment_->RenderSkyAndFog(ctx, scene_textures_);

  // === Stage 16: reserved — WaterService ===

  // === Stage 17: reserved — post-opaque extensions ===

  // === Stage 18: Translucency ===
  // if (translucency_) translucency_->Execute(ctx, scene_textures_);

  // === Stage 19: reserved — DistortionModule ===

  // === Stage 20: reserved — LightShaftBloomModule ===

  // === Stage 21: Resolve scene color ===
  ResolveSceneColor(ctx);

  // === Stage 22: Post processing ===
  // if (post_process_) post_process_->Execute(ctx, scene_textures_);

  // === Stage 23: Post-render cleanup / extraction ===
  PostRenderCleanup(ctx);
}
```

### 3.3 Per-View vs Per-Frame Iteration

Per ARCHITECTURE.md §6.2, some stages execute per-frame and others against the
current scene-view cursor. Renderer Core materializes `ctx.frame_views` and
selects the current view in `ctx.current_view`; `SceneRenderer` consumes that
selected current view for the scene-view render stages:

```text
Per-frame stages:     Per-view stages:
  Stage 6 (light grid)  Stage 2 (init views — per-view outputs)
  Stage 8 (shadows)     Stage 3 (depth prepass)
                         Stage 5 (occlusion)
                         Stage 9 (base pass)
                         Stage 12 (deferred lighting)
                         Stage 18 (translucency)
                         Stage 22 (post-process)
```

**Iteration pattern:**

```cpp
// Renderer Core materializes the eligible views and selects the current scene
// view before SceneRenderer runs.
renderer_.PopulateRenderContextViewState(ctx, frame_context, false);

// Stage 2 publishes per-view prepared-scene payloads but binds only the
// current view's payload into RenderContext.
if (ctx.current_view.prepared_frame == nullptr) {
  PrimePreparedView(ctx);
}

// Later scene-view stages consume the selected current view only.
if (depth_prepass_) depth_prepass_->Execute(ctx, scene_textures_);
if (base_pass_) base_pass_->Execute(ctx, scene_textures_);
PublishDeferredBasePassSceneTextures(ctx);
```

Future heterogeneous multi-view scene execution may still call the scene-view
stage chain multiple times in one frame, but the outer selection / iteration
loop remains a Renderer-Core concern because it owns canonical runtime views,
render-context materialization, publication helpers, and composition planning.

### 3.3.1 Preserving the Per-View Shading-Mode Seam

The architecture and DESIGN contract require `ShadingMode` selection to be
per `CompositionView`, not renderer-global. Phase 2 does not implement mixed-
mode multi-view execution yet, but it must preserve the seam where that view
intent enters the scene renderer.

Phase-2 rule:

- `default_shading_mode_` is a fallback, not the long-term ownership model
- when a current view is available in `RenderContext`, the shell resolves the
  effective mode from the current view's composition intent
- when no current view has been materialized yet, the shell falls back to the
  default mode selected at bootstrap
- full per-view mixed-mode execution remains deferred to the later multi-view
  validation work; the seam itself must already exist in Phase 2

Illustrative helper:

```cpp
auto SceneRenderer::ResolveShadingModeForCurrentView(
  const RenderContext& ctx) const -> ShadingMode {
  if (const auto* view = ctx.GetCurrentCompositionView()) {
    return view->GetShadingMode().value_or(default_shading_mode_);
  }
  return default_shading_mode_;
}
```

### 3.4 Null-Safe Dispatch

Every stage dispatch is guarded by a null check on the owning module or
service pointer:

```cpp
if (lighting_) lighting_->BuildLightGrid(ctx);
```

When a capability is absent, the corresponding service pointer is null, and
the stage is a zero-overhead no-op. This is the architectural guarantee from
ARCHITECTURE.md §6.2.

### 3.5 ResolveSceneColor (Stage 21)

File-separated method (~180 lines). This is already part of the retained
Phase 03 runtime branch, not a future stub. The stage copies live
`SceneColor` and `SceneDepth` into explicit resolved artifacts for downstream
Renderer Core composition/extraction work. It does **not** take ownership of
composition target resolution, composition queueing, or presentation. Phase 4F
does not introduce this stage; it validates the already-live retained-branch
behavior end-to-end as part of the first migration-grade composition path.
When post-processing is active (Phase 4+), this remains the
SceneRenderer-owned resolve / HDR merge point before Renderer Core consumes the
resolved product for composition.

**File:** `SceneRenderer/ResolveSceneColor.cpp`

### 3.6 PostRenderCleanup (Stage 23)

File-separated method (~200-300 lines). This is already part of the retained
Phase 03 runtime branch, not a future stub. It:

1. Creates an explicit `SceneTextureExtracts` set describing extracted handoff
   artifacts, not live in-frame attachments
2. Invokes Renderer Core helper surfaces for one-way handoff completion
3. Resets transient per-frame state

Phase 4F does not introduce Stage 23; it validates the already-live
retained-branch extraction/handoff behavior end-to-end inside the full
composition/presentation path.

**File:** `SceneRenderer/PostRenderCleanup.cpp`

## 4. Wiring SceneRenderer into Renderer

### 4.1 Renderer Additions

The Renderer (Renderer Core) gains:

```cpp
// In Renderer private members:
std::unique_ptr<SceneRenderer> scene_renderer_;

// In Renderer initialization (after substrate setup):
scene_renderer_ = SceneRenderBuilder::Build(
  *this, gfx_, capabilities, initial_extent);

// In Renderer frame methods:
void Renderer::OnPreRender(const FrameContext& frame) {
  // ... substrate work (context alloc, upload staging) ...
  if (scene_renderer_) scene_renderer_->OnPreRender(frame);
}

void Renderer::OnRender(RenderContext& ctx) {
  // ... substrate work ...
  if (scene_renderer_) scene_renderer_->PrimePreparedView(ctx);
  if (scene_renderer_) scene_renderer_->OnRender(ctx);
}

void Renderer::OnCompositing(RenderContext& ctx) {
  if (scene_renderer_) scene_renderer_->OnCompositing(ctx);
  // ... composition planning/execution (Renderer Core owns this) ...
}

void Renderer::OnFrameEnd(const FrameContext& frame) {
  if (scene_renderer_) scene_renderer_->OnFrameEnd(frame);
  // ... substrate cleanup ...
}
```

### 4.2 Ownership Boundary

| Responsibility | Owner |
| -------------- | ----- |
| `SceneRenderer` lifetime | Renderer (via `unique_ptr`) |
| `SceneTextures` lifetime | SceneRenderer |
| Frame-phase delegation | Renderer → SceneRenderer |
| Prepared-frame priming before initial publication | Renderer (via `PrimePreparedView`) |
| Composition planning + execution | Renderer (not SceneRenderer) |
| View registration | Renderer (not SceneRenderer) |
| Upload/staging coordination | Renderer (not SceneRenderer) |

## 5. Data Flow and Dependencies

### 5.1 Construction Flow

```text
Renderer::Initialize()
  └─ SceneRenderBuilder::Build(renderer, gfx, capabilities, extent)
       ├─ Resolve default shading policy from capabilities
       ├─ Create SceneTexturesConfig from capabilities
       ├─ Create SceneTextures(gfx, config)
       ├─ Initialize SceneTextureBindings routing state
       ├─ Create stage modules (null in Phase 2)
       ├─ Create services (null in Phase 2)
       └─ Return SceneRenderer
```

### 5.2 Frame Flow (Phase 2 Shell)

```text
Renderer::OnFrameStart(frame)
  └─ SceneRenderer::OnFrameStart(frame)
       ├─ Check resize
       └─ Reset setup mode

Renderer::OnRender(ctx)
  └─ SceneRenderer::PrimePreparedView(ctx)
  └─ SceneRenderer::OnRender(ctx)
       └─ real Stage 2 / 3 / 9 / 10 / 12 execution on the current Phase 03 branch
       └─ ResolveSceneColor (explicit resolved-artifact copy)
       └─ PostRenderCleanup (extraction/handoff finalization)

Renderer::OnCompositing(ctx)
  └─ SceneRenderer::OnCompositing(ctx)
       └─ Produce composition submission
```

## 6. Resource Management

SceneRenderer does not directly own GPU resources beyond what SceneTextures
manages. All GPU resource allocation is delegated to SceneTextures (see
[scene-textures.md](scene-textures.md)).

## 7. Shader Contracts

Phase 2 introduces no new shaders. The shell dispatch skeleton runs with
no GPU work.

## 8. Stage Integration

The SceneRenderer IS the stage integrator. It defines the 23-stage dispatch
order (ARCHITECTURE.md §6.2). Individual stage modules and subsystem services
integrate into this dispatch by implementing their Execute/domain methods
and being wired into the SceneRenderer's dispatch skeleton.

## 9. Directory Structure

Per PROJECT-LAYOUT.md:

```text
SceneRenderer/
├── SceneRenderer.h
├── SceneRenderer.cpp
├── SceneRenderBuilder.h
├── SceneRenderBuilder.cpp
├── SceneTextures.h
├── SceneTextures.cpp
├── ShadingMode.h
├── DepthPrePassPolicy.h
├── ResolveSceneColor.cpp
├── PostRenderCleanup.cpp
├── Internal/
│   ├── FramePlanBuilder.h/.cpp
│   └── ViewRenderPlan.h
└── Stages/
    ├── InitViews/         (Phase 3)
    ├── DepthPrepass/      (Phase 3)
    ├── Occlusion/         (Phase 5)
    ├── BasePass/          (Phase 3)
    ├── Translucency/      (Phase 5)
    ├── Distortion/        (reserved)
    └── LightShaftBloom/   (reserved)
```

## 10. Testability Approach

### 10.1 Unit Tests

1. **SceneRenderBuilder:** Build with various capability sets. Verify correct
   SceneRenderer configuration. Verify that empty capability sets still produce
   the desktop shell rather than omitting the `SceneRenderer` layer.
2. **SceneRenderer construction:** Verify SceneTextures allocated, setup mode
   at kNone, and empty `SceneTextureBindings` routing state.
3. **Per-view shading-mode seam:** Verify the shell resolves the effective mode
   from the current `CompositionView` when present and falls back to the
   bootstrap default otherwise.
4. **Frame lifecycle:** Call OnFrameStart→OnRender→OnCompositing→OnFrameEnd
   in sequence. Verify no crashes.

### 10.2 Integration Tests

1. **Dispatch skeleton:** Run a full frame through the 23-stage skeleton.
   Verify all stages execute (as no-ops) without errors.
2. **Viewport resize:** Change viewport between frames. Verify SceneTextures
   resizes.
3. **Binding ownership/publication:** Verify `SceneRenderer` owns and refreshes
   `SceneTextureBindings`, and that publication flows through Renderer Core
   helpers rather than pass-local synthesis.
4. **Renderer wiring:** Verify Renderer correctly delegates to SceneRenderer.

### 10.3 Smoke Test

The Phase 1 smoke test (step 1.9) extends to Phase 2:

```cpp
TEST(VortexSceneRendererTest, ShellDispatchRunsWithoutCrash) {
  // Build SceneRenderer with minimal capabilities
  // Run one frame cycle
  // Verify no crash, no errors
}
```

## 11. Open Questions

None — the shell is mechanical scaffolding. Actual stage logic is designed in
Phase 3 LLDs.
