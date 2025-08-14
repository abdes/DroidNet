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
* [x] Add validation in `Renderer::PreExecute` (assert scene constants provided
  once per frame).
* [x] Update example: remove local constant buffer creation &
  `UploadIndicesIfNeeded` style logic (checkpoint 1).
* [x] Docs: update data_flow.md (constants population stage) & render_items.md
  (material override timing).

Deliverable: Example sets camera/world matrices via Renderer API only; no direct
buffer mapping for scene/material/indices.

## Phase 2 – Mesh Resource & Bindless Abstraction (Descriptor Detachment)

Goal: Eliminate manual SRV creation for vertex & index buffers inside the
example.

Tasks:

* [x] Introduce `Renderer::EnsureMeshResources(const data::Mesh&)` which:
  * [x] Uploads mesh if not resident (invoked internally by Ensure path).
  * [x] Registers (or reuses) SRVs for vertex & index buffers and stores shader
    indices in `MeshGpuResources`.
  * [x] Updates internal `DrawResourceIndices` snapshot automatically from the
    ensured mesh (transitional until per-item indices arrive in later phases).
* [x] Internal caching keyed by mesh identity with LRU eviction policy.
  (Implemented; currently keyed by mesh object address. Revisit to switch to a
  stable asset id when available.)
* [x] Remove public exposure of descriptor allocator in example path (keep for
  advanced cases elsewhere).
* [x] Add debug logging summarizing assigned shader-visible indices at first
  creation.
* [x] Example migration (checkpoint 2): delete `EnsureVertexBufferSrv`,
  `EnsureIndexBufferSrv`, `EnsureBindlessIndexingBuffer`, and
  `EnsureMeshDrawResources` usage – replaced by a single call per mesh per frame
  (or per asset load) to `EnsureMeshResources`.
* [~] Docs: bindless_conventions.md (clarify vertex/index SRV lifecycle),
  data_flow.md (resource preparation stage). (Update references to show SRVs are
  created by Renderer on first ensure.)

Deliverable: Example no longer manipulates descriptor handles directly.

Current status (2025-08-13):

* Mesh buffer creation/upload, SRV registration, and caching are implemented in
  `Renderer`. Shader-visible indices are stored with `MeshGpuResources`.
* `Renderer` updates the per-frame `DrawResourceIndices` snapshot when mesh
  resources are ensured and propagates the dynamic bindless slot into
  `SceneConstants`.
* Example calls `renderer_->EnsureMeshResources(*mesh)` and no longer touches
  descriptor allocator or sets draw indices manually.

Next steps to complete Phase 2:

1) Finalize doc updates: SRV lifecycle and resource prep sequence.

## Phase 2.5 – Multi-Draw Item Support (Complete)

Goal: Support multiple draw items (meshes) in a single frame.

**Status: [x] Complete**

Tasks:

* [x] Add `kDrawIndexConstant` root binding for passing per-draw indices via
  root constants
* [x] Implement `SetGraphicsRoot32BitConstant()` and
  `SetComputeRoot32BitConstant()` in CommandRecorder
* [x] Add `BindDrawIndexConstant()` method to RenderPass base class
* [x] Update `IssueDrawCalls()` to bind draw index before each draw call
* [x] Modify shaders to use `g_DrawIndex` root constant instead of
  `SV_InstanceID`
* [x] Ensure consistent root signature layout across all passes (DepthPrePass,
  ShaderPass)
* [x] Update renderer to build `DrawResourceIndices` array for multiple items
* [x] Fix D3D12 bindless rendering limitation where `SV_InstanceID` doesn't work
  without input layout

**Solution**: Root constants provide the Microsoft-recommended approach for
passing per-draw data in bindless scenarios. Each draw call now receives a
unique `draw_index` via root constant (b3, space0), allowing shaders to access
the correct `DrawResourceIndices[g_DrawIndex]` entry.

**Result**: Multiple meshes can now be rendered in a single frame with correct
per-mesh resource binding.

