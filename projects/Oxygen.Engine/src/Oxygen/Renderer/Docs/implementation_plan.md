# Renderer Module Implementation Plan (Living Document)

Status: Living roadmap for achieving feature completeness of the Oxygen
Renderer. Scope: Engine-side rendering layer (Renderer, RenderPass subclasses,
RenderContext integration) – not editor UI or tooling.

This revision restructures phases to prioritize rapid migration of the existing
example (`Examples/Graphics/Simple/MainModule.cpp`) to the evolving Renderer
implementation. Early phases now deliver the smallest vertical slices needed for
the example to adopt new APIs immediately, reducing churn and eliminating large
“big bang” rewrites.

Keep this document updated at the end of every meaningful change (additions,
refactors, removed scope). Use concise checklists; link to design docs instead
of duplicating content.

Cross‑References:

* Responsibilities & architecture: responsibilities.md
* Pass lifecycle & conventions: render_pass_lifecycle.md,
  bindless_conventions.md
* GPU resource management: gpu_resource_management.md
* Render context & pass registry: render_graph.md
* Existing passes: passes/depth_pre_pass.md, passes/shader_pass.md
* Patterns & data flow: render_graph_patterns.md, data_flow.md
* Render items & view abstraction: render_items.md, view_abstraction.md

Legend: [ ] pending, [~] in progress, [x] done, [d] deferred, [r] removed.

---

## Fast Migration Track Overview

Goal: After each early phase (1–4) the example compiles and uses ONLY public
Renderer APIs for that layer of responsibility. Private ad‑hoc logic inside
`MainModule.cpp` (manual SRV creation, constant buffer uploads, vector
management) is progressively deleted or replaced.

Migration Checkpoints (applied to example):

1. Phase 1: Replace local SceneConstants & MaterialConstants handling with
   Renderer-provided setters. Leave manual RenderItem vector.
2. Phase 2: Remove Ensure* SRV / index buffer logic; call
   `renderer_->EnsureMeshResources(mesh)` (new API) returning
   DrawResourceIndices (or renderer auto-updates). Example no longer allocates
   descriptor handles directly.
3. Phase 3: Switch from raw `std::vector<RenderItem>` to `RenderItemsList`
   container (opaque API); validation & UpdateComputedProperties automatically
   enforced.
4. Phase 4: Replace manual population with `scene::CollectRenderItems(view)`
   pipeline. Example only constructs a View (camera params) and submits.
5. Later phases are transparent to the example (passes added, optimizations)
   unless new optional features are toggled.

Old → New Phase Mapping (high level):

* Old Phase 2 (constants) pulled into new Phase 1.
* Old Phase 1 (scene extraction) deferred until new Phase 4 to unblock earlier
  cleanup.
* Old Phase 6 (DrawPacket) remains later (new Phase 6) but now depends on
  stabilized earlier migration.
* Remaining phases renumbered accordingly.

---

## Phase 0 – Baseline Snapshot (Complete)

Goal: Solidify current minimal renderer (DepthPrePass + ShaderPass, mesh upload,
LRU caching, typed pass registry). Status: [x]

---

## Phase 1 – Constants & Bindless Indices API (Immediate Example Alignment)

Goal: Move ad‑hoc constant buffer and DrawResourceIndices management out of the
example into Renderer public API.

Tasks:

* [x] Define/Finalize `SceneConstants` (matrices, camera position, frame index,
  time) in a shared header; expose `Renderer::SetSceneConstants(const
  SceneConstants&)` (stores & marks dirty before Execute).
* [x] Define minimal `MaterialConstants` struct &
  `Renderer::SetMaterialConstants(const MaterialConstants&)` (optional in API;
  ShaderPass now consumes when provided; example sets it each frame).
* [x] Centralize bindless DrawResourceIndices (vertex/index/is_indexed) in
  Renderer; expose read-only accessor (example stops writing its own upload
  code).
* [x] Implement internal dirty tracking & upload just before graph execution.
* [x] Extend `bindless_conventions.md` with constant buffer & indices buffer
  layout section.
* [ ] Add validation in `Renderer::PreExecute` (assert scene constants provided
  once per frame).
* [ ] Update example: remove local constant buffer creation &
  `UploadIndicesIfNeeded` style logic (checkpoint 1).
* [ ] Docs: update data_flow.md (constants population stage) & render_items.md
  (material override timing).

Deliverable: Example sets camera/world matrices via Renderer API only; no direct
buffer mapping for scene/material/indices.


## Phase 2 – Mesh Resource & Bindless Abstraction (Descriptor Detachment)

Goal: Eliminate manual SRV creation for vertex & index buffers inside the
example.

Tasks:

* [ ] Introduce `Renderer::EnsureMeshResources(const data::Mesh&)` which:
  * Uploads mesh if not resident;
  * Registers (or reuses) SRVs for vertex & index buffers;
  * Updates internal DrawResourceIndices table.
