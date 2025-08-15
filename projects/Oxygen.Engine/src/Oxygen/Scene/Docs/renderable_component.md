# Renderable Component – Design & Integration Spec

This document defines the Renderable component for the Oxygen Scene system and describes how it integrates with Data and Renderer modules, migration from the current MeshData usage, update hooks, culling/LOD behavior, and submission.

---

## Implementation Status (as of Aug 2025)

This section summarizes what is implemented today versus the target design below:

- Implemented now
  - Renderable component that holds a `std::shared_ptr<const data::GeometryAsset>`.
  - LOD policy enumeration and control: `LODPolicy { kFixed, kDistance, kScreenSpaceError }` with hysteresis helpers.
  - SceneNode::Renderable facade (typed, SafeCall-based) via `node.GetRenderable()` with:
    - `SetGeometry(...)`, `Detach()`, `GetGeometry()`, `HasGeometry()`
    - LOD controls: `SetLodPolicy(...)`, `SelectActiveMesh(...)`, `GetActiveMesh()`
  - Transform hook wired for Renderable: `OnWorldTransformUpdated(const glm::mat4&)` recomputes world-sphere and invalidates AABB cache (called from `SceneNodeImpl::UpdateTransforms`).
  - `SetGeometry` rebuilds per-LOD/per-submesh local-bounds cache and clamps fixed LOD.
  - Aggregated world-sphere bounds computed from selected LOD (fallback to asset AABB-derived sphere).
  - On-demand per-submesh world AABB computation for current LOD via 8-corner transform.
  - LOD evaluation helpers and hysteresis:
    - Distance thresholds with simple hysteresis ratio.
    - SSE enter/exit thresholds with directional hysteresis.
  - Per-view dynamic LOD evaluation path: renderer extraction (`SceneExtraction`) computes distance/SSE per view and calls `Renderable::SelectActiveMesh(...)` to set the active LOD for the frame.
  - Per-submesh visibility and material overrides storage + resolution.

- Not yet implemented (planned; covered by design below)
  - Renderer submission façade and final bindless handle resolution.
  - Submission builder with bindless handle resolution and stable instance IDs.

Note (breaking): legacy `SceneNode` geometry helpers
`AttachGeometry`/`ReplaceGeometry`/`DetachRenderable`/`GetGeometry`/`GetActiveMesh`/`HasGeometry`
have been replaced by the `SceneNode::Renderable` facade.

## 1) Goals & Scope

Goals

- Attach renderable geometry to a SceneNode using a first-class Renderable component.
- Map Data::GeometryAsset (LODs → Meshes → SubMeshes) and MaterialAsset to scene nodes, with optional per-submesh overrides.
- Produce efficient, bindless-first per-frame submissions to the Renderer.
- Support LOD selection policies, per-submesh visibility, overrides, and robust bounds/culling.

Scope

- Scene: new Renderable component, per-node state, dirty tracking, and bounds.
- Data interop: consume GeometryAsset/MaterialAsset, resolve submesh views and defaults.
- Renderer bridge: lightweight façade for instance records with bindless indices, stable instance IDs, and PSO-friendly keys.
- Tests/examples: LOD, overrides, flags, bounds, and submission.

---

## 2) Architecture Overview

- SceneNodeImpl gains an attachable Renderable component. Dependencies: TransformComponent and NodeData (flags).
- Renderable references one `std::shared_ptr<const data::GeometryAsset>` and maintains per-instance state (LOD policy, visibility, overrides, caches).
- A Scene→Renderer submission façade collects visible Renderables, resolves LODs/materials, and emits compact instance records into renderer queues.
- Scene uses Traversal.h utilities for fast pre-order enumeration with filters and visitors.

---

## 3) Component Contract

Inputs

- Geometry: `std::shared_ptr<const data::GeometryAsset>` (full asset, not a single Mesh).
- Materials: defaults are owned by SubMesh; Renderable exposes optional per-submesh overrides.
- LOD policy: Fixed, Distance-based, or Screen-space error (SSE) with hysteresis.
- Flags: visible/static/shadows via SceneFlags.
- Transform: world matrix from TransformComponent.

Outputs (to Renderer)

- For the selected LOD: a vector of SubmeshInstance PODs containing:
  - geometry_view_handle (bindless index to vertex/index views or geometry view table)
  - material_handle (bindless handle to material constants/textures)
  - transform_handle (index of world matrix in the transform table)
  - draw_range (index/vertex offsets and counts extracted from MeshView)
  - render_domain (opaque/masked/transparent/shadow)
  - flags (casts/receives shadows, visibility, static)
  - bounds_world (sphere for late-stage culling; optional AABB when enabled)

