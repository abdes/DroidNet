# ScenePrep Refactor LLD

**Phase:** Cross-phase refinement supporting Phase 3 deferred core
**Status:** `ready`
**Authority:** This document is the authoritative LLD for the ScenePrep ->
InitViews -> PreparedSceneFrame seam. It refines, but does not replace,
[ARCHITECTURE.md](../ARCHITECTURE.md), [DESIGN.md](../DESIGN.md),
[init-views.md](init-views.md), [depth-prepass.md](depth-prepass.md), and
[base-pass.md](base-pass.md).

## 1. Scope and Context

### 1.1 What This Covers

This document defines the target runtime contract for Vortex ScenePrep after
the Phase-1 substrate migration and before Phase-3 stage implementations are
allowed to depend on it.

It covers:

- ScenePrep ownership and lifetime
- the authoritative traversal model
- the canonical per-view published payload
- the contract between ScenePrep, InitViews, Renderer Core, and downstream
  stage modules
- upload/materialization timing for view-dependent prepared-scene data
- multi-view-safe state isolation

### 1.2 What This Does Not Change

This document does **not** change the architecture-level ownership model:

- `ScenePrep` remains reusable substrate, not a frame owner
- `InitViewsModule` remains the stage-2 owner and publisher of per-view scene
  work packets
- `SceneRenderer` remains the owner of frame order and downstream per-view
  iteration
- `Renderer Core` remains the owner of `RenderContext`, upload infrastructure
  lifetime, view publication, and composition

### 1.3 Why This Document Exists

The migrated ScenePrep substrate is structurally useful, but the surrounding
LLDs currently leave room for contradictory interpretations in four areas:

1. whether the canonical output is `RenderItemData`, `VisiblePrimitive`, or
   `PreparedSceneFrame`
2. whether ScenePrep state is persistent or stack-local per view
3. when view-dependent uploads are allowed to happen
4. whether downstream stages are allowed to re-filter or effectively
   re-traverse scene data on their own

This LLD closes those gaps.

### 1.4 Non-Negotiable Design Goals

1. **One full scene traversal per scene per frame.**
2. **Zero full scene traversals in downstream stages.**
3. **No pass is allowed to rediscover pass routing from the scene graph.**
4. **The canonical published payload must be stable, explicit, and
   multi-view-safe.**
5. **Heavy draw payloads and cache-friendly visibility data must remain
   separated.**
6. **ScenePrep must not silently become a second renderer below
   `SceneRenderer`.**

## 2. Architectural Position and Ownership

### 2.1 Responsibility Table

| Owner | Responsibilities | Must Not Own |
| ----- | ---------------- | ------------ |
| `ScenePrep` substrate | scene traversal, extraction, frame-shared candidate caching, stable-handle resolution support, prepared-scene data assembly support | frame order, view publication policy, downstream per-view iteration, pass execution |
| `InitViewsModule` | stage-2 orchestration, one frame-shared collection pass, one per-view preparation pass per published view, prepared payload publication | frame-global stage ordering, stage-3/stage-9 draw execution |
| `SceneRenderer` | stage ordering, current-view iteration for downstream stages | scene traversal internals, ScenePrep cache policy |
| `Renderer Core` | `RenderContext`, upload infrastructure lifetime/reset, typed publication stack, view lifecycle, composition | scene-policy ownership, scene-stage ordering |
| downstream stage modules | consume prepared payload for the current view and build draw commands | direct scene traversal, ownership of ScenePrep state, ad hoc scene-prep publication |

### 2.2 Hard Boundary Rules

1. `ScenePrepState` is a persistent stage-owned substrate object. It is not
   stack-local scratch created per view.
2. `InitViewsModule` is the only normal runtime caller that may drive
   ScenePrep for the desktop scene-renderer path.
3. `DepthPrepassModule`, `BasePassModule`, `TranslucencyModule`, and later
   stages consume published prepared-scene products only.
4. No downstream stage may access `ScenePrepState` directly.
5. No downstream stage may walk the scene graph, rescan all scene nodes, or
   rebuild material/pass classification for the current view.

## 3. Canonical Data Contracts

### 3.1 Persistent Working State: `ScenePrepState`

`ScenePrepState` is the persistent working set for one active scene domain.
It owns:

- persistent resource helpers (`GeometryUploader`, `TransformUploader`,
  `MaterialBinder`, `DrawMetadataEmitter`)
- frame-shared candidate caches
- per-view transient collected items and retained-item indices

Ownership rule:

- the desktop runtime path owns one persistent `ScenePrepState` per active
  scene-preparation domain
- in the normal runtime path this state is owned by `InitViewsModule`
- non-runtime harnesses may create isolated temporary state explicitly

Reset rule:

- `ResetFrameData()` is called exactly once before the frame-shared collection
  traversal