* [ ] Internal caching keyed by mesh asset id (LRU already present – extend if
  necessary).
* [ ] Remove public exposure of descriptor allocator in example path (keep for
  advanced cases elsewhere).
* [ ] Add debug logging summarizing assigned shader-visible indices at first
  creation.
* [ ] Example migration (checkpoint 2): delete `EnsureVertexBufferSrv`,
  `EnsureIndexBufferSrv`, `EnsureBindlessIndexingBuffer`,
  `EnsureMeshDrawResources` usage – replaced by a single call per mesh per frame
  (or per asset load) to `EnsureMeshResources`.
* [ ] Docs: bindless_conventions.md (clarify vertex/index SRV lifecycle),
  data_flow.md (resource preparation stage).

Deliverable: Example no longer manipulates descriptor handles directly.


## Phase 3 – RenderItem Validation & Container Introduction

Goal: Provide a managed container that enforces invariants, simplifying example
code and preparing for extraction.

Tasks:

* [ ] Implement `RenderItemsList` (add/remove, mark dirty, iteration spans) with
  internal validation (non-negative radius, AABB min<=max).
* [ ] Auto-call `UpdateComputedProperties()` on insertion or mutation (debug
  assert if user forgets manual path – reduces footguns).
* [ ] Provide `Renderer::GetOpaqueItems()` returning span for passes (current
  passes adapt with minimal change).
* [ ] Example migration (checkpoint 3): Replace `std::vector<RenderItem>` with
  `RenderItemsList`. Construction path becomes: create mesh & material → build
  RenderItem → add via container API.
* [ ] Docs: render_items.md updated with container semantics & validation
  section.

Deliverable: Example delegates validation & computed property updates; manual
vector removed.


## Phase 4 – Scene Extraction Integration (Deferred from Old Phase 1)

Goal: Switch from manual item creation in the example to scene-driven extraction
while retaining a fallback during development.

Tasks:

* [ ] Implement minimal `View` struct (camera matrices & frustum planes cached).
* [ ] Implement `CollectRenderItems(view)` (scene graph traversal) returning
  opaque + (placeholder) transparent lists.
* [ ] Integrate basic frustum culling (AABB vs frustum planes) – CPU.
* [ ] Populate opaque list; transparent list TODO placeholder.
* [ ] Example migration (checkpoint 4): Example constructs `View` (from camera
  params) and calls `renderer_->BuildFrame(view)`; manual cube creation path
  guarded behind `#if OXYGEN_RENDERER_EXAMPLE_FALLBACK` until stable, then
  removed.
* [ ] Docs: data_flow.md (extraction stage), view_abstraction.md (initial
  version), render_items.md (extraction responsibilities).

Deliverable: Example no longer manually creates a cube per frame; uses scene
extraction.


## Phase 5 – Light Culling Pass (Stub → Minimal Functional)

(Formerly old Phase 3.)

Stage A – Stub:

* [ ] Add `LightCullingPass` to KnownPassTypes & config (reads depth, scene
  constants). Record debug marker only.
* [ ] Register pass; update data_flow.md.

Stage B – Minimal Functional:

* [ ] Define simple `Light` struct (position, radius, color) and CPU array
  upload (structured buffer SRV).
* [ ] Add root binding index (update bindless_conventions.md).
* [ ] Implement coarse tile segmentation on CPU; store per-tile indices buffer.
* [ ] Add passes/light_culling_pass.md documenting memory layout & limits.
* [ ] Integrate buffers into RenderContext (future per-item light influence
  deferred [d]).


## Phase 6 – DrawPacket Introduction & Opaque Submission Refactor

Goal: Transition passes to consume low-level `DrawPacket` for sorting & future
instancing.

Tasks:

* [ ] Define `DrawPacket` struct (final field table per design doc).
* [ ] Implement packet builder: consumes validated `RenderItemsList` → opaque
  packets.
* [ ] Opaque sort key (material | mesh | depth bucket) & integration into
  ShaderPass.
* [ ] Update `RenderContext` to expose packet spans; DepthPrePass may still read
  bounds directly (evaluate necessity).
* [ ] Debug asserts: packet build only after all items validated & scene
  constants set.
* [ ] Optional basic instancing grouping (hash mesh + material) [d].
* [ ] Unit tests: packet sort determinism, bounding volume transform
  correctness, malformed bounds assert.
* [ ] Docs: render_items.md (finalization), implementation_plan.md references
  updated, performance note (packet build vs direct submission).


## Phase 7 – Transparent Pass (Basic Sorting)

Goal: Support alpha blended geometry after opaque packet pipeline stable.

Tasks:

* [ ] Populate `transparent_draw_list` (from extraction in Phase 4).
* [ ] Implement `TransparentPass` (color target, depth read, blend-enabled PSO,
  depth read-only transitions).
* [ ] Back-to-front CPU sort by camera distance (64-bit key: depth | material |
  mesh documented in render_items.md).
* [ ] Docs updates: data_flow.md, passes docs.

