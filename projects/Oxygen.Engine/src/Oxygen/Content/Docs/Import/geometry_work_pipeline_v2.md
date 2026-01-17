# Geometry Pipeline (v2)

**Status:** Complete Design (Phase 5)
**Date:** 2026-01-15
**Parent:** [async_import_pipeline_v2.md](async_import_pipeline_v2.md)

---

## Overview

This document specifies the **GeometryPipeline** used by async imports to cook
mesh geometry within **a single import job**. The pipeline is compute-only and
must produce **feature-complete** geometry assets that fix legacy omissions
while staying compatible with the PAK format and async architecture.

Core properties:

- **Compute-only**: builds vertex/index buffers, submesh tables, and geometry
  descriptor bytes.
- **No I/O, no indices**: returns cooked payloads; the job commits via
  `BufferEmitter` and `AssetEmitter`.
- **Job-scoped**: created per job and started in the job’s child nursery.
- **ThreadPool offload**: tangent generation and any `content_hash`
  computation run on `co::ThreadPool`.
- **Strict failure policy**: geometry cooking never falls back to placeholders;
  failures surface as explicit diagnostics.
- **Planner‑gated**: the planner submits work only when dependencies are ready
  and the full descriptor can be finalized.

---

## Alignment With Current Architecture

The pipeline-agnostic concurrency and lifetime model is defined in the parent
document [design/async_import_pipeline_v2.md](design/async_import_pipeline_v2.md)
under **Concurrency, Ownership, and Lifetime (Definitive)**.

### Pipeline vs Emitter Responsibilities

- **GeometryPipeline**: produces geometry descriptor bytes and buffer work
  items (compute-only).
- **BufferEmitter**: assigns stable buffer indices and writes `buffers.data` /
  `buffers.table`.
- **AssetEmitter**: writes `.ogeo` geometry descriptor files.
  `content_hash` must be computed on the ThreadPool after buffer indices are
  assigned.

---

## Data Model

### WorkItem

Geometry cooking operates on mesh data already in memory. Source acquisition is
outside the pipeline.

```cpp
struct MeshStreamView {
  std::span<const glm::vec3> positions;
  std::span<const glm::vec3> normals;     // optional
  std::span<const glm::vec2> texcoords;   // optional
  std::span<const glm::vec3> tangents;    // optional
  std::span<const glm::vec3> bitangents;  // optional
  std::span<const glm::vec4> colors;      // optional
  std::span<const glm::uvec4> joint_indices; // optional (skinned)
  std::span<const glm::vec4> joint_weights;  // optional (skinned)
};

struct TriangleRange {
  uint32_t material_slot = 0;  // Scene material index for this range
  uint32_t first_index = 0;    // Offset into indices
  uint32_t index_count = 0;    // Multiple of 3
};

struct TriangulatedMesh {
  data::MeshType mesh_type = data::MeshType::kStandard;
  MeshStreamView streams;
  std::span<const uint32_t> indices;      // Triangle list (u32)
  std::span<const TriangleRange> ranges;  // Material ranges
};

struct UfbxMeshView {
  const ufbx_scene* scene = nullptr;
  const ufbx_mesh* mesh = nullptr;
};

using MeshSource = std::variant<TriangulatedMesh, UfbxMeshView>;

struct MeshLod {
  std::string lod_name;
  MeshSource source;
  std::shared_ptr<const void> source_owner; // Keeps mesh data alive
};

struct Bounds3 {
  std::array<float, 3> min;
  std::array<float, 3> max;
};

struct WorkItem {
  std::string source_id;        // Diagnostic id
  std::string mesh_name;        // Authored or synthesized name
  std::string storage_mesh_name; // Namespaced name (scene/mesh)
  const void* source_key{};     // Opaque mesh identity (e.g., ufbx_mesh*)

  std::vector<MeshLod> lods;    // Must contain at least one LOD

  std::vector<data::AssetKey> material_keys; // Aligned with scene materials
  data::AssetKey default_material_key;
  bool want_textures = false;  // Used for UV diagnostics
  bool has_material_textures = false; // Used for UV diagnostics

  ImportRequest request;       // Options, naming, asset key policy
  std::stop_token stop_token;
};
```