- `ResetViewData()` is called exactly once before each per-view preparation
  pass
- `ResetViewData()` must not destroy frame-shared caches produced by the
  frame-shared collection pass

Stack-local per-view construction of `ScenePrepState` is forbidden for the
desktop runtime path.

### 3.2 Canonical Published Payload: `PreparedSceneFrame`

`PreparedSceneFrame` is the canonical published prepared-scene payload for one
view within one frame.

Although the type name contains "Frame", its semantic unit for SceneRenderer
consumption is:

- one published view
- in one frame
- after stage-2 ScenePrep preparation/finalization

Authoritative content lives here:

- `render_items`
- draw-metadata bytes / bindless slots
- pass partitions
- draw bounding spheres
- shadow-related per-draw records
- explicit current / previous transform publication slots
- explicit current / previous deformation publication slots
- any other per-view prepared-scene arrays needed by downstream stages

Authoritative rule:

- if a downstream stage needs prepared scene data, it must be reachable from
  `PreparedSceneFrame` or from an explicitly published auxiliary product keyed
  to it

### 3.2.1 Frame-Shared Motion History Boundary

ScenePrep does not own cross-frame motion history. The owning layer is the
renderer.

Required split:

- renderer-owned rigid-transform history cache:
  - keyed by stable scene identity (`NodeHandle`)
  - rolls current -> previous once per frame
  - retires stale entries independently of view preparation
- renderer-owned deformation-history cache:
  - current-state source identity:
    - `NodeHandle`
    - producer family
    - deformation contract hash
  - renderer publication/history identity:
    - `NodeHandle`
    - geometry asset key
    - LOD index
    - submesh index
    - producer family
    - deformation contract hash
  - rolls current -> previous once per frame
  - invalidates on geometry / skeleton / morph / material-deformation contract
    change
  - retires stale entries independently of view preparation
- ScenePrep / InitViews:
  - touches frame-global motion-history state during frame collection
  - resolves stable handles / indices once per scene node per frame
  - publishes frame-global current/previous transform and deformation buffers
    for later per-view consumption

ScenePrep must not use traversal order or view-local allocation order as the
authoritative identity for previous-frame motion data.

### 3.3 Internal Backing Storage

Because `PreparedSceneFrame` is span-based, stage-2 must own stable backing
storage for every published view.

The implementation may name this storage differently, but the contract is:

```cpp
struct PreparedSceneViewStorage {
  std::vector<sceneprep::RenderItemData> render_items;
  std::vector<std::byte> draw_metadata_bytes;
  std::vector<PreparedSceneFrame::PartitionRange> partitions;
  std::vector<glm::vec4> draw_bounding_spheres;
  std::vector<glm::vec4> shadow_caster_bounding_spheres;
  std::vector<glm::vec4> visible_receiver_bounding_spheres;
  std::vector<ConventionalShadowDrawRecord> shadow_draw_records;

  PreparedSceneFrame published_view {};
};
```

Rules:

1. backing storage is isolated per published view
2. spans exported through `PreparedSceneFrame` must remain valid until that
   view is no longer needed for the frame
3. multi-view execution must not alias one view's prepared payload with
   another view's payload

### 3.4 Optional Lightweight Projections

Lightweight helper records are allowed for CPU-side mesh processors, but they
are **not** the canonical published contract.

If such a helper exists, it must:

- be derived from the current view's `PreparedSceneFrame`
- index into `PreparedSceneFrame` arrays rather than back into scene nodes
- avoid duplicating heavyweight geometry/material payloads

Example shape:

```cpp
struct VisiblePrimitiveRef {
  uint32_t render_item_index;   // Index into PreparedSceneFrame::render_items
  uint32_t draw_index;          // Index into draw-metadata order, if needed
  float distance_sq;            // Cached for sort/refinement only
  uint32_t flags;               // Local classification bits
};
```

If code or LLD text uses a "visible primitive" helper, it must mean a helper
of this kind. It must not mean a second authoritative scene-prep payload.

### 3.5 Published Auxiliary Products

The following auxiliary products may be published alongside
`PreparedSceneFrame`:

- velocity / motion classification keyed to draw order
- current / previous transform publication handles or slots
- current / previous deformation publication handles or slots
- culling statistics
- optional compact draw-index lists for stage-local hot paths

All auxiliary products must be keyed by prepared-frame indices, draw order, or
stable scene identity. They must never be keyed only by raw scene traversal
order.

## 4. Runtime Sequence and Traversal Budget

### 4.1 Authoritative Runtime Sequence

The desktop runtime path must follow this sequence:

