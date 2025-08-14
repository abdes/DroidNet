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

> Note: Completed phases have been extracted to `implementation_done.md`.

## Phase 2 – Mesh Resource & Bindless Abstraction (Remaining work)

Goal: Finalize documentation for the already-implemented descriptor detachment.

Remaining tasks:

* [ ] Docs: `bindless_conventions.md` (clarify vertex/index SRV lifecycle),
  `passes/data_flow.md` (resource preparation stage). Update references to show
  SRVs are created by Renderer on first ensure.

## Phase 5 – Lighting Foundations (Types → Basic Shading → Multi-Light)

Rationale: Introduce lighting capability from first principles before any
culling. All GPU buffers follow the engine’s bindless design: lights-related
buffers are accessed via indices in the global bindless table, not as
root-signature bindings. Root constants are only used for the existing per-draw
`g_DrawIndex`.

Stage 5A – Light types & scene integration

* [ ] Types: `DirectionalLight { direction_ws, color_rgb, intensity, enabled }`,
  `PointLight { position_ws, radius, color_rgb, intensity, enabled }`.
* [ ] Scene: `LightComponent` attachable to `SceneNode` (Directional or Point;
  Spot [d]). Defaults documented; units clarified (cd or unitless).
* [ ] Extraction: Extend Phase 4 extraction to collect enabled lights into a
  per-frame `RendererLights` CPU snapshot (arrays of directional/point lights).
* [ ] RenderContext: Publish a read-only view of collected lights; no GPU upload
  yet.
* [ ] Docs: add `lighting_overview.md` (units/spaces/limits) and a short `scene
  LightComponent` doc stub; update `passes/data_flow.md` with a “Lights
  Collection” step running alongside item extraction.

Stage 5B – Basic shading (single light; constants only)

* [ ] Shaders: Add simple Lambert diffuse (+ optional Blinn-Phong specular) path
  using a single directional light.
* [ ] Constants: Extend `SceneConstants` or add a small `LightingConstants`
  block that is embedded into `SceneConstants` to include: `ambient_color`,
  `dir_light_direction`, `dir_light_color`, `dir_light_intensity`, and
  `num_lights`.
* [ ] Renderer: Pick the first enabled directional light from extraction and
  populate constants (fallback to ambient-only if none).
* [ ] Docs: add `shading_model.md` (math notes, gamma, spaces). No new bindless
  slot at this stage.

Stage 5C – Multiple lights (bindless buffer SRV; no culling)

* [ ] GPU buffers: Create a structured buffer SRV for lights as a bindless
  resource (AoS layout; 32-byte aligned entries). Do NOT add a root binding;
  declare a bindless slot (e.g., `kLightsBufferSlot`) in
  `bindless_conventions.md`.
* [ ] Shaders: Loop over `min(num_lights, MAX_LIGHTS_PS)` accumulating direct
  lighting. Support Point and Directional lights; Spot [d].
* [ ] Renderer: Upload the per-frame CPU lights array to the GPU buffer (dirty
  tracked). Set `SceneConstants.num_lights` and the bindless index for
  `g_Lights` according to established conventions.
* [ ] Docs: update `bindless_conventions.md` with the lights buffer slot and
  struct packing; add `passes/shader_pass_lighting.md` to describe consumption
  and caps.

Stage 5D – Optional per-draw light selection (CPU)

* [ ] Data layout (bindless):
  * Buffer A (SRV): `PerDrawLightRange[draw_index] = {offset, count}`
  * Buffer B (SRV): `LightIndices[]` (flat uint list)
* [ ] CPU assignment: For each `RenderItem`, sphere–AABB test to pick nearby
  point lights, clamp per-draw count (e.g., 8–16).
* [ ] Shaders: Use `g_DrawIndex` to read the range and then index into
  `LightIndices`. If range is empty, fall back to global list.
* [ ] Bindless: Reserve two additional SRV slots for these buffers; no new root
  bindings.

Deliverable for Phase 5: Basic lit shading with a single directional light (5B),
expanded to multiple lights via one bindless lights buffer (5C). Optional CPU
per-draw selection (5D) improves performance without adding a culling pass.

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
* Phase 4 complete: Implemented View/Frustum, SceneExtraction with CPU culling,
  added Renderer::BuildFrame(), and migrated Simple example to build frames from
  scene+view – 2025-08-14.