Notes:

- `lods` must contain at least one LOD; `lods[0]` is the highest detail.
- `UfbxMeshView` is allowed; it must contain the `ufbx_scene` and `ufbx_mesh`
  pointers and an owner to keep them alive. Importers should prefer
  format-agnostic `TriangulatedMesh` where possible.
- `TriangulatedMesh` is the format-agnostic path for glTF or pre-processed
  sources; it must already be triangle lists.
- `storage_mesh_name` is used for virtual paths and descriptor relpaths.
- `TriangulatedMesh.mesh_type` selects `MeshType` and drives serialization
  (standard vs skinned).
- For skinned meshes, `joint_indices` and `joint_weights` must be present and
  aligned with `positions`.
- The FBX path (`UfbxMeshView`) is supported for standard meshes only; skinned
  FBX meshes must be converted to `TriangulatedMesh` with joint streams.
- `material_keys` may be empty; the pipeline uses `default_material_key` when a
  material slot is missing.
- `has_material_textures` should be precomputed by the importer for
  format-agnostic meshes; for FBX it can be derived from `ufbx_material`.
- `request.options.normal_policy` and `request.options.tangent_policy` are fully
  honored.
- **Naming is the importer’s responsibility.** Use
  `util::BuildMeshName`, `util::DisambiguateMeshName`, and
  `util::NamespaceImportedAssetName` (see
  [src/Oxygen/Content/Import/util/ImportNaming.h](../src/Oxygen/Content/Import/util/ImportNaming.h))
  before submitting the work item. The pipeline must treat names as resolved and
  must not rename.

### WorkResult

```cpp
struct CookedMeshPayload {
  CookedBufferPayload vertex_buffer;
  CookedBufferPayload index_buffer;
  std::vector<CookedBufferPayload> auxiliary_buffers; // skinning, morph, etc.
  Bounds3 bounds;
};

struct CookedGeometryPayload {
  data::AssetKey geometry_key;
  std::string virtual_path;
  std::string descriptor_relpath;
  std::vector<std::byte> descriptor_bytes;  // .ogeo payload

  std::vector<CookedMeshPayload> lods; // LOD0..LOD(n-1)
};

struct WorkResult {
  std::string source_id;
  const void* source_key{};
  std::optional<CookedGeometryPayload> cooked;
  std::vector<ImportDiagnostic> diagnostics;
  bool success = false;
};
```

Notes:

- The planner must ensure buffer indices are assigned before final descriptor
  hashing. If buffer emission occurs first, the planner may schedule a
  descriptor‑finalization step on the ThreadPool that fills indices and computes
  `content_hash` over the complete bytes.

---

## Public API (Pattern)

```cpp
class GeometryPipeline final {
public:
  struct Config {
    size_t queue_capacity = 32;
    uint32_t worker_count = 2;
    bool with_content_hashing = true; // Enabled for integrity + dedupe
  };

  explicit GeometryPipeline(co::ThreadPool& thread_pool, Config cfg = {});

  void Start(co::Nursery& nursery);

  [[nodiscard]] auto Submit(WorkItem item) -> co::Co<>;
  [[nodiscard]] auto TrySubmit(WorkItem item) -> bool;
  [[nodiscard]] auto Collect() -> co::Co<WorkResult>;

  void Close();

  [[nodiscard]] auto HasPending() const noexcept -> bool;
  [[nodiscard]] auto PendingCount() const noexcept -> size_t;
  [[nodiscard]] auto GetProgress() const noexcept -> PipelineProgress;
};
```

---

## Worker Behavior

Workers run as coroutines on the import thread and drain the bounded input queue.

For each work item:

1) Check cancellation; if cancelled, return `success=false`.
2) Assume names are already resolved by the importer; no renaming occurs here.
3) Validate each LOD mesh:
   - If positions are missing → error `mesh.missing_positions`.
   - If faces/indices are empty → error `mesh.missing_buffers`.
