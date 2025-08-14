# Renderable Component – Design & Integration Spec

This document defines the Renderable component for the Oxygen Scene system and
describes how it integrates with Data and Renderer modules, migration from the
current MeshData usage, update hooks, culling/LOD behavior, and submission.

---

## 1) Goals & Scope

Goals

- Attach renderable geometry to a SceneNode using a first-class Renderable
  component.
- Map Data::GeometryAsset (LODs → Meshes → SubMeshes) and MaterialAsset to scene
  nodes, with optional per-submesh overrides.
- Produce efficient, bindless-first per-frame submissions to the Renderer.
- Support LOD selection policies, per-submesh visibility, overrides, and robust
  bounds/culling.

Scope

- Scene: new Renderable component, per-node state, dirty tracking, and bounds.
- Data interop: consume GeometryAsset/MaterialAsset, resolve submesh views and
  defaults.
- Renderer bridge: lightweight façade for instance records with bindless
  indices, stable instance IDs, and PSO-friendly keys.
- Tests/examples: LOD, overrides, flags, bounds, and submission.

---

## 2) Architecture Overview

- SceneNodeImpl gains an attachable Renderable component. Dependencies:
  TransformComponent and NodeData (flags).
- Renderable references one `std::shared_ptr<const data::GeometryAsset>` and
  maintains per-instance state (LOD policy, visibility, overrides, caches).
- A Scene→Renderer submission façade collects visible Renderables, resolves
  LODs/materials, and emits compact instance records into renderer queues.
- Scene uses Traversal.h utilities for fast pre-order enumeration with filters
  and visitors.

---

## 3) Component Contract

Inputs

- Geometry: `std::shared_ptr<const data::GeometryAsset>` (full asset, not a
  single Mesh).
- Materials: defaults are owned by SubMesh; Renderable exposes optional
  per-submesh overrides.
- LOD policy: Fixed, Distance-based, or Screen-space error (SSE) with
  hysteresis.
- Flags: visible/static/shadows via SceneFlags.
- Transform: world matrix from TransformComponent.

Outputs (to Renderer)

- For the selected LOD: a vector of SubmeshInstance PODs containing:
  - geometry_view_handle (bindless index to vertex/index views or geometry view
    table)
  - material_handle (bindless handle to material constants/textures)
  - transform_handle (index of world matrix in the transform table)
  - draw_range (index/vertex offsets and counts extracted from MeshView)
  - render_domain (opaque/masked/transparent/shadow)
  - flags (casts/receives shadows, visibility, static)
  - bounds_world (sphere for late-stage culling; optional AABB when enabled)

Materials fallback semantics

- If SubMesh::Material() exists and no override is provided → use submesh
  material.
- If override is provided → use override.
- If neither exists → use engine debug/default material (from Data or Renderer
  debug path).

---

## 4) Data Mapping Strategy

- Attach the full GeometryAsset to Renderable.
- GeometryAsset → LODs → Meshes → SubMeshes.
- Each submesh contributes one draw item.
- Cache local-space bounds per LOD/submesh; compute aggregated world bounds from
  the selected LOD on transform/LOD changes.

---

## 5) Bounds & Culling

Source of truth

- Per-submesh local AABB from SubMesh::BoundingBoxMin/Max.
- Per-mesh (LOD) aggregated local bounds from Mesh::BoundingBoxMin/Max and
  Mesh::BoundingSphere.

Caches in Renderable

- Per-LOD cache:
  - `std::vector<Bounds>` submesh_local_bounds (indexed by submesh id)
  - Bounds mesh_local_bounds (aggregated for the LOD)
  - bool bounds_valid
- Aggregated world bounds: a world-space sphere used by Scene/Renderer culling;
  recomputed on transform/LOD updates.

Transform semantics & non-uniform scale

- World sphere: transform center by world matrix, scale radius by
  max(|sx|,|sy|,|sz|) — conservative and fast.
- Per-submesh world AABB (optional): transform eight local AABB corners;
  recompute world min/max; computed on-demand for tighter culling.

Dirty rules

- Recompute world sphere when either the node transform changes or the selected
  LOD changes.
- Recompute local-space bounds only on SetGeometry (assets are immutable after
  construction).

---

## 6) LOD Selection

Policies

- Fixed(index): deterministic authoring/debug.
- Distance-based: distance thresholds using world-sphere radius for scale
  normalization.
- Screen-space error (SSE): thresholds from projected size or error metric.

Hysteresis

- Use separate enter/exit thresholds at each boundary to avoid oscillation at
  edges.

Evaluation

- Evaluate after transform/bounds updates and camera movement; cache current LOD
  to avoid churn.

---

## 7) Submesh Controls

