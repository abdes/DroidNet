# InitViews Module LLD

**Phase:** 3 — Deferred Core
**Deliverable:** D.8
**Status:** `ready`

## 1. Scope and Context

### 1.1 What This Covers

`InitViewsModule` — the stage-2 orchestrator responsible for visibility
culling, ScenePrep dispatch, per-view prepared-scene publication, and dynamic
primitive-mask tracking. It is the first scene-specific render stage in the
23-stage frame and the gateway through which all downstream GPU passes receive
their work.

### 1.2 Stage Position

| Position | Stage | Notes |
| -------- | ----- | ----- |
| Predecessor | Stage 1 (OnPreRender) | Frame constants, camera, SceneTextures allocation |
| **This** | **Stage 2 — InitViews** | Visibility + prepared-scene publication |
| Successor | Stage 3 (DepthPrepass) | Depth-only pass consuming the current-view prepared payload |

### 1.3 Architectural Authority

- [ARCHITECTURE.md §6.2](../ARCHITECTURE.md) — stage table, row 2
- [ARCHITECTURE.md §6.3.1](../ARCHITECTURE.md) — deferred-core invariants
- [ARCHITECTURE.md §5.1.3](../ARCHITECTURE.md) — frame dispatch pipeline
- [sceneprep-refactor.md](sceneprep-refactor.md) — authoritative ScenePrep
  runtime contract and traversal budget

### 1.4 Required Invariants For This Module

This module must preserve the following invariants from
[ARCHITECTURE.md §6.3.1](../ARCHITECTURE.md):

- `InitViews` is the per-frame publisher of per-view prepared scene packets
- `SceneRenderer`, not `InitViewsModule`, owns downstream per-view iteration
- downstream stages consume the published prepared-scene payload for the
  current view selected in `RenderContext`
- the desktop runtime path pays one full scene traversal per scene per frame,
  then only per-view refinement over cached candidates

## 2. Interface Contracts

### 2.1 File Placement

Per [PROJECT-LAYOUT.md](../PROJECT-LAYOUT.md):

```text
src/Oxygen/Vortex/
└── SceneRenderer/
    └── Stages/
        └── InitViews/
            ├── InitViewsModule.h
            └── InitViewsModule.cpp
```

### 2.2 Public API

```cpp
namespace oxygen::vortex {

class InitViewsModule {
 public:
  explicit InitViewsModule(Renderer& renderer,
                           const SceneTexturesConfig& config);
  ~InitViewsModule();

  // Non-copyable, non-movable (owns pipeline state)
  InitViewsModule(const InitViewsModule&) = delete;
  auto operator=(const InitViewsModule&) -> InitViewsModule& = delete;

  /// Stage 2 entry point. Called once per frame.
  /// Iterates all published views, builds per-view prepared-scene payloads,
  /// and publishes them for downstream per-view stage execution owned by
  /// SceneRenderer.
  void Execute(RenderContext& ctx, SceneTextures& scene_textures);

 private:
  Renderer& renderer_;

  // Persistent ScenePrep substrate owned for the lifetime of the stage.
  std::unique_ptr<sceneprep::ScenePrepPipeline> scene_prep_;
  sceneprep::ScenePrepState scene_prep_state_;

  // Per-view prepared-scene backing storage, reused across frames.
  struct PreparedSceneViewStorage;
  std::unordered_map<ViewId, PreparedSceneViewStorage> prepared_views_;
};

}  // namespace oxygen::vortex
```

### 2.3 Ownership and Lifetime

| Owner | Owned By | Lifetime |
| ----- | -------- | -------- |
| `InitViewsModule` | `SceneRenderer` (unique_ptr) | Same as SceneRenderer |
| `ScenePrepPipeline` | `InitViewsModule` (unique_ptr) | Same as InitViewsModule |
| `ScenePrepState` | `InitViewsModule` | Persistent across frames |
| per-view prepared-scene storage | `InitViewsModule` | Reused across frames, isolated per view |

## 3. Data Flow and Dependencies

### 3.1 Inputs

| Source | Data | Access Pattern |
| ------ | ---- | -------------- |
| Engine | Published CompositionViews (ViewId, camera, ZOrder) | Via Renderer → CompositionView API |
| Engine | Scene node graph (transforms, geometry, materials) | One frame-shared ScenePrep traversal, then cached-candidate reuse per view |
| SceneTextures | Resolution, format config | Via SceneTexturesConfig |

### 3.2 Outputs

| Product | Consumer | Delivery |
| ------- | -------- | -------- |
| Per-view `PreparedSceneFrame` payloads | DepthPrepassModule, BasePassModule, later per-view stages | Stored in RenderContext / typed per-view publication |
| Dynamic primitive classification | BasePassModule (velocity completion), later stages | Published alongside the prepared-scene payload, keyed to prepared-frame indices |
| Culling statistics | DiagnosticsService (Phase 5) | Optional Tracy counters |

### 3.3 Sequence Diagram