Materials fallback semantics

- If SubMesh::Material() exists and no override is provided → use submesh material.
- If override is provided → use override.
- If neither exists → use engine debug/default material (from Data or Renderer debug path).

---

## 4) Data Mapping Strategy

- Attach the full GeometryAsset to Renderable.
- GeometryAsset → LODs → Meshes → SubMeshes.
- Each submesh contributes one draw item.
- Cache local-space bounds per LOD/submesh; compute aggregated world bounds from the selected LOD on transform/LOD changes.

---

## 5) Bounds & Culling

Source of truth

- Per-submesh local AABB from SubMesh::BoundingBoxMin/Max.
- Per-mesh (LOD) aggregated local bounds from Mesh::BoundingBoxMin/Max and Mesh::BoundingSphere.

Caches in Renderable

- Per-LOD cache:
  - `std::vector<Bounds>` submesh_local_bounds (indexed by submesh id)
  - Bounds mesh_local_bounds (aggregated for the LOD)
- Aggregated world bounds: a world-space sphere used by Scene/Renderer culling; recomputed on transform/LOD updates.

Transform semantics and non-uniform scale

- World sphere: transform center by world matrix, scale radius by max(|sx|,|sy|,|sz|) — conservative and fast.
- Per-submesh world AABB (optional): transform eight local AABB corners; recompute world min/max; computed on-demand for tighter culling.

Dirty rules

- Recompute world sphere when either the node transform changes or the selected LOD changes.
- Recompute local-space bounds only on SetGeometry (assets are immutable after construction).

---

## 6) LOD Selection

Policies

- Fixed(index): deterministic authoring/debug.
- Distance-based: distance thresholds using world-sphere radius for scale normalization.
- Screen-space error (SSE): thresholds from projected size or error metric.

Hysteresis

- Use separate enter/exit thresholds at each boundary to avoid oscillation at edges.

Evaluation

- Evaluate after transform/bounds updates and camera movement; cache current LOD to avoid churn.

Practical note (current behavior)

- `GetActiveMesh()` returns empty when:
  - No GeometryAsset is set or it has zero LODs, or
  - Policy is Distance/SSE and no evaluation has yet set the current LOD for the frame. Today, this evaluation happens during renderer scene extraction per-view, which computes normalized distance or SSE and calls `Renderable::SelectActiveMesh(...)` accordingly.
- For Fixed policy, the requested LOD index is clamped to the available range.


## 7) Submesh Controls

- Per-submesh visibility bitset.
- Per-submesh material overrides: nullptr means “use SubMesh material or fallback”.
- Optional per-submesh culling using submesh AABB (off by default; enable for very large modular assets).

---

## 8) Renderer Bridge and Stable IDs

Interop approach

- Scene produces PODs and calls a renderer-provided IFrameSubmissionBuilder interface; Scene does not own bindless handle generation or PSO keys.
- Bindless indices for geometry/materials and transform table handles are resolved by the builder.

Stable instance IDs

- Compose from (Scene node stable id, selected LOD, submesh index). This yields stable identities across frames and simplifies incremental diffs.

Submission flow

1) Renderer culls using Renderable::GetBoundsWorld() and camera state to produce candidate node ids.
2) Renderer constructs a concrete builder and calls Scene::EnumerateRenderablesForSubmission(builder).
3) For each visible Renderable, Scene resolves logical details (LOD, visibility, materials, draw ranges, bounds) and calls builder Resolve* to get handles, then PushInstance.
4) Renderer finalizes snapshot/batches and generates draw commands.

Why snapshot/finalize

- Atomic, consistent view; efficient merging/sorting by domain/PSO; place for validation and fallback mapping; supports parallel traversal.

---

## 9) Integration with Existing Scene and Migration

Current state

- Scene now exposes a typed Renderable facade via `SceneNode::GetRenderable()` for geometry/LOD/submesh/bounds operations. The previous convenience helpers on `SceneNode` have been removed in favor of this facade. Callers should migrate to:

  ```cpp
  auto r = node.GetRenderable();
  r.SetGeometry(geometry);
  auto active = r.GetActiveMesh();
  ```


Migration plan

- Phase 1: Introduce Renderable alongside MeshData (no breaking changes). Keep SceneNode mesh API and tests intact. (complete historically)
- Phase 2: Introduce a typed facade via `GetRenderable()` and migrate call sites; remove legacy SceneNode geometry helpers. (completed in this iteration; breaking)
- Phase 3: Optional: rewire legacy Mesh attachment to a 1‑LOD `GeometryAsset` shim if needed for compatibility (deferred).