- Per-submesh visibility bitset.
- Per-submesh material overrides: nullptr means “use SubMesh material or
  fallback”.
- Optional per-submesh culling using submesh AABB (off by default; enable for
  very large modular assets).

---

## 8) Renderer Bridge & Stable IDs

Interop approach

- Scene produces PODs and calls a renderer-provided IFrameSubmissionBuilder
  interface; Scene does not own bindless handle generation or PSO keys.
- Bindless indices for geometry/materials and transform table handles are
  resolved by the builder.

Stable instance IDs

- Compose from (Scene node stable id, selected LOD, submesh index). This yields
  stable identities across frames and simplifies incremental diffs.

Submission flow

1) Renderer culls using Renderable::GetBoundsWorld() and camera state to produce
   candidate node ids.
2) Renderer constructs a concrete builder and calls
   Scene::EnumerateRenderablesForSubmission(builder).
3) For each visible Renderable, Scene resolves logical details (LOD, visibility,
   materials, draw ranges, bounds) and calls builder Resolve* to get handles,
   then PushInstance.
4) Renderer finalizes snapshot/batches and generates draw commands.

Why snapshot/finalize

- Atomic, consistent view; efficient merging/sorting by domain/PSO; place for
  validation and fallback mapping; supports parallel traversal.

---

## 9) Integration with Existing Scene & Migration

Current state

- Scene exposes a Mesh component API on SceneNode that operates on
  `std::shared_ptr<const data::Mesh>` implemented via detail::MeshData.

Migration plan

- Phase 1: Introduce Renderable alongside MeshData (no breaking changes). Keep
  SceneNode mesh API and tests intact.
- Phase 2: Add SceneNode helpers: AttachRenderable/HasRenderable/GetRenderable
  or typed getters via Composition. Prefer minimal API surface consistent with
  Camera.
- Phase 3: Optional: rewire AttachMesh to build a 1-LOD GeometryAsset shim
  internally (deferred). Eventually deprecate MeshData once Renderable adoption
  is complete.

Update hook strategy

- Option A (recommended): Add virtual void
  Component::OnWorldTransformUpdated(const glm::mat4&) with default no-op; call
  it at the end of SceneNodeImpl::UpdateTransforms. Renderable recomputes world
  sphere here.
- Option B (fallback): Compute Renderable’s world bounds lazily during
  RenderSync. Simpler wiring but out-of-sync bounds for other consumers between
  frames. Acceptable for initial rollout if needed.

---

## 10) API Sketch

```cpp
class Renderable final : public Component {
  OXYGEN_COMPONENT(Renderable)
public:
  enum class LODPolicy { kFixed, kDistance, kScreenSpaceError };

  explicit Renderable(std::shared_ptr<const data::GeometryAsset> geometry);

  void SetGeometry(std::shared_ptr<const data::GeometryAsset> geometry);
  const std::shared_ptr<const data::GeometryAsset>& GetGeometry() const noexcept;

  void SetLODFixed(size_t index);
  void SetLODPolicy(LODPolicy policy);
  void SetLODThresholds(std::vector<float> distances);
  void SetLODSSERange(std::vector<float> sse_enter, std::vector<float> sse_exit);

  void SetSubmeshVisible(size_t i, bool visible);
  bool GetSubmeshVisible(size_t i) const;

  void SetSubmeshMaterialOverride(size_t i, std::shared_ptr<const data::MaterialAsset> mat);
  const std::shared_ptr<const data::MaterialAsset>& GetSubmeshMaterial(size_t i) const; // resolves default or override

  Bounds GetBoundsWorld() const;                    // aggregated world sphere or AABB
  Bounds GetSubmeshBoundsWorld(size_t i) const;     // on-demand AABB when enabled

  // Called by Scene update phases (if Option A is implemented)
  void OnWorldTransformUpdated(const glm::mat4& world) override;

  // Export a compact submission snapshot for the current frame
  std::span<const SubmeshInstance> BuildSubmission(const RendererContext& ctx);
};
```

SetGeometry behavior (summary)

- If new pointer equals current → no-op.
- If nullptr → detach; clear caches, mark RenderSync dirty.
- Else → rebuild per-LOD local bounds (Mesh/SubMesh); clamp visibility/overrides
  vector sizes to new submesh counts; recompute world bounds for selected LOD;
  mark RenderSync dirty.

---

## 11) Edge Cases & Rules

- Non-indexed meshes (vertex-only): draw ranges must accommodate index_count==0;
  handle via MeshView utilities.
- Mixed 16/32-bit indices: submission builder must widen/encode correctly; Scene
  only passes logical ranges.
- Non-uniform scale: use max-scale for world sphere; only compute per-submesh
  AABB on demand.
