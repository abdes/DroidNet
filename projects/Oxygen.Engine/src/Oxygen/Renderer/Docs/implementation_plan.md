# Renderer Module Implementation Plan (Living Document)

Status: Living roadmap for achieving feature completeness of the Oxygen
Renderer. Scope: Engine-side rendering layer (Renderer, RenderPass subclasses,
RenderContext integration) – not editor UI or tooling.

**Last Updated**: January 5, 2026

**Document Status**: Phase 5 (Lighting Integration) rewritten to reflect complete
Scene-side implementation and provide detailed, implementable renderer tasks.
Phases 1–4 remain complete as of December 2025.

Keep this document updated at the end of every meaningful change (additions,
refactors, removed scope). Use concise checklists; link to design docs instead
of duplicating content.

Cross‑References:

* Pass lifecycle & conventions: render_pass_lifecycle.md, bindless_conventions.md
* Scene preparation (fully documented): scene_prep.md
* Texture binding (fully documented): texture-binder.md
* Render context & pass registry: render_graph.md
* Existing passes: passes/depth_pre_pass.md, passes/shader_pass.md, passes/transparent_pass.md
* Patterns & data flow: render_graph_patterns.md, data_flow.md
* Render items: render_items.md

Legend: [ ] pending, [~] in progress, [x] done, [d] deferred, [r] removed.

## Completed Phases (Summary)

### Phase 1 – Material Constants & Renderer Snapshot [x]

* [x] Material constants snapshot implemented and owned by Renderer
* [x] Example migrated to use Renderer setters
* [x] Bindless indices buffer management integrated
* [x] Dirty tracking & just-in-time upload implemented

### Phase 2 – Mesh Resources & Bindless Abstraction [x]

* [x] Mesh SRVs (vertex/index) created and owned by Renderer
* [x] Cached in `MeshGpuResources` with stable SRV indices
* [x] `EnsureMeshResources()` idempotent; first call allocates, subsequent calls reuse
* [x] DrawResourceIndices automatically updated
* [x] Documentation in `bindless_conventions.md` fully current (updated Dec 2025)

### Phase 3 – RenderItemsList & Scene Integration [x]

* [x] `RenderItemsList` container implemented and integrated into Renderer
* [x] CPU frustum culling for visibility filtering
* [x] `RenderContext` updated with per-view SoA support (`PreparedSceneFrame`)
* [x] Example migrated to container-based item management

### Phase 4 – ScenePrep Pipeline & Frame Building [x]

* [x] Full ScenePrep two-phase system implemented (Collection + Finalization)
* [x] `Renderer::BuildFrame()` integrates extraction with view culling
* [x] View/Frustum abstractions complete
* [x] Example migrated to new frame building API
* [x] ScenePrep documentation complete (scene_prep.md - current as of Dec 2025)
* [x] Texture binding fully implemented (texture-binder.md - current as of Dec 2025)

## Phase 5 – Lighting Integration [pending]

### Stage 5A – LightManager & Extraction [ ]

Single class that collects lights during traversal and uploads to GPU.

**Tasks:**

* [X] Update Bindless Module
* [X] GPU structs
* [X] Environment structs
* [X] LightManager
* [X] Integrate into ScenePrepPipeline::Collect()
* [X] Root Signature Update
* [X] Extend SceneConstants::GpuData
* [X] Shader update

---

### Stage 5B – Clustered Light Culling [ ]

Implement a Compute Shader to assign lights to 3D frustum clusters (Clustered Shading).

**Tasks:**

* [ ] **Cluster Config**:
  * Define cluster grid dimensions (e.g., 16 x 9 x 24).
  * Add `ClusterConfig` to `EnvironmentDynamicData` (scale/bias for logarithmic Z-binning).
* [ ] **LightCulling.hlsl (Compute)**:
  * **Inputs**: `PositionalLightData` buffer, Depth Prepass SRV (optional for tighter culling, or just AABB intersection).
  * **Logic**:
    1. Compute Cluster AABB (screen tile + depth slice).
    2. Intersect Sphere/Cone vs AABB.
    3. Append light indices to `GlobalLightIndexList`.
    4. Write `{offset, count}` to `ClusterGrid` (RWStructuredBuffer or RWTexture3D).
  * **Safety**: Hard cap lights per cluster (e.g., 64 or 128).
* [ ] **LightCullingPass** (`src/Oxygen/Renderer/RenderPass/LightCullingPass.h/.cpp`):
  * Dispatches the compute shader.
  * Manages `ClusterGrid` and `GlobalLightIndexList` buffers.
  * Exposes SRV indices for the grid and index list.