Update hook strategy

- Option A (recommended): Add virtual void Component::OnWorldTransformUpdated(const glm::mat4&) with default no-op; call it at the end of SceneNodeImpl::UpdateTransforms. Renderable recomputes world sphere here.
- Option B (fallback): Compute Renderable’s world bounds lazily during RenderSync. Simpler wiring but out-of-sync bounds for other consumers between frames. Acceptable for initial rollout if needed.

---

## 10) Extraction Phases and Phase 2 Design

This section defines a two-phase plan for scene extraction that respects the full geometry hierarchy (GeometryAsset → Mesh/LOD → SubMesh → MeshView) and preserves buffer offsets and index types without lossy merging.

### Phase 1 — Bridge (non-breaking)

- Keep emitting one node-level item (current renderer uses `data::Mesh`).
- Honor per-submesh visibility and material overrides during extraction:
  - Evaluate active LOD per node.
  - Iterate submeshes of the active `Mesh` and skip invisible ones.
  - Resolve effective material per submesh (override → submesh → default).
  - Aggregate world-space AABBs of visible submeshes for tighter node culling.
  - Choose the material from the first visible submesh for the node’s item; log a debug note when visible submeshes have mixed materials.

Known limitations (accepted for this phase)

- Single material per node even if multiple are visible.
- Single draw proxy per node (mesh-level), not per view.

### Phase 2 — Final (view-level granularity)

Emit one item per `MeshView` (the smallest draw slice), preserving exact draw offsets and index types. This eliminates the need to merge discontiguous ranges and aligns with GPU draw primitives.

PODs (engine-common)

- DrawSlice
  - first_vertex: uint32
  - vertex_count: uint32
  - first_index: uint32 (0 for non-indexed)
  - index_count: uint32 (0 for non-indexed; implies draw arrays)
  - index_type: enum { None, UInt16, UInt32 }
  - base_vertex: int32 (duplicate of first_vertex for APIs using base-vertex)

- MeshViewRef
  - mesh: `std::shared_ptr<const data::Mesh>`
  - slice: DrawSlice

- SubmeshViewItem
  - geometry: MeshViewRef
  - material: `std::shared_ptr<const data::MaterialAsset>`
  - world_transform (or builder-resolved transform_handle)
  - lod_index: uint32
  - submesh_index: uint32
  - view_index: uint32
  - stable_instance_id: 64-bit = (node_id << 24) | (lod << 16) | (submesh << 8) | view
  - render_domain: derived from material domain (opaque/masked/transparent/..)
  - flags: casts/receives shadows, static
  - bounds_world: sphere; optional per-submesh AABB

Extraction algorithm (per node)

1) Evaluate LOD via Renderable policy/hysteresis.
2) For each submesh s in mesh(lod):
   - Skip if not visible per Renderable.
   - Resolve material (override → submesh → default).
   - Acquire bounds: world sphere from node; per-submesh world AABB via Renderable cache when enabled.
   - For each MeshView v in submesh s:
     - Build DrawSlice from view descriptor exactly.
     - Emit SubmeshViewItem with indices (lod, s, v) and material.

Renderer builder

- Consumes SubmeshViewItem, resolves bindless handles for geometry/materials, and emits one draw per item.
- Sorting/partitioning by PSO/material/domain is done on the item list.

Edge considerations

- Non-indexed: index_count==0, index_type=None → draw arrays.
- Mixed index types: carried per item; backend widens as needed.
- Multiple views per submesh: supported as multiple items with stable IDs.
- Non-uniform scale: sphere radius uses max-axis scale; AABB from 8-corner transform per submesh.

---

## 11) Edge Cases and Rules

- Non-indexed meshes (vertex-only): draw ranges must accommodate index_count==0; handle via MeshView utilities.
- Mixed 16/32-bit indices: submission builder must widen/encode correctly; Scene only passes logical ranges.
- Non-uniform scale: use max-scale for world sphere; only compute per-submesh AABB on demand.
- Empty geometry or 0 LODs: not renderable; empty bounds; ignored by submission.
- LOD hysteresis: apply enter/exit separately and cache the last LOD; clamp LOD within [0, lod_count-1].

---

## 12) Testing Strategy

Unit tests (Scene/Test)

- Renderable attach/detach/clone.
- LOD selection correctness + hysteresis with camera movement.
- Submesh visibility toggles and overrides (including fallback to debug material).
- Bounds propagation under uniform and non-uniform scale (sphere near-equality; AABB exact via corner-transform checks).
- Geometry swap preserves overrides/visibility up to new submesh count (others cleared) and recomputes caches.
- Vertex-only/mixed-index meshes produce correct draw ranges.

