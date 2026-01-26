# Geometry Pipeline (v2)

**Status:** Complete Design (Phase 5)
**Date:** 2026-01-15
**Parent:** [async_import_pipeline_v2.md](async_import_pipeline_v2.md)

---

## Overview

This document specifies the **GeometryPipeline** used by async imports to
finalize geometry *descriptors* produced by the mesh build stage. The heavy
mesh cooking (vertex/index expansion, attribute policy application, tangent
and normal generation, and creation of buffer payloads and raw descriptor
bytes) is performed by the **MeshBuildPipeline**. The `GeometryPipeline` is a
descriptor-finalization stage that patches buffer indices, applies resolved
material keys, and computes the geometry descriptor `content_hash` once all
buffer indices are known.

Core properties:

- **Descriptor finalizer**: patches buffer indices into serialized
  `GeometryAssetDesc`/`MeshDesc` blobs, applies material key patches, and
  writes the finalized descriptor bytes.
- **No I/O**: does not write files or emit buffers – it produces finalized
  descriptor bytes; `BufferEmitter` / `AssetEmitter` still perform writes.
- **Job-scoped**: created per job and started in the job’s child nursery.
- **ThreadPool offload**: descriptor `content_hash` computation runs on
  `co::ThreadPool` when hashing is enabled.
- **Strict failure policy**: finalization failures surface as explicit
  diagnostics; no silent fallbacks are performed.
- **Planner‑gated**: the planner submits finalization work only after buffer
  indices (bindings) and resolved material keys are available.

---

## Alignment With Current Architecture

The pipeline-agnostic concurrency and lifetime model is defined in the parent
document [design/async_import_pipeline_v2.md](design/async_import_pipeline_v2.md)
under **Concurrency, Ownership, and Lifetime (Definitive)**.

### Pipeline vs Emitter Responsibilities

- **MeshBuildPipeline**: performs CPU-bound mesh cooking. It produces cooked
  buffer payloads and the *initial* geometry descriptor bytes (with
  placeholder indices) plus a table of per-submesh material patch offsets.
- **GeometryPipeline**: finalizes the geometry descriptor by patching buffer
  bindings (indices), applying resolved material keys at the recorded byte
  offsets, and computing the descriptor-level `content_hash` (on the
  ThreadPool) if enabled.
- **BufferEmitter**: assigns stable buffer indices, writes `buffers.data` and
  `buffers.table`, and makes the binding indices available for finalization.
- **AssetEmitter**: writes `.ogeo` geometry descriptor files using the
  finalized descriptor bytes produced by `GeometryPipeline`.

---

## Data Model

### WorkItem (GeometryPipeline)

GeometryPipeline receives *finalization* work items produced by the planner
once the mesh has been cooked by `MeshBuildPipeline` and buffer indices have
been assigned. A GeometryPipeline work item contains the following logical
fields:

- `source_id`: a correlation string (used for diagnostics and lookup).
- `cooked`: the `CookedGeometryPayload` produced by `MeshBuildPipeline`. This
  includes the raw descriptor bytes (`descriptor_bytes`) with recorded
  per-submesh material patch offsets and the cooked buffer payloads for each
  LOD.
- `bindings`: a vector of buffer bindings (one binding per LOD) that supplies
  the concrete resource indices to patch into the serialized `MeshDesc`
  blobs (vertex/index/joint buffers).
- `material_patches`: a compact list of `{material_key_offset, key}` entries
  indicating absolute byte offsets (from payload start) where `data::AssetKey`
  values must be overwritten with the resolved keys.
- `on_started` / `on_finished`: optional callbacks invoked when a worker
  begins/finishes processing the item.
- `stop_token`: cancellation token scanned by the pipeline; cancellation must
  produce a non-successful `WorkResult` with no partial outputs.

Notes:

- Mesh construction (vertex expansion, attribute application, tangent
  generation, auxiliary buffer creation) is the responsibility of
  `MeshBuildPipeline`. GeometryPipeline expects the descriptor bytes and
  material patch offsets to be produced by that earlier stage.