Deliverable: Engine supports rendering multiple distinct meshes per frame using
proper D3D12 bindless patterns.

## Phase 3 – RenderItem Validation & Container Introduction

Goal: Provide a managed container that enforces invariants, simplifying example
code and preparing for extraction.

Tasks:

* [x] Implement `RenderItemsList` (add/remove, mark dirty, iteration spans) with
  internal validation (non-negative radius, AABB min<=max).
* [x] Auto-call `UpdateComputedProperties()` on insertion or mutation (debug
  assert if user forgets manual path – reduces footguns).
* [x] Provide `Renderer::GetOpaqueItems()` returning span for passes (current
  passes adapt with minimal change). Wire `PreExecute` to set
  `context.opaque_draw_list` from the container and ensure resources.
* [x] Example migration (checkpoint 3): Replace `std::vector<RenderItem>` with
  `RenderItemsList`. Construction path becomes: create mesh & material → build
  RenderItem → add via container API.
* [x] Docs: render_items.md updated with container semantics & validation
  section.

Deliverable: Example delegates validation & computed property updates; manual
vector removed.

Current status (2025-08-14):

* `RenderItemsList.cpp` implements validation (non-negative sphere radius; AABB
  min ≤ max) and recomputation via `UpdateComputedProperties()` on
  insert/update; exposes `Items()`, `Add/RemoveAt/Update/Clear/Reserve`, and
  `Size()`.
* `Renderer.h` exposes `OpaqueItems()` for mutation and `GetOpaqueItems()` for
  read-only spans. `PreExecute` wires `context.opaque_draw_list` from the
  container and ensures mesh resources for the list before uploads.
* Example (Simple) migrated to use the container; the local
  `std::vector<RenderItem>` and explicit `EnsureResourcesForDrawList` path were
  removed, with draw list publication handled centrally.
* Docs updated: `render_items.md` includes a Container Semantics section. A
  minor markdown list-style lint remains optional to address.

## Phase 4 – Scene Extraction Integration (Deferred from Old Phase 1)

Goal: Replace manual item creation with a camera-driven extraction pipeline:
scene traversal → culling → `RenderItem` population → renderer-managed list. The
example constructs a View from a camera and submits via a single call. Keep a
configurable fallback during development.

Scope and contracts:

### View abstraction (immutable per-frame snapshot)

* Inputs: camera matrices and render state (from scene camera or app).
* Derived: cached inverses, view-projection, frustum planes.
* Used to: populate `SceneConstants` and perform CPU culling.
* Mapping to `SceneConstants`: `view_matrix`, `projection_matrix`,
  `camera_position` are always set from View.

### Extraction

* Traverse a `scene::Scene` (read-only) and collect renderable nodes.
* Build `RenderItem`s with world transform and computed bounds.
* CPU cull against View frustum before inserting into `opaque_items_`.
* Transparent pipeline is a placeholder; opaque-only for this phase.

### Integration and migration

* New renderer entrypoint consumes a `View` and optionally a `Scene`.
* Example switches to this API and removes manual geometry population.

—

### View: concrete spec (minimal, future-proof)

Contract (inputs/outputs):

* Inputs: `glm::mat4 view`, `glm::mat4 proj`, optional `glm::ivec4 viewport`,
  optional `glm::ivec4 scissor`, `glm::vec2 pixel_jitter` (default 0),
  `bool reverse_z` (default false), `bool mirrored` (default false),
  `glm::vec3 camera_position` (optional; infer from `inverse(view)` if missing).
* Derived (cached at construction): `inv_view`, `inv_proj`, `view_proj`,
  `inv_view_proj`, `Frustum frustum` (6 planes; normals point outward; swap
  near/far for reverse-Z).
* Methods: getters only; immutable snapshot (no setters).

Notes:

* Aligned with typical engines (UE/Frostbite/idTech): render-time snapshot with
  cached matrices and frustum.