* [ ] **ShaderPass Integration**:
  * `ForwardMesh.hlsl`:
    * Compute Cluster Index from `SV_Position.xy` and Linear Depth.
    * Read `ClusterGrid[cluster_index]` to get offset/count.
    * Loop `GlobalLightIndexList` from offset.
* [ ] **Add to KnownPassTypes**.

---

### Stage 5C – Per-Draw Light Assignment [deferred] [ ]

Assign bounded light subset per draw for shadows and performance cap.

**Deferred until shadow mapping phase.** Design preserved here.

**Tasks:**

* [ ] **LightManager::AssignPerDraw()**: Score positional lights by importance,
  select top-N per object. Store `{offset, count}` in per-draw buffer.
* [ ] **Extend DrawMetadata**: Reuse padding slot for `light_range_offset`.
* [ ] **GPU buffers**: `PerDrawLightRanges`, `PerDrawLightIndices`.
* [ ] **Shader path**: If `light_range_offset != ~0u`, use per-draw list.
  Else fall back to tile list or global loop.
* [ ] **Quality setting**: `renderer.per_draw_light_assignment` (default off).

---

**Deliverable**: Scene-driven lighting via bindless buffers. Forward+ tile
culling for positional lights. Per-draw assignment deferred to shadow phase.

## Phase 6 – DrawPacket Introduction & Opaque Submission Refactor [pending]

Goal: Transition passes to consume low-level `DrawPacket` for sorting & future instancing.

**Status**: Not started. This phase will refactor passes to consume pre-sorted draw packets instead of the current direct item iteration.

Tasks:

* [ ] Define `DrawPacket` struct (final field table per design doc in render_items.md).
* [ ] Implement packet builder: consumes validated `RenderItemsList` → opaque packets.
* [ ] Opaque sort key (material | mesh | depth bucket) & integration into ShaderPass.
* [ ] Update `RenderContext` to expose packet spans; DepthPrePass may still read bounds directly (evaluate necessity).
* [ ] Debug asserts: packet build only after all items validated & scene constants set.
* [ ] Optional basic instancing grouping (hash mesh + material) [d].
* [ ] Unit tests: packet sort determinism, bounding volume transform correctness, malformed bounds assert.
* [ ] Docs: render_items.md (finalization), implementation_plan.md references updated, performance note (packet build vs direct submission).

## Phase 7 – Transparent Pass (Basic Sorting) [x — implementation exists]

**Status**: `TransparentPass` implemented and integrated into KnownPassTypes.
Basic sorting and back-to-front ordering framework in place.

Goal: Support alpha blended geometry after opaque packet pipeline stable.

Tasks:

* [x] Populate `transparent_draw_list` (from extraction in Phase 4).
* [x] Implement `TransparentPass` (color target, depth read, blend-enabled PSO, depth read-only transitions).
* [ ] Back-to-front CPU sort by camera distance (64-bit key: depth | material | mesh documented in render_items.md).
* [ ] Docs updates: Create passes/transparent_pass.md, update data_flow.md, bindless_conventions.md pass table.

Deferred: OIT techniques.

## Phase 8 – Post Process Pass (Tone Map Stub) [pending]

Goal: Full-screen post step reading prior color target.

**Status**: Not started.

Tasks:

* [ ] `PostProcessPass` (inputs: previous color SRV, scene constants) output: framebuffer color (or target override).
* [ ] Basic tone map (linear → gamma) via fullscreen shader.
* [ ] Ensure PSO root layout compatibility (reuse SRV table; append texture SRV binding only if needed – document if added).
* [ ] Update bindless_conventions.md if new binding.
* [ ] Docs: Create passes/post_process_pass.md.

Deferred: Bloom, HDR exposure, FX chain.

## Phase 9 – Resource Lifetime & Memory Optimizations [pending]

Goal: Prepare for more passes & memory pressure.

**Status**: Partial (texture binder has pooling; buffer aliasing not yet implemented).

Tasks:

* [~] Transient texture aliasing plan (texture binder.md specifies async loading; GPU aliasing deferred).
* [ ] Evaluate buffer reuse for light grids / post-process intermediates (pooling).
* [ ] Metrics counters (buffers created, evicted meshes/frame) – debug log.
* [ ] gpu_resource_management.md: metrics & pooling section.
* [ ] Plan SoA culling arrays (centers, radii, layer bits) design note.
* [ ] Evaluate static vs dynamic item partition for culling + sorting reuse.