- Material patch offsets are absolute byte offsets into the descriptor payload
  and must be valid for the descriptor produced by `MeshBuildPipeline`. Any
  offset outside the descriptor bounds is treated as a finalization error.

### WorkResult

A completed GeometryPipeline work item produces a `WorkResult` that contains:

- `source_id`: echoed from the WorkItem for correlation.
- `cooked` (optional): the `CookedGeometryPayload` when finalization succeeded
  (otherwise `std::nullopt`).
- `finalized_descriptor_bytes`: the finalized `.ogeo` payload (buffer indices
  and material keys patched, and `header.content_hash` written if hashing
  succeeded and was enabled).
- `diagnostics`: any `ImportDiagnostic` entries produced during finalization.
- `telemetry`: per-item timing/telemetry (e.g., `cook_duration`).
- `success`: true on success; false if canceled or any errors occurred.

Notes:

- The planner must ensure that buffer indices are available (via `bindings`)
  and that material keys to write at the recorded offsets are provided.
- `MeshBuildPipeline` is responsible for recording the per-submesh material
  patch offsets while serializing the initial descriptor; `GeometryPipeline`
  uses those offsets to write `data::AssetKey` values directly into the
  finalized payload.

---

## Public API (Pattern)

The implemented `GeometryPipeline` exposes the usual pipeline control
functions and a public helper for descriptor finalization:

- `Config` — configuration with `queue_capacity` (default 32),
  `worker_count` (default 1 in the implementation), and
  `with_content_hashing` (default true).
- `GeometryPipeline(co::ThreadPool&, Config)` — create the pipeline.
- `Start(co::Nursery&)` — starts worker coroutines.
- `Submit(WorkItem)` / `TrySubmit(WorkItem)` — submit work (blocking / non-
  blocking variants).
- `Collect()` — receive one completed `WorkResult` (suspends until ready or
  closed).
- `Close()` — close the input queue.
- `HasPending()` / `PendingCount()` / `GetProgress()` — progress and counters.
- `InputQueueSize()` / `OutputQueueSize()` — inspect channel sizes.
- `FinalizeDescriptorBytes(bindings, descriptor_bytes, material_patches, diagnostics)`
  — public coroutine that patches buffer bindings/materials into the supplied
  `descriptor_bytes`, computes `header.content_hash` on the ThreadPool when
  `with_content_hashing` is enabled, and returns the finalized byte vector or
  `std::nullopt` on failure. Diagnostics are appended to the provided
  diagnostics vector.

---

## Worker Behavior

Workers run as coroutines and process descriptor-finalization work items from
the bounded input queue. GeometryPipeline workers are lightweight: they call
`FinalizeDescriptorBytes()` to patch bindings/materials and optionally compute
`header.content_hash`.

For each work item the worker performs:

1) Check cancellation; if canceled, report a cancelled `WorkResult` and
   continue.
2) Invoke `on_started()` (if provided).
3) Call `FinalizeDescriptorBytes(bindings, cooked.descriptor_bytes,
   material_patches, diagnostics)` which:
   - Validates the supplied descriptor bytes and reads the header (`GeometryAssetDesc`).
   - Verifies `lod_count` matches `bindings.size()`.
   - Iterates each `MeshDesc` and, depending on `mesh_type`, patches the
     mesh blob with the concrete resource indices supplied in the matching
     `bindings` entry (standard/procedural/skinned support). Skinned blobs
     are patched with vertex/index/joint buffer indices and any auxiliary
     indices (e.g., `inverse_bind_buffer`, `joint_remap_buffer`) when present.
   - Writes submesh descriptors and mesh views verbatim.
   - Applies all material key patches by seeking to each recorded absolute
     offset and writing the `data::AssetKey` value. Offsets outside the
     descriptor bounds are treated as errors.
   - If `with_content_hashing` is enabled, schedules `util::ComputeContentHash`
     on the `co::ThreadPool` and, on success, writes `header.content_hash` into
     the descriptor bytes.
   - Returns the finalized bytes or `std::nullopt` on any failure, accumulating
     diagnostics.