```text
SceneRenderer::OnRender(ctx)
  │
  ├─ InitViewsModule::Execute(ctx, scene_textures)
  │     │
  │     ├─ ScenePrepPipeline::BeginFrameCollection(scene, frame_id, persistent_state)
  │     │     └─ one full scene traversal for the scene
  │     │
  │     ├─ for each published view:
  │     │     ├─ ScenePrepPipeline::PrepareView(scene, view, frame_id, persistent_state, storage)
  │     │     ├─ per-view frustum / LOD refinement over cached candidates only
  │     │     ├─ ScenePrepPipeline::FinalizeView(persistent_state, storage)
  │     │     └─ Publish PreparedSceneFrame for the view
  │     │
  │     └─ Publish optional per-view auxiliary classification products
  │
  ├─ DepthPrepassModule::Execute(ctx, ...)   // consumes current-view PreparedSceneFrame
  └─ BasePassModule::Execute(ctx, ...)       // consumes current-view PreparedSceneFrame
```

## 4. Resource Management

### 4.1 GPU Resources

InitViewsModule owns no render or compute passes in Phase 3. Visibility is
computed on the CPU via frustum culling, but stage-2 finalization may
materialize view-local structured-buffer payloads (for example draw metadata
and related prepared-scene arrays) through Renderer-Core-owned upload helpers.

### 4.2 CPU Allocations

| Resource | Lifetime | Strategy |
| -------- | -------- | -------- |
| per-view prepared-scene backing storage | Per view per frame | Reuse capacity across frames, isolate storage per view |
| `ScenePrepState` | Persistent | Owned by `InitViewsModule`, reset once per frame and once per view by phase |

### 4.3 Future GPU Resources (Phase 5+)

When GPU-driven culling is added (OcclusionModule), InitViewsModule will
produce indirect-draw argument buffers on the GPU. The CPU visibility path
remains as fallback.

## 5. Shader Contracts

None — InitViewsModule performs no GPU work in Phase 3.

## 6. Stage Integration

### 6.1 Dispatch Contract

SceneRenderer calls `InitViewsModule::Execute(ctx, scene_textures)` at
stage 2. If `init_views_` is null, stage 2 is skipped with zero overhead.

### 6.2 Null-Safe Behavior

When null: downstream per-view stages receive no published prepared-scene
payloads.
DepthPrepass and BasePass therefore receive no current-view prepared-scene
payload and emit no draw calls. SceneTextures remain in their post-allocation
state.

### 6.3 Capability Gate

`InitViewsModule` requires `kScenePreparation`. In the Phase 3 deferred-core
baseline that capability is expected to be active; if it is intentionally
absent, stage 2 stays null and no prepared-scene payloads are published.

## 7. ScenePrep Integration

### 7.1 Authoritative ScenePrep Contract

The authoritative ScenePrep runtime contract lives in
[sceneprep-refactor.md](sceneprep-refactor.md). `InitViewsModule` must use the
phase-explicit flow defined there; it must not use a stack-local per-view
`ScenePrepState` or a "first view resets everything" pattern.

### 7.2 ScenePrepPipeline Usage Pattern

```cpp
void InitViewsModule::Execute(RenderContext& ctx,
                               SceneTextures& scene_textures) {
  auto& views = renderer_.GetPublishedViews();
  auto& state = scene_prep_state_;
  const auto& scene = renderer_.GetActiveScene();

  // Exactly one full scene traversal for the scene in this frame.
  scene_prep_->BeginFrameCollection(scene, ctx.frame_id(), state);

  for (const auto& view : views) {
    auto& storage = AcquirePreparedSceneViewStorage(view.GetViewId());

    scene_prep_->PrepareView(scene, view, ctx.frame_id(), state, storage);
    scene_prep_->FinalizeView(state, storage);

    // Publish per-view prepared-scene payload for downstream stages.
    ctx.PublishPreparedSceneFrame(view.GetViewId(), storage.published_view);
  }
}
```

### 7.3 Traversal Model and Per-View Refinement

Phase 3 implements CPU view refinement with this required cost model:

1. perform one full scene traversal to build frame-shared candidate caches
2. for each published view, iterate cached candidates only
3. perform per-view frustum culling, LOD selection, and dynamic classification
   during that cached-candidate scan
4. finalize one published `PreparedSceneFrame` for the view
5. never re-traverse the scene graph in stage 3, stage 9, or later stages

GPU-driven culling (compute shader, HZB feedback) is deferred to Phase 5
(OcclusionModule integration).

## 8. Published Prepared-Scene Contract

The stage-2 canonical published product is `PreparedSceneFrame`, not a raw
`ScenePrepState` and not a second competing scene-prep payload.

If `InitViewsModule` keeps any lightweight "visible primitive" helper for local
CPU hot paths, it is an internal helper only and must be keyed to indices in
the current view's `PreparedSceneFrame`.

## 9. Testability Approach

1. **Unit test:** Construct `InitViewsModule` with a mock scene containing
   known geometry. Verify one frame-shared collection traversal and correct
   per-view refinement for a given camera.
2. **Integration test:** Run full InitViews → DepthPrepass → BasePass
   pipeline. Verify that GBuffer contains data only for geometry present in the
   published prepared-scene payload consumed by downstream per-view stages.
3. **RenderDoc validation:** At frame 10, inspect that draw calls in
   DepthPrepass and BasePass match the published prepared-scene payload from
   InitViews without hidden scene re-traversal in the downstream stages.

## 10. Open Questions

None for the baseline contract. Any future GPU-driven culling or compact
helper-list optimization must preserve the traversal budget and publication
model defined in [sceneprep-refactor.md](sceneprep-refactor.md).