Eviction (deferred but tracked here; not urgent)

* [ ] Wire `EvictUnusedMeshResources(current_frame)` into frame orchestration
  (e.g., end of `PostExecute`).
* [ ] On eviction/unregister, call
  `ResourceRegistry::UnRegisterResource(*vertex_buffer)` and
  `...(*index_buffer)` to release SRV views/descriptors before buffers are
  destroyed.
* [ ] Ensure `UnregisterMesh(mesh)` uses the same unregistration logic and
  notifies the policy.
* [ ] Add unit test: register views → evict → views unregistered and descriptor
  count drops; cache entry removed.
* [ ] Add metrics: evicted meshes/frame, descriptors freed/frame; log summary in
  debug builds.

Deferred: Actual aliasing implementation (Phase 10 if complexity warrants).

## Phase 10 – Visibility & Performance Enhancements [pending]

Goal: Improve culling fidelity & reduce overdraw.

**Status**: Basic CPU frustum culling implemented in Phase 4. Advanced culling deferred.

Tasks:

* [ ] Hierarchical frustum culling (BVH or loose octree) – pre CollectRenderItems.
* [ ] Depth pre-pass early Z stats toggle & logging (optional).
* [ ] Per-pass timing via timestamp queries (if backend supports).
* [ ] Frame timing summary (debug logging integration).
* [ ] Optional SoA culling path (toggle + benchmark).
* [ ] Per-item motion vector groundwork (store previous transform).
* [ ] LOD selection policy (screen-space error heuristic) during extraction.

Deferred: Occlusion culling (software depth buffer) / GPU queries.

## Phase 11 – Advanced Light Culling (GPU Compute) [pending]

**Prereq**: Phase 5 Stage B stable.

**Status**: Not started.

Tasks:

* [ ] Compute pipeline for clustering (new shader) – descriptor plan + docs.
* [ ] Depth linearization or pyramid generation (DepthPrePass extension or new pass).
* [ ] Replace CPU tile assignment with GPU buffers consumed in ShaderPass.
* [ ] Update passes/light_culling_pass.md (GPU path & fallback docs).
* [ ] Integrate GPU light index buffer consumption into ShaderPass / DrawPacket.

Deferred: Temporal light list reuse.

## Phase 12 – Quality / Hardening [pending]

Goal: Stabilize for broader engine integration & long-term maintenance.

**Status**: Not started.

Tasks:

* [ ] Unit tests: EvictionPolicy (LRU age logic), RenderPass rebuild conditions.
* [ ] Integration tests: pass sequence transition correctness (mock CommandRecorder capture).
* [ ] Failure modes validated (missing depth texture → exception) logged & tested.
* [ ] Doxygen pass for all public Renderer / RenderPass APIs.
* [ ] Final doc sweep – ensure README links current.
* [ ] Unit tests: Sorting key stability (opaque + transparent) once keys implemented.
* [ ] Unit tests: LOD selection heuristic boundaries.
* [ ] Benchmarks: AoS vs SoA culling performance.
* [ ] Validation: Assert `UpdateComputedProperties` invoked before render graph (debug build).

## Deferred / Explicitly Out of Scope (Reassess Later)

* Full generic render graph with automated resource lifetime solver.
* Editor-driven dynamic graph editing.
* Advanced order-independent transparency (per-pixel linked lists, weighted
  blended, etc.).
* Ray tracing integration.

## Revision History

* **January 5, 2026**: Phase 5 (Lighting Integration) completely rewritten.
  Scene-side lighting is now complete (components, APIs, PAK persistence).
  Renderer-side plan restructured into 5 concrete stages (5A–5E) with specific
  file locations, struct definitions, and test requirements. Cross-references
  to `light-deisgn.md` and `lighting_overview.md` added; no content duplication.
* **December 28, 2025**: Document caught up with actual implementation state.
  All phases 1–4 marked complete with verification. Phase 5+ status verified
  as pending. TransparentPass verified as implemented and integrated. Added
  current documentation references (scene_prep.md, texture-binder.md). Updated
  cross-references and scoping.
* **August 14, 2025**: Phase 4 marked complete; example migrated.
* **August 14, 2025**: Phase 3 marked complete; RenderItemsList integrated.
* **August 13, 2025**: Reorganized phases for fast example migration (added
  Phases 1–4 migration checkpoints, renumbered subsequent phases).
* **August 13, 2025 (original)**: Initial plan baseline roadmap.