- Empty geometry or 0 LODs: not renderable; empty bounds; ignored by submission.
- LOD hysteresis: apply enter/exit separately and cache the last LOD; clamp LOD
  within [0, lod_count-1].

---

## 12) Testing Strategy

Unit tests (Scene/Test)

- Renderable attach/detach/clone.
- LOD selection correctness + hysteresis with camera movement.
- Submesh visibility toggles and overrides (including fallback to debug
  material).
- Bounds propagation under uniform and non-uniform scale (sphere near-equality;
  AABB exact via corner-transform checks).
- Geometry swap preserves overrides/visibility up to new submesh count (others
  cleared) and recomputes caches.
- Vertex-only/mixed-index meshes produce correct draw ranges.

Examples

- Extend a sample to attach a Renderable with a procedural GeometryAsset and
  verify a basic forward pass path.

---

## 13) Complete Task Checklist (Prioritized)

Scene – Component & Hooks

- [ ] Define Renderable component interface and data members.
- [ ] Implement SetGeometry and per-LOD/per-submesh local-bounds cache.
- [ ] Implement aggregated world-sphere bounds and optional per-submesh AABB
  on-demand.
- [ ] Implement LOD policies (fixed, distance, SSE) with hysteresis.
- [ ] Implement submesh visibility bitset and material overrides storage +
  resolution.
- [ ] Option A: Add Component::OnWorldTransformUpdated(world) and call from
  SceneNodeImpl::UpdateTransforms.
- [ ] Option B (if chosen): Lazy world-bounds recompute during RenderSync.
- [ ] Add Scene helpers to attach/get Renderable (minimal API consistent with
  Camera).

Scene – Render Sync & Enumeration

- [ ] Define engine-common PODs: SubmeshInstance, RendererContext (no
  renderer-specific handles), and IFrameSubmissionBuilder.
- [ ] Implement Scene::EnumerateRenderablesForSubmission(builder) using
  Traversal.h with appropriate filters.
- [ ] Partition by render domain; compute stable instance IDs = (node id, LOD,
  submesh).

Data Interop

- [ ] Helpers to fetch local bounds per LOD/submesh from GeometryAsset → Mesh →
  SubMesh.
- [ ] Resolve default materials per submesh; expose material domain when needed.
- [ ] Map submesh views to draw ranges (vertex/index offsets, counts).

Renderer Interop

- [ ] Implement builder Resolve* methods to produce bindless indices and
  transform handles.
- [ ] Implement PushInstance and snapshot finalize path (sorting/validation).
- [ ] Ensure root signature/bindless table usage matches Renderer conventions.

Migration & Compatibility

- [ ] Keep MeshData and SceneNode mesh API intact initially.
- [ ] Introduce Renderable without breaking tests; plan deprecation of MeshData
  later.
- [ ] Optional: Implement 1-LOD shim to adapt AttachMesh → Renderable
  (deferred).

Tests & Examples

- [ ] Unit tests: attach/detach/clone; LOD + hysteresis; visibility/overrides;
  bounds (uniform/non-uniform); geometry swap clamp; vertex-only/mixed-indices
  ranges.
- [ ] Example: basic scene with Renderable submission path.

Documentation

- [ ] Update Scene docs and README; cross-link from design.md to this spec.
- [ ] Add a short “Fallbacks & Invariants” subsection summarizing defaults and
  error handling.

---

## 14) Acceptance Criteria

- A Renderable can be attached to any SceneNode with a transform.
- Given a GeometryAsset, the component selects LODs correctly and emits
  per-submesh instance PODs with correct ranges and material resolution.
- Per-submesh visibility and overrides work and are reflected in submissions.
- Bounds-based culling uses the world sphere; optional per-submesh AABBs yield
  tighter culling when enabled.
- Unit tests cover LOD, overrides, bounds, flags, vertex-only/mixed indices, and
  geometry swap behavior.

## Risks and Mitigations

- Risk: Over-coupling Scene and Renderer. Mitigation: define small POD interop
  structs and a façade.
- Risk: LOD popping. Mitigation: hysteresis and optional temporal smoothing.
- Risk: Performance regressions from per-frame rebuilds. Mitigation: dirty
  tracking and stable instance IDs.

## Acceptance Criteria

- A `Renderable` can be attached to any `SceneNode` with a transform.
- Given a `GeometryAsset`, the component correctly selects LODs and emits
  submesh instances with bindless handles and correct draw ranges.
- Per-submesh visibility and material overrides work and are reflected in
  renderer submissions.
- Bounds-based culling is possible using the emitted bounds (node-level
  minimum).
- Unit tests cover LOD, overrides, bounds, flags, and diffs.
