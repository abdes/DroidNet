# InitViews Module LLD

**Phase:** 3 — Deferred Core
**Deliverable:** D.8
**Status:** `ready`

## 1. Scope and Context

### 1.1 What This Covers

`InitViewsModule` — the stage-2 orchestrator responsible for visibility
culling, ScenePrep dispatch, per-view command generation, and dynamic
primitive-mask tracking. It is the first scene-specific render stage in the
23-stage frame and the gateway through which all downstream GPU passes
receive their work.

### 1.2 Stage Position

| Position | Stage | Notes |
| -------- | ----- | ----- |
| Predecessor | Stage 1 (OnPreRender) | Frame constants, camera, SceneTextures allocation |
| **This** | **Stage 2 — InitViews** | Visibility + command generation |
| Successor | Stage 3 (DepthPrepass) | Depth-only pass consuming visibility lists |

### 1.3 Architectural Authority

- [ARCHITECTURE.md §6.2](../ARCHITECTURE.md) — stage table, row 2
- [ARCHITECTURE.md §5.1.3](../ARCHITECTURE.md) — frame dispatch pipeline
- UE5 reference: `BeginInitViews` family (~6.5 k lines)

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
  /// Iterates all published views and builds per-view command packets.
  void Execute(RenderContext& ctx, SceneTextures& scene_textures);

 private:
  Renderer& renderer_;

  // ScenePrep pipeline — owns scene traversal + visibility
  std::unique_ptr<ScenePrep::ScenePrepPipeline> scene_prep_;

  // Per-view visibility results (rebuilt each frame)
  struct ViewVisibility {
    ViewId view_id;
    std::vector<VisiblePrimitive> visible_primitives;
    uint32_t dynamic_primitive_mask{0};
  };
  std::vector<ViewVisibility> view_visibilities_;
};

}  // namespace oxygen::vortex
```

### 2.3 Ownership and Lifetime

| Owner | Owned By | Lifetime |
| ----- | -------- | -------- |
| `InitViewsModule` | `SceneRenderer` (unique_ptr) | Same as SceneRenderer |
| `ScenePrepPipeline` | `InitViewsModule` (unique_ptr) | Same as InitViewsModule |
| `ViewVisibility` vec | `InitViewsModule` | Rebuilt per frame |

## 3. Data Flow and Dependencies

### 3.1 Inputs

| Source | Data | Access Pattern |
| ------ | ---- | -------------- |
| Engine | Published CompositionViews (ViewId, camera, ZOrder) | Via Renderer → CompositionView API |
| Engine | Scene node graph (transforms, geometry, materials) | Via ScenePrepPipeline::Collect() |
| SceneTextures | Resolution, format config | Via SceneTexturesConfig |

### 3.2 Outputs

| Product | Consumer | Delivery |
| ------- | -------- | -------- |
| Per-view visible-primitive lists | DepthPrepassModule, BasePassModule | Stored in RenderContext (per-view slot or typed publish) |
| Dynamic primitive masks | BasePassModule (material routing) | Stored alongside visibility lists |
| Culling statistics | DiagnosticsService (Phase 5) | Optional Tracy counters |

### 3.3 Sequence Diagram

```text
SceneRenderer::OnRender(ctx)
  │
  ├─ InitViewsModule::Execute(ctx, scene_textures)
  │     │
  │     ├─ for each published view:
  │     │     ├─ ScenePrepPipeline::Collect(scene, view, frame_id, state, reset)
  │     │     ├─ Frustum culling → visible primitive list
  │     │     ├─ Dynamic primitive mask compilation
  │     │     └─ Store ViewVisibility in ctx
  │     │
  │     └─ ScenePrepPipeline::Finalize()
  │
  ├─ DepthPrepassModule::Execute(ctx, ...)   // consumes visibility
  └─ BasePassModule::Execute(ctx, ...)        // consumes visibility
```

## 4. Resource Management

### 4.1 GPU Resources

InitViewsModule is CPU-only in Phase 3. No GPU resources are allocated.
Visibility is computed on the CPU via frustum culling.

### 4.2 CPU Allocations

| Resource | Lifetime | Strategy |
| -------- | -------- | -------- |
| Visible-primitive vectors | Per frame | Reuse capacity across frames (clear + rebuild) |
| ScenePrepState | Per frame | Owned by ScenePrepPipeline, reset per Collect() |

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

When null: downstream stages receive empty visibility lists. DepthPrepass
and BasePass produce no draw calls. SceneTextures remain in their
post-allocation state.

### 6.3 Capability Gate

`InitViewsModule` requires `kScenePreparation` capability. It is always
instantiated when `SceneRenderer` is created (Phase 3 baseline).

## 7. ScenePrep Integration

### 7.1 ScenePrepPipeline Usage Pattern

```cpp
void InitViewsModule::Execute(RenderContext& ctx,
                               SceneTextures& scene_textures) {
  // Reset per-frame state
  view_visibilities_.clear();

  auto& views = renderer_.GetPublishedViews();

  for (const auto& view : views) {
    ScenePrepState state;
    bool reset = (view == views.front());

    scene_prep_->Collect(
        renderer_.GetActiveScene(),
        view,
        ctx.frame_id(),
        state,
        reset);

    // Build visibility from ScenePrepState
    ViewVisibility vis;
    vis.view_id = view.GetViewId();
    BuildVisiblePrimitiveList(state, view, vis);
    view_visibilities_.push_back(std::move(vis));
  }

  scene_prep_->Finalize();

  // Publish to RenderContext for downstream consumption
  ctx.SetViewVisibilities(view_visibilities_);
}
```

### 7.2 Frustum Culling

Phase 3 implements CPU frustum culling:

1. For each scene node in ScenePrepState, test AABB against view frustum.
2. Nodes passing the test are added to the ViewVisibility's visible list.
3. Dynamic primitives (animated, WPO) are flagged in the dynamic mask.

GPU-driven culling (compute shader, HZB feedback) is deferred to Phase 5
(OcclusionModule integration).

## 8. Visible Primitive Record

```cpp
struct VisiblePrimitive {
  uint32_t node_index;         // Index into ScenePrepState node array
  uint32_t mesh_lod_index;     // Selected LOD level
  uint32_t material_index;     // Material binding index
  float distance_sq;           // Squared distance to camera (for sorting)
  bool is_dynamic;             // True for animated/WPO geometry
  bool casts_shadow;           // From node flags
};
```

Downstream consumers (DepthPrepass, BasePass) iterate this list and filter
by their own criteria (e.g., depth prepass skips translucent, base pass
routes by material shader).

## 9. Testability Approach

1. **Unit test:** Construct `InitViewsModule` with a mock scene containing
   known geometry. Verify that frustum culling produces correct
   visible/invisible splits for a given camera.
2. **Integration test:** Run full InitViews → DepthPrepass → BasePass
   pipeline. Verify that GBuffer contains data only for visible geometry.
3. **RenderDoc validation:** At frame 10, inspect that draw calls in
   DepthPrepass and BasePass match the visibility lists from InitViews
   (draw count ≈ visible primitive count).

## 10. Open Questions

1. **Sorting order:** Should visible primitives be sorted front-to-back for
   depth prepass or should each downstream stage sort independently? Current
   design: InitViews sorts by distance; downstream stages consume in order.
   This may be revisited if GPU-driven sorting is added.