```cpp
void InitViewsModule::Execute(RenderContext& ctx, SceneTextures& scene_textures) {
  auto& state = scene_prep_state_;
  const auto* scene = ctx.GetScene().get();
  const auto frame_id = ctx.frame_sequence;

  CHECK_NOTNULL_F(scene, "InitViewsModule requires an active scene");

  // Resolve frame-global motion history and transform/deformation publication
  // once for the scene before any per-view refinement starts.
  scene_prep_->BeginFrameCollection(*scene, frame_id, state);

  for (const auto& view_entry : ctx.frame_views) {
    if (!view_entry.is_scene_view || view_entry.resolved_view == nullptr) {
      continue;
    }
    auto& storage = AcquirePreparedSceneViewStorage(view_entry.view_id);

    scene_prep_->PrepareView(*scene, *view_entry.resolved_view, frame_id, state);
    scene_prep_->FinalizeView(state);
    PublishPreparedSceneFrame(state, storage.render_items, storage.prepared_frame);
  }
}
```

The phase split above is binding:

- one frame-shared collection phase
- one frame-shared motion-history / transform-resolution phase
- one per-view preparation phase
- one per-view finalization/publication phase

### 4.2 Traversal Budget

This budget is mandatory for the normal desktop runtime path.

| Operation | Allowed Count |
| --------- | ------------- |
| full scene graph traversal for one active scene | exactly 1 per frame |
| scan over cached filtered candidates | at most 1 per published view |
| build pass routing / partitions | exactly 1 per published view |
| full scene graph traversal in stages 3, 9, 18, 22 | 0 |
| reclassification of material/pass routing from raw scene data in downstream stages | 0 |

Interpretation:

- Vortex may pay `O(scene)` once per scene per frame
- Vortex may then pay `O(candidates_for_view)` per view
- Vortex must not degrade into `O(scene * number_of_views * number_of_passes)`
- previous-frame motion identity must also be resolved in the `O(scene)` phase,
  not once per view
- current authoritative producer state is instance-keyed, while renderer
  publications/history are LOD-aware render identities materialized for the
  active views in the frame

### 4.3 Multiple Scenes

If a frame ever contains multiple distinct scenes, the budget applies per
scene:

- one `ScenePrepState` per active scene domain
- one full traversal per scene
- per-view preparation for the views that belong to that scene

This document does not authorize mixing different scenes into one
`ScenePrepState`.

## 5. Interface Contract

### 5.1 Phase-Explicit ScenePrep API

The target ScenePrep API must be phase-explicit. A boolean `reset_state`
parameter is not sufficient as the primary runtime contract because it is too
easy to misuse.

Target contract:

```cpp
class ScenePrepPipeline {
 public:
  void BeginFrameCollection(
      const scene::Scene& scene,
      frame::SequenceNumber frame_id,
      ScenePrepState& state);

  void PrepareView(
      const scene::Scene& scene,
      const ResolvedView& view,
      frame::SequenceNumber frame_id,
      ScenePrepState& state);

  void FinalizeView(ScenePrepState& state);
};
```

Migration note:

- the legacy `Collect(..., reset_state)` / `Finalize()` shim is retired from
  the Vortex runtime-facing API
- prepared-scene backing storage remains `InitViewsModule` ownership;
  `ScenePrepPipeline` finalizes state, then stage-2 publication copies it into
  per-view storage
- `PrepareView()` / `FinalizeView()` are phase-continuation calls against the
  same scene, frame sequence, and `ScenePrepState` established by
  `BeginFrameCollection()`; swapping or skipping those phases is a contract
  violation

### 5.2 `InitViewsModule` Ownership Contract

`InitViewsModule` owns:

- one persistent `ScenePrepPipeline`
- one persistent `ScenePrepState`
- per-view prepared-scene backing storage reused across frames

`InitViewsModule` does **not** own the authoritative cross-frame motion-history
cache. It consumes renderer-owned history and publishes the current frame's
prepared-scene view over it.

Target ownership shape:

```cpp
class InitViewsModule {
 private:
  Renderer& renderer_;
  std::unique_ptr<sceneprep::ScenePrepPipeline> scene_prep_;
  sceneprep::ScenePrepState scene_prep_state_;
  std::unordered_map<ViewId, PreparedSceneViewStorage> prepared_views_;
};
```

### 5.3 Publication Contract

The stage-2 published product is a per-view `PreparedSceneFrame`.

Publication rule:

- stage 2 publishes one prepared-scene payload per published scene view in
  `RenderContext::frame_views`
- `InitViewsModule` retains the per-view backing storage and exposes
  `GetPreparedSceneFrame(ViewId)` as the stage-local publication surface
- `SceneRenderer` binds the selected payload into
  `RenderContext.current_view.prepared_frame` before downstream per-view stages
- the normal runtime path must not publish raw `ScenePrepState`

If helper visibility lists are published, they are auxiliary products only and
must be tied to the same prepared-scene publication.

## 6. Resource Management and Upload Timing

### 6.1 Ownership Split

Upload infrastructure ownership remains:

- **Renderer Core:** upload coordinator lifetime, per-frame reset/opening of
  upload windows, transient allocator lifetime, `RenderContext`
- **ScenePrep / InitViews:** view-dependent writes into those already-open
  facilities

This means view-dependent ScenePrep finalization is allowed to materialize GPU
payloads during stage 2, but only through Renderer-Core-owned infrastructure
that was reset/opened earlier in the frame.

### 6.2 Finalization Split

ScenePrep finalization has two logical halves:

1. **CPU finalization**
   - finalize retained render items for the current view
   - emit draw metadata
   - sort and partition once
   - leave `ScenePrepState` ready for stage-owned publication
2. **GPU materialization**
   - ensure view-dependent GPU buffers for the current view are allocated
   - upload draw metadata and related view-local arrays
   - expose bindless SRV indices for the later `PreparedSceneFrame` publish step

### 6.3 Timing Rules

1. `BeginFrameCollection()` is CPU-only.
2. `PrepareView()` is CPU-only.
3. `FinalizeView()` may perform view-dependent uploads through the core-owned
   upload helpers.
4. `InitViewsModule` publishes `PreparedSceneFrame` immediately after
   `FinalizeView()`.
5. Downstream stages must not "repair" missing ScenePrep uploads.
6. No stage after InitViews may call ScenePrep finalization for the current
   view.

## 7. Downstream Consumption Contract

### 7.1 What DepthPrepass, BasePass, and Later Stages Consume

The primary input to downstream stages is the current view's
`PreparedSceneFrame`.

At minimum, downstream stages may consume:

- `render_items`
- `partitions`
- draw-metadata bindless slots
- draw bounding spheres
- any stage-specific auxiliary lists keyed to prepared-frame indices

### 7.2 What Downstream Stages Must Not Do

Downstream stages must not:

- traverse `scene::Scene`
- iterate all scene nodes to rebuild visibility
- classify material/pass routing from raw materials again
- construct their own competing prepared-scene payloads

### 7.3 Pass Routing

Pass routing is built once during stage 2.

Rules:

1. coarse pass-family routing remains expressed through `PassMask`
2. `PreparedSceneFrame::partitions` is the canonical coarse routing map
3. mesh processors may refine within those ranges, but must start from the
   published prepared payload
4. stage-3, stage-9, and later stages must not rescan the entire prepared item
   set just to rediscover coarse pass eligibility

## 8. Multi-View Rules

### 8.1 Per-View Isolation

Every published view gets its own prepared-scene backing storage and published
payload.

This is required even when:

- all views reference the same scene
- all views use the same shading mode
- all views have identical capability sets

### 8.2 Shared Work vs View-Specific Work

Shared across views:

- one full scene traversal
- frame-shared candidate filtering
- view-invariant cached node basics

View-specific:

- LOD selection
- frustum / visibility refinement
- draw metadata order for the view
- dynamic-primitive classification
- per-view uploaded prepared-scene payload

### 8.3 No Cross-View Accumulation Bug

It is a defect if per-view prepared items accumulate into one shared
`collected_items_` output and later views cannot be isolated from earlier
views.

The contract is:

- frame-shared caches may be shared
- published prepared payloads may not be shared or merged across views

## 9. Testability Approach

1. **Traversal-budget test:** Instrument the runtime path and verify one full
   scene traversal for one scene across an N-view frame.
2. **Downstream no-retraversal test:** Verify stage 3 and stage 9 execute
   without invoking scene traversal utilities.
3. **Publication test:** Verify each published view receives a distinct
   `PreparedSceneFrame` with stable spans and bindless slots.
4. **Partition test:** Verify stage-2 partition building happens once per view
   and that stage 3 / stage 9 consume published partitions without rebuilding
   coarse routing from raw scene data.
5. **Multi-view isolation test:** Render two views of the same scene with
   different frusta; verify prepared payloads differ appropriately and do not
   alias storage.
6. **Harness compatibility test:** Verify single-pass / render-graph / offscreen
   harnesses can still build isolated prepared-scene payloads explicitly when
   they opt out of the normal desktop runtime path.

## 10. Drift Blockers

The following are architectural defects, not design variations:

1. constructing `ScenePrepState` on the stack inside the per-view loop
2. making `VisiblePrimitive` the canonical published product instead of a
   helper keyed to `PreparedSceneFrame`
3. allowing stage 3 or stage 9 to rescan the scene graph
4. rebuilding coarse material/pass routing in downstream stages
5. storing one merged prepared-item vector for all views in the frame without
   per-view isolation
6. moving upload infrastructure ownership from Renderer Core into ScenePrep
7. treating ScenePrep as a hidden frame owner below `SceneRenderer`

## 11. Closure

Future GPU-driven culling, occlusion feedback, or visibility compaction work is
allowed only if it preserves the ownership model and traversal budget defined
here.