4) Build vertices, bounds, and attribute masks per LOD:
   - Expand to one vertex per index.
   - Apply coordinate conversion via `coord::ApplySwapYZ*`.
   - Apply `normal_policy`:
     - `kNone`: emit defaults and clear `kGeomAttr_Normal` in the attribute mask.
     - `kPreserveIfPresent`: preserve when present, otherwise defaults + clear.
     - `kGenerateMissing`: generate when missing, otherwise preserve.
     - `kAlwaysRecalculate`: always recompute from positions/indices.
   - Apply `tangent_policy`:
     - If prerequisites (positions, UVs, normals) are missing, emit
       `mesh.missing_tangent_prereq` and clear `kGeomAttr_TangentBitangent`.
     - Otherwise preserve, generate, or recompute per policy.
5) Warn on missing UVs when `want_textures == true` and
   `has_material_textures == true` (`mesh.missing_uvs` warning).
6) Build submesh buckets by material slot:
   - Triangulate faces (FBX uses `ufbx_triangulate_face`).
   - Sort buckets by `scene_material_index`.
7) Fix invalid tangents/bitangents to ensure orthonormal basis when tangents are
   emitted.
8) Build `SubMeshDesc` and `MeshViewDesc` per LOD:
   - Submesh name: `"mat_<scene_material_index>"`.
   - MeshView covers the submesh’s index and vertex ranges (tight bounds).
9) Create buffer payloads per LOD:
   - Vertex buffer: `data::Vertex` array, stride `sizeof(Vertex)`,
     usage flags = `VertexBuffer | Static`, format = `Format::kUnknown`.
   - Index buffer: `uint32_t` array, usage flags = `IndexBuffer | Static`,
     format = `Format::kR32UInt`.
   - Skinned meshes emit additional joint index/weight buffers.
   - `content_hash` is **not** computed here. Hashing happens on the
     ThreadPool in `BufferPipeline` once buffer dependencies are ready.
10) Compute geometry identity:
    - `virtual_path = request.loose_cooked_layout.GeometryVirtualPath(
       storage_mesh_name)`
    - `descriptor_relpath = request.loose_cooked_layout.GeometryDescriptorRelPath(
       storage_mesh_name)`
    - `geometry_key` based on `AssetKeyPolicy`
11) Build geometry descriptor bytes:
    - `GeometryAssetDesc` + `MeshDesc[ lod_count ]` + submeshes + views
    - `lod_count = lods.size()`
    - `mesh_type` per LOD (`kStandard`, `kSkinned`, or `kProcedural`)
    - `header.version = kGeometryAssetVersion`
    - `header.variant_flags` uses **union-of-LOD attributes** (any attribute
      emitted by any LOD sets the bit). Missing attributes on a specific LOD
      must be defaulted by loaders.
12) Defer `header.content_hash` until all dependency indices are known.
  Hashing must run on the ThreadPool and must use the **complete**
  descriptor bytes (no skip ranges).
13) Return `CookedGeometryPayload` with metadata and buffer payloads.

All errors must be converted to `ImportDiagnostic`; no exceptions cross async
boundaries.

---

## Pipeline Stages (Complete)

1) **LOD validation**
   - Validate each LOD for positions/indices.
   - Record diagnostics for missing streams.

2) **Vertex expansion + coordinate conversion**
   - Build one vertex per index (no vertex dedupe).
   - Apply `CoordinateConversionPolicy` swap-YZ to positions, normals, tangents.

3) **Attribute policy application**
   - Enforce `normal_policy` and `tangent_policy`.
   - Generate normals/tangents as required, or clear attribute bits when absent.
   - Emit diagnostics when tangent prerequisites are missing.

4) **Material bucketing**
   - Face material slots map to `material_keys` (or default).
   - Triangulate faces, group by material, sort by slot index.

5) **Submesh + view layout**
   - `SubMeshDesc` per material bucket; `MeshViewDesc` per submesh.
   - `MeshViewDesc` ranges are tight to the submesh.