* View is the source of truth for values written to `SceneConstants`.

Planned placement: `Oxygen/Renderer/Types/View.h(.cpp)` and
`Oxygen/Renderer/Types/Frustum.h(.cpp)` with helpers `IntersectsAABB(min,max)`
and `IntersectsSphere(center,radius)`.

—

### Scene traversal and extraction: key points and plan

Key points from Scene module (for later implementation reference):

* Traversal API: `scene::SceneTraversal<const Scene>` supports orders
  (BreadthFirst, PreOrder, PostOrder) with visitor returning `VisitResult`
  (`kContinue`, `kSkipSubtree`, `kStop`), and filtering via `SceneFilterT`.
* Non-recursive, cache-friendly traversal over `SceneNodeImpl` pointers.
* Transform system: update via `SceneTraversal.UpdateTransforms()` before
  extraction so world matrices are current.
* Mesh access: `SceneNode::HasMesh()`/`GetMesh()` returns
  `shared_ptr<const data::Mesh>`.
* Visibility: use `SceneNodeImpl::Flags` effective values; reject invisible
  subtrees early.
* Mutation: do not modify the graph during traversal; extraction is read-only.

Minimal extraction algorithm:

1. Ensure transforms are up to date (`SceneTraversal.UpdateTransforms()`).
2. Traverse PreOrder from roots; filter out invisible subtrees early.
3. For nodes with a mesh component:
   * Build a `RenderItem`:
     * `mesh` = node mesh; `material` = first submesh material (limitation: if
       mesh has multiple submeshes, initial version may pick the first only;
       optional enhancement is per-submesh items).
     * `world_transform` from TransformComponent world matrix; call
       `UpdateComputedProperties()` to compute bounds and normal matrix.
     * Snapshot flags (cast/receive shadows) from node flags effective values.
   * Culling: test world AABB (sphere optional pre-test) against `View.frustum`.
   * If visible, insert into `Renderer::OpaqueItems()` container.

—

### Renderer integration and public API changes

New public methods (names tentative):

* `Renderer::BuildFrame(const scene::Scene& scene, const View& view) -> void`
  * Clears and repopulates `opaque_items_` via extraction + culling.
  * Calls `ModifySceneConstants` to set `view/projection/camera_position` from
    View.
  * Leaves material constants untouched; example may still set them per frame.

Optional convenience:

* `Renderer::CollectRenderItems(const scene::Scene&, const View&) -> size_t`
  * Performs extraction and returns items count; `BuildFrame` uses it.

Internals to add:

* `Types/View.{h,cpp}`, `Types/Frustum.{h,cpp}` with plane extraction (Gribb &
  Hartmann) and AABB/sphere tests.
* `Extraction/SceneExtraction.{h,cpp}` implementing traversal + item build.

No pass/root-signature changes required; passes already consume matrices from
`SceneConstants`.

—

### Example migration (checkpoint 4)

* Replace manual cube insertion with a small `scene::Scene` setup:
  * Create a camera node with `scene::PerspectiveCamera` (D3D12 convention),
    set FOV/aspect/near/far; compute View from the camera node’s transform and
    `ProjectionMatrix()`.
  * Create two mesh nodes; attach meshes (reuse debug cube and offset copy);
    set transforms via `SceneNode::GetTransform()` helpers.
* Build the View (no jitter, reverse-Z=false initially) and call
  `renderer_->BuildFrame(scene, view)` before submitting the graph.
* Keep a dev fallback behind a flag: if the scene is empty or no camera, use
  the old procedural path to populate one cube for debugging.

—

### Tasks (precise, testable)

API and types

* [x] Add `Types/Frustum` with 6 planes, `FromViewProj(mat4, reverseZ)` and
  `IntersectsAABB(min,max)`/`IntersectsSphere(center,radius)`.
* [x] Add `Types/View` per spec above; compute cached matrices and frustum.

Extraction

