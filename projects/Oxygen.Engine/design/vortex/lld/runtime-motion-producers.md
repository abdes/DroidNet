# Runtime Motion Producer Support LLD

**Phase:** 3 — Deferred Core enabling support
**Status:** `ready`

## 1. Scope and Context

This document defines the runtime producer architecture required for Vortex to
reach full UE5.7-grade opaque velocity parity for:

- rigid motion
- masked opaque motion
- skinned deformation
- morph/deformation motion
- material-driven WPO
- motion-vector-world-offset
- temporal-responsiveness / pixel-animation metadata when exposed by the
  material contract

This is not a stage-module LLD. It is a cross-cutting runtime support contract
that sits beside `sceneprep-refactor.md` and `base-pass.md`.

This document covers renderer-consumable motion production only. It does not
redefine simulation ownership.

## 2. Domain Boundaries

### 2.1 Renderer vs Physics

`SceneVelocity` is a renderer-owned screen-space history product.

| Domain | Owns |
| ------ | ---- |
| `src/Oxygen/Physics` | simulation-space linear/angular velocity, body poses, solver state |
| `src/Oxygen/PhysicsModule` | scene/physics reconciliation and pose application back to scene nodes |
| `src/Oxygen/Vortex` | render-history caches, current/previous motion publications, Stage-9 velocity production |

Rules:

1. Physics may be an upstream source of node/world pose.
2. Physics must never own `SceneVelocity`, motion-vector textures, or previous
   render history.
3. Renderer motion vectors must account for camera motion, masked alpha
   behavior, deformation, and material-driven offsets; simulation velocity alone
   is not sufficient.

### 2.2 Renderer vs Scripting

`src/Oxygen/Scripting` may author transform/material/deformation intent, but it
does not own render-history publication. The renderer consumes stable runtime
state after scripting/scene mutation phases have already committed it.

### 2.3 Renderer vs Data/Content

`src/Oxygen/Data` and `src/Oxygen/Content` own cooked schemas, asset wrappers,
and resource loading only. They do not own per-instance current/previous
runtime motion state.

### 2.4 Concrete Oxygen Home

The missing runtime producer layer must live in Oxygen outside Vortex as a new
phase-aware bridge module under:

- `src/Oxygen/SceneSync/RuntimeMotionProducerModule.h`
- `src/Oxygen/SceneSync/RuntimeMotionProducerModule.cpp`

Reason:

- `SceneSync` already exists as the engine-facing bridge surface for
  scene-related synchronization at phase boundaries.
- This producer layer is not renderer policy and it is not simulation
  ownership. It is a synchronization/publication layer between authored/runtime
  state and renderer consumption.

Engine-phase contract:

1. `kSceneMutation`
   - producer-local mutation bookkeeping only
   - examples:
     - accumulate pending script-driven deformation inputs
     - ingest physics-backed deformation intents when such producers exist
     - update local dirty/version state for current-frame producer inputs
   - this phase must not assume other modules have already finished, because
     `kSceneMutation` is barriered concurrency in Oxygen
2. `kTransformPropagation`
   - `RuntimeMotionProducerModule` does not own a second transform barrier
   - it must not call `Scene::Update()` and must not assume post-propagation
     world caches are available inside its own handler
3. `kPublishViews`
   - authoritative ordered freeze point for current-frame producer state
   - publish one immutable `PublishedRuntimeMotionSnapshot` for the frame
   - no further mutation of the published snapshot for that frame
   - resolve final current authoritative producer inputs for renderer
     consumption using the already-committed state from earlier phases
4. `kRender`
   - Vortex Stage 2 consumes the frozen current-frame producer state after the
     existing engine/renderer transform-propagation barrier has completed
   - Vortex combines:
     - propagated rigid/world state sampled during scene prep
     - frozen current-frame producer state from
       `RuntimeMotionProducerModule`
     - renderer-owned previous-frame history caches

Successful-frame commit/reset contract:

- `RuntimeMotionProducerModule` owns current-frame authoritative producer state
  only
- Vortex owns previous-frame render-history caches and rolls them only after a
  successful frame completes
- failed/aborted frames do not advance previous-frame render history

Transform barrier rule:

- the authoritative transform-propagation barrier remains singular
- `RuntimeMotionProducerModule` is not a post-`Scene::Update()` sampler
- Vortex remains the post-propagation sampler of world/LOD/render identity
  state during Stage 2 / `InitViews`

Priority rule:

- `RuntimeMotionProducerModule` must run before the renderer in ordered phases
  (`kPublishViews`)
- it therefore must use a numerically lower priority than
  `kRendererModulePriority`
- the contract relies on Oxygen’s ascending-priority execution buckets for
  ordered phases

Required public surface:

```cpp
class RuntimeMotionProducerModule final : public engine::EngineModule {
 public:
  [[nodiscard]] auto GetPublishedSnapshot(observer_ptr<scene::Scene> scene) const
    -> const PublishedRuntimeMotionSnapshot*;
};
```

`PublishedRuntimeMotionSnapshot` is immutable during `kRender`.

## 3. What Exists Today

### 3.1 Real Runtime Producers

- Scene/node transforms:
  - local/world TRS
  - dirty propagation
  - world-matrix recomputation
- Scripting:
  - can move nodes
  - can swap geometry/material state
- PhysicsModule:
  - can reconcile body pose back to scene nodes

### 3.2 Schema / Asset Support Only

- skinned mesh buffers and skeleton asset keys in `Data` / `Content`
- nominal morph mesh type
- material asset flags, UV transforms, shader references

### 3.3 Missing Runtime Producers

- no skeleton runtime / pose owner
- no animation runtime
- no morph/deformation runtime
- no renderer-facing deformation bridge
- no runtime WPO producer contract
- no soft-body mesh deformation output path

## 4. Producer Families

### 4.1 Rigid Motion Producer

Scope:

- world transform of a renderable scene node
- camera motion via previous/current view state

Owner:

- renderer-owned rigid transform history cache

### 4.2 Skinned Pose Producer

Scope:

- current joint palette
- skeleton topology identity
- geometry/submesh binding identity

Owner:

- current authoritative state: `RuntimeMotionProducerModule`
- previous-frame render history: Vortex renderer-owned cache

### 4.3 Morph / Deformation Producer

Scope:

- current morph-weight buffer
- deformation layout identity

Owner:

- current authoritative state: `RuntimeMotionProducerModule`
- previous-frame render history: Vortex renderer-owned cache

### 4.4 Material WPO Producer

Scope:

- current WPO input state
- previous WPO input state
- WPO contract identity

Owner:

- current authoritative state: `RuntimeMotionProducerModule`
- previous-frame render history: Vortex renderer-owned cache

### 4.5 Motion-Vector-World-Offset Producer

Scope:

- material-declared per-pixel motion-vector offset path

Owner split:

- `RuntimeMotionProducerModule` owns only the time-varying current-frame input
  values and capability flags that say a producer requires MVWO handling
- `BasePassModule` / Stage 9 owns the auxiliary MVWO texture, resolve/merge
  step, and the final `SceneVelocity` publication gate

## 5. Runtime Owners and Lifecycle

### 5.1 Rigid History Cache

Owner:

- renderer-owned cache keyed by `scene::NodeHandle`

Contract:

- `BeginFrame(frame_sequence)`
- `TouchCurrent(node_handle, current_world)`
- `EndFrame()`

Rules:

- current -> previous rolls once per successful frame
- stale entries trim by seen-frame age
- invalidates on node destruction or scene change
- if previous rigid history is missing or invalid for a render identity in the
  current frame:
  - seed `previous_rigid = current_rigid`
  - mark rigid previous-history validity false for that frame
  - force zero/fallback object-motion velocity for the rigid contribution in
    Stage 9

### 5.2 Deformation History Cache

Owner:

- Vortex renderer-owned previous-frame history cache

Current authoritative state owner:

- `RuntimeMotionProducerModule`

Current-state source identity:

- instance identity:
  - `NodeHandle`
  - producer family
  - current authoritative contract hash

Renderer publication / history identity:

- render identity:
  - `NodeHandle`
  - geometry asset key
  - LOD index
  - submesh index
  - producer family
  - deformation contract hash

This split is required because Oxygen performs per-view LOD selection. Current
authoritative state is per instance; renderer publications/history are
per-render-identity.

Stored state:

- previous published skinned pose snapshot handle(s)
- previous published morph/deformation snapshot handle(s)
- previous published material-deformation snapshot handle(s)
- frame-seen / frame-updated stamps

Invalidation:

- node destruction
- scene change
- geometry identity change
- skeleton topology / joint-layout change
- morph layout change
- deformation contract change
- material WPO contract change

Roll/trim:

- current -> previous once per successful frame
- stale entries trim by seen-frame age
- cache/publication is resolved per render identity for the set of active views
  in the current frame