6) **Buffer payloads**
   - Vertex buffer uses `data::Vertex` layout.
   - Index buffer uses `uint32_t`.
   - Skinned meshes emit joint index/weight buffers.
   - Content hashes computed for all buffers.

7) **Geometry descriptor serialization**
   - Packed alignment = 1 (no padding).
   - `GeometryAssetDesc` + `MeshDesc[ lod_count ]` + submesh/view tables.
   - Optional mesh-type blob follows each `MeshDesc` as required.

---

## Cooked Output Contract (PAK vNext)

This pipeline targets a **new PAK namespace** with a new
`kGeometryAssetVersion`. Backward compatibility is not required for geometry.
The format **must** explicitly support a skinned-mesh blob and any additional
geometry metadata needed by this pipeline.

### Geometry Descriptor (`.ogeo`)

Packed binary blob:

```text
GeometryAssetDesc (256 bytes)
MeshDesc[ lod_count ]
  [optional mesh-type blob]
  SubMeshDesc[ submesh_count ]
  MeshViewDesc[ mesh_view_count ]
```

Requirements:

- `GeometryAssetDesc.header.asset_type = AssetType::kGeometry`
- `GeometryAssetDesc.header.version = kGeometryAssetVersion`
- `GeometryAssetDesc.header.content_hash` computed on the ThreadPool after all
  buffer indices are known, over the **complete** descriptor bytes
- `lod_count = lods.size()`
- `SubMeshDesc::material_asset_key` populated for each submesh

### Geometry Attribute Mask

`GeometryAssetDesc.header.variant_flags` encodes a per-geometry attribute mask:

```text
bit 0: kGeomAttr_Normal
bit 1: kGeomAttr_Tangent
bit 2: kGeomAttr_Bitangent
bit 3: kGeomAttr_Texcoord0
bit 4: kGeomAttr_Color0
bit 5: kGeomAttr_JointWeights
bit 6: kGeomAttr_JointIndices
```

The pipeline must clear bits when attributes are not emitted by policy. Loaders
and runtime must treat missing attributes as defaults.

@note The bit layout is a pipeline contract and must be mirrored by loaders and
tools that interpret `variant_flags`. The mask is **union-of-LOD attributes**.

### Mesh-Type Blobs

If `mesh_type == MeshType::kStandard`, no blob is emitted.

If `mesh_type == MeshType::kProcedural`, emit the procedural params blob
immediately after `MeshDesc` (size = `MeshDesc.info.procedural.params_size`).

If `mesh_type == MeshType::kSkinned`, emit a skinned blob defined by the new
format version (example structure below):

```text
struct SkinnedMeshInfo {
  ResourceIndexT vertex_buffer;
  ResourceIndexT index_buffer;
  ResourceIndexT joint_index_buffer;
  ResourceIndexT joint_weight_buffer;
  float bounding_box_min[3];
  float bounding_box_max[3];
};
```

The pipeline must set `mesh_view_count`, `submesh_count`, and bounds as usual.

### Buffers (`buffers.data` / `buffers.table`)

Buffers are emitted via `BufferEmitter` using `CookedBufferPayload`:

- Vertex buffer alignment = `sizeof(Vertex)`
- Index buffer alignment = `alignof(uint32_t)`
- Skinned meshes emit joint index/weight buffers (alignment = 16 bytes)
- `content_hash` must be populated for every buffer payload by
  `BufferPipeline` on the ThreadPool

---

## Feature Completion Requirements

- **Attribute policies**: `normal_policy` and `tangent_policy` are fully honored,
  with diagnostics when prerequisites are missing.
- **LOD support**: all authored LODs are serialized in order.
- **Mesh types**: `kStandard`, `kSkinned`, and `kProcedural` are supported. Mesh
  types without a defined blob must emit a diagnostic and be skipped or coerced
  to `kStandard` per job policy.
- **Header metadata**: `header.version` and `header.content_hash` are
  populated (hash computed on ThreadPool after dependencies resolve).