Deferred: OIT techniques.


## Phase 8 – Post Process Pass (Tone Map Stub)

Goal: Full-screen post step reading prior color target.

Tasks:

* [ ] `PostProcessPass` (inputs: previous color SRV, scene constants) output:
  framebuffer color (or target override).
* [ ] Basic tone map (linear → gamma) via fullscreen shader.
* [ ] Ensure PSO root layout compatibility (reuse SRV table; append texture SRV
  binding only if needed – document if added).
* [ ] Update bindless_conventions.md if new binding.

Deferred: Bloom, HDR exposure, FX chain.


## Phase 9 – Resource Lifetime & Memory Optimizations

Goal: Prepare for more passes & memory pressure.

Tasks:

* [ ] Transient texture aliasing plan (doc only initially).
* [ ] Evaluate buffer reuse for light grids / post-process intermediates
  (pooling).
* [ ] Metrics counters (buffers created, evicted meshes/frame) – debug log.
* [ ] gpu_resource_management.md: metrics & pooling section.
* [ ] Plan SoA culling arrays (centers, radii, layer bits) design note.
* [ ] Evaluate static vs dynamic item partition for culling + sorting reuse.

Deferred: Actual aliasing implementation (Phase 10 if complexity warrants).


## Phase 10 – Visibility & Performance Enhancements

Goal: Improve culling fidelity & reduce overdraw.

Tasks:

* [ ] Hierarchical frustum culling (BVH or loose octree) – pre
  CollectRenderItems.
* [ ] Depth pre-pass early Z stats toggle & logging (optional).
* [ ] Per-pass timing via timestamp queries (if backend supports).
* [ ] Frame timing summary (debug logging integration).
* [ ] Optional SoA culling path (toggle + benchmark).
* [ ] Per-item motion vector groundwork (store previous transform).
* [ ] LOD selection policy (screen-space error heuristic) during extraction.

Deferred: Occlusion culling (software depth buffer) / GPU queries.


## Phase 11 – Advanced Light Culling (GPU Compute)

Prereq: Phase 5 Stage B stable.

Tasks:

* [ ] Compute pipeline for clustering (new shader) – descriptor plan + docs.
* [ ] Depth linearization or pyramid generation (DepthPrePass extension or new
  pass).
* [ ] Replace CPU tile assignment with GPU buffers consumed in ShaderPass.
* [ ] Update passes/light_culling_pass.md (GPU path & fallback docs).
* [ ] Integrate GPU light index buffer consumption into ShaderPass / DrawPacket.

Deferred: Temporal light list reuse.


## Phase 12 – Quality / Hardening

Goal: Stabilize for broader engine integration & long-term maintenance.

Tasks:

* [ ] Unit tests: EvictionPolicy (LRU age logic), RenderPass rebuild conditions.
* [ ] Integration tests: pass sequence transition correctness (mock
  CommandRecorder capture).
* [ ] Failure modes validated (missing depth texture → exception) logged &
  tested.
* [ ] Doxygen pass for all public Renderer / RenderPass APIs.
* [ ] Final doc sweep – ensure README links current.
* [ ] Unit tests: Sorting key stability (opaque + transparent) once keys
  implemented.
* [ ] Unit tests: LOD selection heuristic boundaries.
* [ ] Benchmarks: AoS vs SoA culling performance.
* [ ] Validation: Assert `UpdateComputedProperties` invoked before render graph
  (debug build).


## Deferred / Explicitly Out of Scope (Reassess Later)

* Full generic render graph with automated resource lifetime solver.
* Editor-driven dynamic graph editing.
* Advanced order-independent transparency (per-pixel linked lists, weighted
  blended, etc.).
* Ray tracing integration.


## Maintenance Checklist (Per Merge)

* Update status boxes here.
* If new pass → add to KnownPassTypes + new doc under passes/ + update
  data_flow.md & bindless_conventions.md if root layout changes.
* Remove obsolete TODOs from design docs.
* If example migration checkpoint achieved, remove corresponding legacy code
  path in example and update this file.

Revision History:

* Reorganized phases for fast example migration (added Phases 1–4 migration
  checkpoints, renumbered subsequent phases) – 2025-08-13.
* Added Phase 6 DrawPacket refactor (retained; moved after extraction) –
  2025-08-13 (original concept preserved).
* Initial plan (pre-reorg) captured earlier baseline roadmap – 2025-08-13.
* Marked Phase 1 MaterialConstants task complete; implemented Renderer-managed
  material snapshot & example migration – 2025-08-13.
* Removed brittle invariant that DrawResourceIndices occupy heap slot 0; slot
  propagated via `SceneConstants.draw_resource_indices_slot`.
  path in example and update this file.
* Marked dirty tracking & just-in-time upload (scene/material/draw indices) implemented via refactored PreExecute helpers – 2025-08-13.
* Extended bindless_conventions.md with constant/material/draw indices buffer layout & slot propagation details – 2025-08-13.