4) If cancellation is requested after finalization started, report cancelled.
5) Determine `success = finalized.has_value() && diagnostics.empty()`.
6) Assemble and send a `WorkResult` containing the `source_id`, optional
   `cooked` payload (present on success), `finalized_descriptor_bytes`, any
   diagnostics, `telemetry` (e.g., `cook_duration`) and the `success` flag.
7) Invoke `on_finished()` if provided.

All errors are reported as `ImportDiagnostic` entries; no exceptions cross
async boundaries.

---

## Pipeline Stages (Complete)

### Mesh build (performed by `MeshBuildPipeline`)

1) **LOD validation**
   - Validate each LOD for positions/indices and other stream prerequisites.
   - Record diagnostics for missing or invalid streams.

2) **Vertex expansion + coordinate conversion**
   - Build one vertex per index (no vertex dedupe) and apply the required
     coordinate conversion to engine space.

3) **Attribute policy application**
   - Enforce `normal_policy` and `tangent_policy` and generate attributes as
     required. Emit diagnostics for missing prerequisites.

4) **Material bucketing**
   - Group triangle ranges by material slot and sort buckets by slot index.

5) **Submesh + view layout**
   - Construct `SubMeshDesc` and `MeshViewDesc` entries (tight ranges).

6) **Buffer payloads**
   - Build `CookedBufferPayload`s for vertex/index buffers and optional
     auxiliary buffers (skinning, morph targets, etc.).
   - Compute buffer-level content hashes as part of buffer emission processes
     (outside MeshBuildPipeline when buffers are written).

7) **Geometry descriptor serialization**
   - Emit `GeometryAssetDesc` + `MeshDesc[ lod_count ]` + submesh/view tables
     into `descriptor_bytes` (packed alignment = 1).
   - Record absolute byte offsets for each `SubMeshDesc::material_asset_key` so
     that material keys can be patched later.

### Descriptor finalization (performed by `GeometryPipeline`)

1) Validate the supplied `descriptor_bytes` and read `GeometryAssetDesc`.
2) Verify that the number of `bindings` matches `lod_count`.
3) For each LOD, read `MeshDesc` and any mesh-type blob; patch the mesh info
   with resource indices from the corresponding `bindings` entry.
4) Emit submesh descriptors and mesh views verbatim.
5) Apply all `material_patches` by writing `data::AssetKey` values at their
   recorded absolute offsets. Offsets outside the descriptor range are errors.
6) Compute `header.content_hash` on `co::ThreadPool` (if enabled) over the
   complete finalized bytes and write it into the header.
7) Return the finalized descriptor bytes or an error diagnostic.

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
- `material_patch_offsets` provides absolute offsets (from payload start) for
  each submesh material key

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

If `mesh_type == MeshType::kSkinned`, emit a skinned blob that carries the
resource indices for the mesh buffers as well as any auxiliary skinning tables
required by runtime. In practice the skinned blob contains at least:

- `vertex_buffer`, `index_buffer`, `joint_index_buffer`, `joint_weight_buffer`;
- optional auxiliary indices such as `inverse_bind_buffer` and `joint_remap_buffer`;
- per-mesh bounding box values (min/max).

The finalizer patches these resource indices from the supplied `bindings` and
the pipeline must still set `mesh_view_count`, `submesh_count`, and bounds as
usual.

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
  populated; `GeometryPipeline` computes `header.content_hash` on the
  `co::ThreadPool` after buffer indices are patched (when
  `with_content_hashing` is enabled).
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

## Adapter Design (Format Bridges)

The import adapter layer is format-agnostic. Importers must provide adapters
that translate FBX (`ufbx`) and glTF/GLB (`cgltf`) into `WorkItem` payloads for
`MeshBuildPipeline` (cooking) while preserving authored intent and attaching
full diagnostics context. `GeometryPipeline` consumes the cooked payloads and
finalizes descriptors.