- **Attribute mask**: missing attributes are reflected via
  `header.variant_flags` (union-of-LOD attributes).
- **Failure policy**: no geometry fallback is allowed. Any failure produces
  explicit diagnostics and `success=false`.

### Importer/Orchestrator Responsibilities

- Mesh name resolution and disambiguation
  (`BuildMeshName`, `DisambiguateMeshName`, `NamespaceImportedAssetName`).
- Effective material key resolution
  (`BuildEffectiveMaterialKeys`, default material fallback).
- Import content flags (geometry/scene/materials).
- LOD construction (ordering and naming), plus mesh-type selection.
- Diagnostics enrichment and policy decisions (e.g., abort vs continue).

---

## Separation of Concerns

- **GeometryPipeline**: compute-only; no I/O.
- **BufferEmitter**: dedupe + async data writes.
- **AssetEmitter**: `.ogeo` write.
- **ScenePipeline**: consumes `ImportedGeometry` mapping to build `.oscene`.

---

## Cancellation Semantics

Pipeline must honor `WorkItem.stop_token`. Cancellation returns
`success=false` without partial outputs.

---

## Backpressure and Memory Safety

Use bounded queues. The job must `co_await Submit()` before reading the next
mesh to avoid unbounded vertex/index buffers in memory.

---

## Progress Reporting

Pipeline tracks submitted/completed/failed counts and exposes
`PipelineProgress`. The job maps this to `ImportProgress`.

---

## Robustness Rules (Do Not Violate)

1) Pipeline never writes output files.
2) Pipeline never calls emitters.
3) Geometry descriptors must be packed with alignment = 1.
4) Buffer content hashes must be computed for data integrity.
5) All errors cross boundaries as `ImportDiagnostic`.
6) Coordinate conversion is applied **once** and only if required by the
  importer’s declared source space.

---

## Coordinate Conversion Policy (Definitive)

The engine space is **right-handed, Z-up, forward = -Y** as defined in
[src/Oxygen/Core/Constants.h](../src/Oxygen/Core/Constants.h). Coordinate
conversion must be **one-shot** and applied **only if** the importer declares
that the source space differs from engine space.

Rules:

1) The importer must provide the **source space** metadata for each mesh.
2) The pipeline applies at most one conversion, producing final engine space.
3) No additional “unmapping” or multiple remappings are allowed.
4) If the importer declares the source space already matches engine space,
   conversion is a no-op.

This conversion policy is **pluggable** by design. The pipeline exposes a
`CoordinateConversionPolicy` interface (or equivalent) that the importer
supplies per job. The policy must define deterministic transforms for
positions, normals, tangents, and bitangents, and must re-normalize directions
after conversion.

Diagnostics:

- If source-space metadata is missing or inconsistent, emit a **blocking error**
  diagnostic and fail the work item.

---

## Limits and Validation (Industry-Style Defaults)

The pipeline must validate inputs and emit diagnostics for out-of-range or
invalid data. Use conservative defaults aligned with common engine practices:

- `lod_count`: 1..8 (error if 0 or > 8).
- Per-LOD `index_count`: must be > 0 and a multiple of 3.
- Per-LOD `vertex_count` and `index_count`: must fit in `uint32_t`.
- Per-LOD `submesh_count` and `mesh_view_count`: must fit in `uint32_t` and be
  non-zero if indices are present.
- Buffer payload sizes must be ≤ `kDataBlobMaxSize` and respect alignment
  (`sizeof(Vertex)`, `alignof(uint32_t)`, and 16-byte alignment for skinning
  buffers).
- Names must fit `kMaxNameSize` (truncate with diagnostic if oversized).

---

## See Also

- [texture_work_pipeline_v2.md](texture_work_pipeline_v2.md)
- `src/Oxygen/Content/Import/emit/GeometryEmitter.*` (legacy behavior)
- `src/Oxygen/Data/PakFormat.h` (GeometryAssetDesc/MeshDesc/SubMeshDesc)