- if previous deformation history is missing or invalid for a render identity in
  the current frame:
  - seed `previous_* = current_*` for the affected producer family
  - mark that producer family's previous-history validity false for that frame
  - force zero/fallback object-motion velocity for that producer contribution
    in Stage 9 instead of reusing stale data

### 5.2.1 Current Producer Payloads

These payloads are authoritative. They are not optional alternatives.

Skinned current-state payload:

- current joint palette buffer
- skeleton topology hash
- skeleton asset key
- skinning layout hash

Skinned renderer publication:

- `PublishedSkinnedPoseSlot` keyed by render identity

Morph current-state payload:

- current morph-weight buffer
- morph-layout hash

Morph renderer publication:

- `PublishedMorphWeightSlot` keyed by render identity

Material WPO current-state payload:

- current material-deformation parameter block
- compiled WPO capability bits
- material-deformation contract hash

Material WPO renderer publication:

- `PublishedMaterialWpoSlot` keyed by render identity

Motion-vector-world-offset / temporal metadata payload:

- current material parameter block fields needed by MVWO
- compiled material capability bits:
  - uses motion-vector world offset
  - uses temporal responsiveness
  - has pixel animation

Motion-vector status renderer publication:

- `PublishedMotionVectorStatusSlot` keyed by render identity

### 5.2.2 Previous Producer Publications

Vortex previous-frame history caches must retain the previous publication
snapshots for the same renderer publication families:

- previous joint palette buffer
- previous morph-weight buffer
- previous material-deformation parameter block
- previous capability snapshot binding

### 5.3 Previous View History

Owner:

- `Renderer`

Key:

- published runtime view identity

Stored state:

- previous view matrix
- previous projection matrix
- previous stable projection matrix
- previous inverse view-projection
- previous jitter

Invalidation:

- first frame
- published runtime view recreation
- resize / view-rect change
- camera cut
- scene switch
- projection/stable-projection discontinuity

Rule:

- invalid previous-view state seeds previous = current and requires zero /
  fallback camera-motion velocity for that frame

### 5.4 Invalid-History Fallback Contract

The following fallback rule is mandatory for every producer family:

| Producer family | Missing / invalid previous state behavior |
| --------------- | ----------------------------------------- |
| rigid transform | seed previous rigid transform from current rigid transform; zero/fallback object-motion velocity for this frame |
| skinned pose | seed previous joint palette from current joint palette; zero/fallback skinned object-motion velocity for this frame |
| morph / deformation | seed previous morph/deformation publication from current publication; zero/fallback deformation object-motion velocity for this frame |
| material WPO | seed previous material-deformation parameter block from current parameter block; zero/fallback WPO object-motion velocity for this frame |
| motion-vector status inputs | seed previous status publication from current status publication; zero/fallback MVWO / temporal-responsiveness object-motion contribution for this frame |
| previous view state | seed previous view/projection/jitter from current view/projection/jitter; zero/fallback camera-motion velocity for this frame |

No producer family may reuse stale previous-frame data after invalidation.

## 6. Publication Seams to Vortex

### 6.1 PreparedSceneFrame

`PreparedSceneFrame` must publish:

- current rigid transforms
- previous rigid transforms
- current skinned pose publications
- previous skinned pose publications
- current morph publications
- previous morph publications
- current material WPO publications
- previous material WPO publications
- current motion-vector status publications
- previous motion-vector status publications
- per-draw velocity eligibility flags

### 6.2 DrawFrameBindings

`DrawFrameBindings` must expose explicit current/previous slots. One overloaded
transform slot is not sufficient.

Typed publication families:

- `RigidTransformPublication`
  - owner: Vortex renderer
  - shader consumers: base-pass VS, depth-only VS, any velocity-capable stage
- `SkinnedPosePublication`
  - current owner: `RuntimeMotionProducerModule`
  - previous/history owner: Vortex renderer
  - shader consumers: stage-9 base-pass velocity VS / future skinning paths
- `MorphPublication`
  - current owner: `RuntimeMotionProducerModule`
  - previous/history owner: Vortex renderer
  - shader consumers: stage-9 deformation-capable VS / future morph paths
- `MaterialWpoPublication`
  - current owner: `RuntimeMotionProducerModule`
  - previous/history owner: Vortex renderer
  - shader consumers: stage-9 base-pass VS
- `MotionVectorStatusPublication`
  - current owner: `RuntimeMotionProducerModule`
  - previous/history owner: Vortex renderer
  - shader consumers: stage-9 base-pass PS and MVWO auxiliary path