### Adapter Contract (Common)

Adapters are responsible for **source acquisition**, **naming**, and
**material/LOD mapping**. The output must be a fully-populated `WorkItem`:

- **Identity**
  - `source_id`: stable diagnostic ID (e.g., `scene_path::mesh_name` or
    `scene_path::mesh_name::prim_2`).
  - `source_key`: stable pointer or hash of the source mesh/primitive.
- **Naming** (must be resolved before submission)
  - `mesh_name`, `storage_mesh_name` computed via
    `BuildMeshName` / `DisambiguateMeshName` /
    `NamespaceImportedAssetName`.
- **LOD Construction**
  - LOD order is importer-defined; `lods[0]` is highest detail.
  - `MeshLod::source_owner` must keep source data alive.
- **Material Mapping**
  - `material_keys` aligned with scene material array.
  - `default_material_key` used when a slot is missing.
  - `want_textures` and `has_material_textures` set from importer material
    analysis to drive UV diagnostics.
- **Coordinate Conversion**
  - Adapter declares source space and sets `ImportRequest.options.coordinate`
    to achieve the engine contract (right-handed, Z-up, forward = -Y).
- **Diagnostics**
  - Emit `ImportDiagnostic` for unsupported topology, missing attributes,
    invalid indices, and any lossy conversions (e.g., joint trimming).

### FBX Adapter (ufbx)

**Goal:** Map `ufbx_scene` into `WorkItem` entries with full material and LOD
coverage.

- **Standard meshes**
  - Preferred: emit `UfbxMeshView` (when supported) for zero-copy access.
  - Otherwise require triangle faces and emit explicit indices/ranges.
- **Skinned meshes**
  - Must emit `TriangleMesh` with `joint_indices`/`joint_weights`.
  - If >4 influences, keep highest 4, renormalize, emit warning diagnostic.
  - If joints/weights missing, emit `mesh.missing_skinning` and skip mesh.
- **Material slots**
  - Create one `TriangleRange` per material slot in the mesh (sorted).
  - Map `ufbx_material` index to `material_keys`.
- **LOD mapping**
  - If the scene provides LOD groups, map each LOD entry to a `MeshLod`.
  - Otherwise create a single `LOD0` entry.
- **Tangents/bitangents**
  - Use `ufbx` tangent layers when present; otherwise rely on pipeline
    generation based on `tangent_policy`.

### glTF/GLB Adapter (cgltf)

**Goal:** Map `cgltf_data` meshes and primitives into `TriangleMesh`-based
`WorkItem` entries.

- **Primitive handling**
  - Default: one `WorkItem` per primitive to avoid mismatched attribute sets.
  - Optional: merge primitives only when attribute layouts are identical and
    topology is triangle-list.
- **Topology**
  - Accept triangle-list primitives only; emit diagnostic for other modes.
- **Indices**
  - Convert index buffers to `uint32_t`; if missing, generate sequential
    indices for triangle-list primitives and emit a warning diagnostic.
- **Attributes**
  - Required: `POSITION`.
  - Optional: `NORMAL`, `TEXCOORD_0`, `COLOR_0`.
  - `TANGENT` is `vec4`; compute bitangent as
    `cross(normal, tangent.xyz) * tangent.w`.
- **Skinning**
  - Use `JOINTS_0`/`WEIGHTS_0` as `uvec4`/`vec4` (4 influences).
  - If more than 4 influences exist, trim + renormalize and emit diagnostic.
- **Materials**
  - Map `cgltf_material` to `material_keys`; null material uses
    `default_material_key`.
  - Set `has_material_textures` when any bound texture exists
    (base color, normal, metallic-roughness, occlusion, emissive).
- **LODs**
  - Map LODs explicitly when the importer provides authored LOD ordering.
  - Otherwise emit a single `LOD0` per primitive.

### Adapter Outputs and Post-Processing

- Adapters must preserve `source_owner` lifetimes for all `MeshSource` data.
- Any name truncation required for packed descriptors must emit diagnostics.
- Adapters should attach `object_path` (scene node path) to diagnostics for
  precise tooling feedback.