* [ ] Add `Extraction/SceneExtraction.{h,cpp}` with:
  * [ ] `size_t CollectRenderItems(const scene::Scene&, const View&, RenderItemsList&)`.
  * [ ] Pre-order traversal; visibility filter; transform update pre-pass.
  * [ ] Build one `RenderItem` per mesh (initially per-mesh; per-submesh as
    optional enhancement) and call `UpdateComputedProperties()`.
  * [ ] CPU culling with `View.frustum` before insertion.

Renderer wiring

* [ ] Add `Renderer::BuildFrame(const scene::Scene&, const View&)` that:
  * [ ] Clears `opaque_items_`; calls `CollectRenderItems` to repopulate.
  * [ ] Writes `view/projection/camera_position` to `SceneConstants`.
  * [ ] Leaves material constants as-is; doc that materials are per-item for
        later phases.

Example migration and fallback

* [ ] Modify `Examples/Graphics/Simple/MainModule.cpp`:
  * [ ] Create a minimal `scene::Scene` with camera and two mesh nodes.
  * [ ] Build `View` from camera; call `renderer_->BuildFrame(scene, view)`.
  * [ ] Remove direct `OpaqueItems().Add(...)`; keep optional fallback when no
        scene/camera is present.

Docs and tests

* [ ] Update docs:
  * [ ] `Docs/passes/data_flow.md`: add extraction stage before PreExecute.
  * [ ] `Docs/view_abstraction.md`: reflect finalized fields and reverse-Z note.
  * [ ] `Docs/render_items.md`: clarify extraction responsibilities and
    per-submesh item policy (temporary limitation).
* [ ] Unit tests:
  * [ ] Frustum plane extraction and intersection (AABB + sphere; reverse-Z).
  * [ ] Scene extraction happy path: 2 mesh nodes -> 2 items; invisible subtree
    culled; transforms applied.
  * [ ] Null/edge cases: empty scene -> 0 items; mesh with no indices -> still
    extracted; no camera -> BuildFrame sets matrices from provided View only.

Edge cases to handle (acceptance):

* Empty scene or no meshes -> zero items; no crash.
* No camera node: View is provided by the example; `BuildFrame` must not depend
  on scene camera discovery yet.
* Reverse-Z: frustum near/far plane extraction swaps accordingly.
* Multiple submeshes: document current behavior; optionally emit per-submesh
  items in an incremental commit.

Deliverable: Example uses `BuildFrame(scene, view)` to populate items; manual
cube creation removed; opaque-only pipeline renders with culling.

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
  propagated via `SceneConstants.bindless_indices_slot`. path in example and
  update this file.
* Marked dirty tracking & just-in-time upload (scene/material/draw indices)
  implemented via refactored PreExecute helpers – 2025-08-13.
* Extended bindless_conventions.md with constant/material/draw indices buffer
  layout & slot propagation details – 2025-08-13.
* Added PreExecute validation assert for single SceneConstants set per frame –
  2025-08-13.
* Phase 1 example migration checkpoint: example now uses Renderer setters for
  scene/material constants & draw resource indices (removed manual constants
  upload) – 2025-08-13.
* Updated data_flow.md (detailed constants population sequence) &
  render_items.md (material override timing) – 2025-08-13.
* Phase 2 status analyzed and plan updated; marked caching complete and outlined
  SRV/indices migration steps – 2025-08-13.
* Phase 2 implementation: Renderer now owns mesh SRVs and updates
  DrawResourceIndices automatically; example migrated to EnsureMeshResources –
  2025-08-13.
* Phase 3 complete: Introduced `RenderItemsList`, integrated into `Renderer`
  (opaque items container and span accessor), wired `PreExecute` to publish draw
  list and ensure resources, migrated Simple example; docs updated. Manual
  touch-ups to `RenderItemsList.cpp` and `Renderer.h` incorporated – 2025-08-14.
* Phase 3 follow-up: Verified Simple example uses container exclusively and
  added one-time info log when items are first added – 2025-08-14.