Examples

- Extend a sample to attach a Renderable with a procedural GeometryAsset and verify a basic forward pass path.

---

## 13) Complete Task Checklist (Prioritized)

Scene – Component and Hooks

- [x] Define Renderable component interface and data members.
- [x] Implement SetGeometry and per-LOD/per-submesh local-bounds cache.
- [x] Implement aggregated world-sphere bounds and optional per-submesh AABB on-demand.
- [x] Implement LOD policies (fixed, distance, SSE) with hysteresis.
- [x] Implement submesh visibility bitset and material overrides storage + resolution.
- [x] Wire transform hook for Renderable: call `Renderable::OnWorldTransformUpdated(world)` from `SceneNodeImpl::UpdateTransforms`.
- [x] Add Scene helpers to get Renderable (typed facade consistent with Camera): `SceneNode::GetRenderable()`.

Scene – Render Sync and Enumeration

- Phase 1 (bridge)
  - [ ] Update extraction to honor submesh visibility and material overrides while emitting one mesh-level item per node:
    - [ ] Aggregate per-submesh world AABBs into node AABB for culling.
    - [ ] Pick first visible submesh’s material; warn on mixed materials.
  - [ ] Verify per-view LOD evaluation remains intact.

- Phase 2 (final)
  - [ ] Define engine-common PODs: DrawSlice, MeshViewRef, SubmeshViewItem, RendererContext, IFrameSubmissionBuilder (view-level).
  - [ ] Implement Scene::EnumerateRenderablesForSubmission(builder) to emit SubmeshViewItem per MeshView honoring visibility/overrides.
  - [ ] Compute stable instance IDs = (node id, LOD, submesh, view).
  - [ ] Partition by render domain and material for batching.

Data interop

- [x] Helpers to fetch local bounds per LOD/submesh from GeometryAsset → Mesh → SubMesh (used to build per-LOD local bounds cache).
- Phase 1
  - [ ] Expose/propagate material domain when needed for logging/partitioning.
- Phase 2
  - [ ] Map submesh views to DrawSlice preserving exact offsets/counts and index type.
  - [ ] Ensure MeshView exposes needed accessors (first_vertex/index, counts, index_type); add if missing.

Renderer interop

- Phase 1
  - [ ] No builder changes (mesh-level item remains).
- Phase 2
  - [ ] Implement builder Resolve* for buffer slices/materials; produce bindless indices and transform handles.
  - [ ] Implement PushInstance(SubmeshViewItem) and snapshot finalize at view granularity (sorting/validation).
  - [ ] Ensure root signature/bindless usage matches Renderer conventions.

Migration and compatibility

- [x] Introduce Renderable without breaking tests; plan deprecation of MeshData later.
- [x] Expose `SceneNode::Renderable` facade and migrate tests; remove legacy SceneNode geometry helpers (breaking in this branch).
- [ ] Optional: Implement 1-LOD shim to adapt AttachMesh → Renderable (deferred).

Tests and examples

- Phase 1
  - [ ] Visibility toggles affect culling via aggregated AABB.
  - [ ] Overrides affect selected material in extraction.
  - [ ] LOD + hysteresis preserved; examples continue to render.
- Phase 2
  - [ ] Per-view item emission; stable IDs; mixed index types; non-indexed views; multiple views/submesh; overrides and visibility.
  - [ ] Example: multi-submesh, multi-view mesh with per-submesh materials.

Documentation

- [ ] Update Scene docs and README; cross-link from design.md to this spec.
- [ ] Add a short “Fallbacks and Invariants” subsection summarizing defaults and error handling.

---

## 14) Acceptance Criteria

- A Renderable can be attached to any SceneNode with a transform.
- Given a GeometryAsset, the component selects LODs correctly and emits per-submesh instance PODs with correct ranges and material resolution.
- Per-submesh visibility and overrides work and are reflected in submissions.
- Bounds-based culling uses the world sphere; optional per-submesh AABBs yield tighter culling when enabled.
- Unit tests cover LOD, overrides, bounds, flags, vertex-only/mixed indices, and geometry swap behavior.

## Risks and Mitigations

- Risk: Over-coupling Scene and Renderer. Mitigation: define small POD interop structs and a façade.
- Risk: LOD popping. Mitigation: hysteresis and optional temporal smoothing.
- Risk: Performance regressions from per-frame rebuilds. Mitigation: dirty tracking and stable instance IDs.