---

## Adapter API (Modern C++20)

The adapter layer should be modeled as **value-type adapters** with a
lightweight data contract and a concept-based interface. This keeps the job
orchestrator free of virtual dispatch and allows format-specific logic to be
compiled out when not used.

### Data Contracts

```cpp
struct GeometryAdapterInput final {
  std::string_view source_id_prefix;   // For stable diagnostic IDs
  std::string_view object_path_prefix; // For object_path diagnostics

  std::span<const data::AssetKey> material_keys;
  data::AssetKey default_material_key;

  ImportRequest request;               // Includes naming + coordinate policy
  std::stop_token stop_token;
};

struct GeometryAdapterOutput final {
  std::vector<GeometryPipeline::WorkItem> work_items;
  std::vector<ImportDiagnostic> diagnostics;
  bool success = true;
};
```

### Concept-Based Adapter Interface

```cpp
template <typename T, typename SourceT>
concept GeometryAdapter = requires(T adapter,
  const SourceT& source,
  const GeometryAdapterInput& input) {
  { adapter.BuildWorkItems(source, input) }
    -> std::same_as<GeometryAdapterOutput>;
};
```

### Adapter Implementations (Value Types)

```cpp
struct FbxGeometryAdapter final {
  GeometryAdapterOutput BuildWorkItems(
    const UfbxSceneView& scene, const GeometryAdapterInput& input) const;
};

struct GltfGeometryAdapter final {
  GeometryAdapterOutput BuildWorkItems(
    const CgltfSceneView& scene, const GeometryAdapterInput& input) const;
};
```

### Orchestrator Usage Pattern

```cpp
FbxGeometryAdapter adapter;
GeometryAdapterInput input { /* filled from job state */ };
auto output = adapter.BuildWorkItems(scene, input);
if (!output.success) {
  // diagnostics already populated
}
for (auto& item : output.work_items) {
  co_await geometry_pipeline.Submit(std::move(item));
}
```

### Design Notes

- **No shared ownership required**: `MeshLod::source_owner` carries lifetime.
- **No virtual dispatch**: concept + value type encourages inlining.
- **Strict, explicit inputs**: adapters do not reach into job globals.
- **Cancellation-aware**: adapters must check `input.stop_token` and return
  `success=false` with a cancellation diagnostic when requested.

---

## Implementation Readiness Checklist

The design is ready for implementation when the following are true:

- `GeometryAdapterInput` / `GeometryAdapterOutput` are defined in a shared
  header and used by both FBX and glTF adapters.
- `GeometryAdapter` concept is used to enforce the `BuildWorkItems(...)`
  signature at compile time.
- Adapter output is **zero-copy** where possible (spans + `source_owner`).
- Adapters emit diagnostics with `source_id` and `object_path` context.
- Jobs submit `WorkItem`s only; pipelines never touch format APIs.

---

## Adapter API Summary (Final)

```cpp
// Shared contract
struct GeometryAdapterInput final {
  std::string_view source_id_prefix;
  std::string_view object_path_prefix;
  std::span<const data::AssetKey> material_keys;
  data::AssetKey default_material_key;
  ImportRequest request;
  std::stop_token stop_token;
};

struct GeometryAdapterOutput final {
  std::vector<GeometryPipeline::WorkItem> work_items;
  std::vector<ImportDiagnostic> diagnostics;
  bool success = true;
};

template <typename T, typename SourceT>
concept GeometryAdapter = requires(T adapter,
  const SourceT& source,
  const GeometryAdapterInput& input) {
  { adapter.BuildWorkItems(source, input) }
    -> std::same_as<GeometryAdapterOutput>;
};
```

---

## Separation of Concerns

- **GeometryPipeline**: compute-only; no I/O.
- **BufferEmitter**: dedupe + async data writes.
- **AssetEmitter**: `.ogeo` write.
- **ScenePipeline**: consumes `geometry_keys` to build `.oscene`.

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