### 6.3 ViewConstants

`ViewConstants` must expose current and previous view/projection/stable
projection/jitter data for Stage 9.

## 7. Stage Integration

### 7.1 Stage 2

Stage 2 (`InitViews`) responsibilities:

- resolve frame-global motion history once
- resolve typed current/previous renderer publications for the active set of
  render identities produced by the frame's published views
- publish per-view prepared-scene payloads over those frame-global results

### 7.2 Stage 3

Under the active desktop deferred opaque-velocity policy, Stage 3 remains
depth-only.

### 7.3 Stage 9

Stage 9 owns the full opaque velocity producer chain:

1. primary masked/opaque base-pass velocity output
2. optional motion-vector-world-offset auxiliary pass
3. merge/update step
4. output-backed `SceneVelocity` publication result

### 7.4 Stage 10

Stage 10 is the SceneRenderer-owned rebuild/promote/refresh boundary. It calls
`SceneTextures::RebuildWithGBuffers()` as the family-local helper and only then
refreshes bindings after Stage 9 has actually finished the full producer
chain.

## 8. Shader and Resource Contract

Required shader-visible inputs:

- current rigid transform
- previous rigid transform
- current joint palette / skinned pose payload
- previous joint palette / skinned pose payload
- current morph-weight payload
- previous morph-weight payload
- current material-deformation parameter block
- previous material-deformation parameter block
- current view/projection
- previous view/projection
- masked alpha-test material flags
- motion-vector-world-offset capability bits / inputs
- temporal-responsiveness / pixel-animation capability bits / inputs

Required resources:

- Stage-9 opaque velocity MRT
- Stage-9 auxiliary motion-vector-world-offset transient texture
- Stage-9 merge/update resource path
- typed publication buffers for:
  - rigid transforms
  - skinned poses
  - morph weights
  - material WPO parameters
  - motion-vector status inputs

## 9. Validation Plan

### 9.1 VortexBasic Expansion

`VortexBasic` remains the runtime validation surface, but it must be expanded to
include:

- animated rigid opaque geometry
- masked cutout geometry
- skinned geometry driven by a deterministic joint-palette producer
- morph/deformation geometry driven by a deterministic morph-weight producer
- WPO-driven geometry driven by a deterministic material-parameter producer
- deterministic screen-space separation for each producer
- stable capture/debug names for producer-specific analysis

Concrete validation additions:

- deterministic rigid animator
- deterministic skinned pose driver
- deterministic morph-weight driver
- deterministic WPO / MVWO parameter animator
- explicit invalid-history fallback checks:
  - first-frame fallback
  - camera-cut fallback
  - resize/projection-discontinuity fallback
- producer-specific named capture regions
- producer-specific analyzer checks for:
  - rigid
  - masked
  - skinned
  - morph/deformation
  - WPO
  - MVWO auxiliary pass + merge correctness
  - temporal-responsiveness / pixel-animation encoding correctness

### 9.2 Automated Proof

Required automated proof surfaces:

- unit tests for rigid/deformation cache lifecycle and invalidation
- publication seam tests for current/previous slots
- Stage-9 MRT binding and output-backed publication tests
- invalid-history fallback tests for:
  - rigid history invalidation
  - geometry identity change
  - skeleton layout change
  - morph layout change
  - material/WPO contract change
  - previous-view invalidation on first frame / camera cut / resize /
    projection discontinuity
- capture/runtime checks for nonzero producer-specific velocity:
  - rigid
  - masked
  - skinned/deforming
  - WPO

## 10. Resolved Decisions

1. Temporal-responsiveness / pixel-animation metadata is part of the first
   parity wave.
2. Motion-vector-world-offset is part of the first parity wave and requires an
   explicit auxiliary path plus merge/update step.
3. Physics remains pose-only from Vortex’s point of view until a future
   deformation producer publishes mesh-space deformation explicitly.
4. `RuntimeMotionProducerModule` owns current authoritative deformation state;
   Vortex owns previous-frame render-history caches and current/previous render
   publications consumed by Stage 9.
5. The producer layer lives in `src/Oxygen/SceneSync` and freezes its
   current-frame snapshot by `kPublishViews`.

## 11. Non-Goals

This document does not introduce:

- a physics-owned velocity path
- a second authoritative history cache inside upload helpers
- Nanite or unrelated late-translucency parity work

Those stay out of scope unless separately planned.
