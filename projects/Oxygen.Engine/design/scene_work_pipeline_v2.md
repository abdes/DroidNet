# Scene Pipeline (v2)

**Status:** Complete Design (Phase 5)
**Date:** 2026-01-15
**Parent:** [async_import_pipeline_v2.md](async_import_pipeline_v2.md)

---

## Overview

This document specifies the **ScenePipeline** used by async imports to build
cooked scene descriptors (`.oscene`). The pipeline is compute-only and must
produce **feature-complete** scene assets that fix legacy omissions while
staying compatible with the PAK format and async architecture.

Core properties:

- **Compute-only**: builds the complete scene descriptor payload in memory.
- **No I/O**: `AssetEmitter` writes `.oscene` files.
- **Job-scoped**: created per job and started in the job’s child nursery.
- **Geometry-aware**: links nodes to geometry assets via `ImportedGeometry`.
- **PAK v3**: emits the v3 scene format to support the environment block.

---

## Alignment With Current Architecture

The pipeline-agnostic concurrency and lifetime model is defined in the parent
document [design/async_import_pipeline_v2.md](design/async_import_pipeline_v2.md)
under **Concurrency, Ownership, and Lifetime (Definitive)**.

### Pipeline vs Emitter Responsibilities

- **ScenePipeline**: produces scene descriptor bytes and metadata.
- **AssetEmitter**: writes `.oscene` files and records assets.

---

## Data Model

### WorkItem

```cpp
struct SceneSource {
  const ufbx_scene* scene = nullptr;
  std::shared_ptr<const void> scene_owner; // Keeps scene alive
};

struct SceneEnvironmentSystem {
  uint32_t system_type = 0; // EnvironmentComponentType
  std::vector<std::byte> record_bytes; // Includes record header
};

struct WorkItem {
  std::string source_id;
  SceneSource source;
  std::vector<ImportedGeometry> geometry_map; // mesh* -> AssetKey
  std::vector<SceneEnvironmentSystem> environment_systems;
  ImportRequest request;
  std::stop_token stop_token;
};
```

Notes:

- `SceneSource` is FBX-specific today. Other importers must provide an
  equivalent scene view with the same semantics.
- `request` provides naming strategy, asset key policy, and coordinate policy.
- `environment_systems` encodes the trailing scene environment block (PAK v3).
  The pipeline validates record headers and computes the block size.

### WorkResult

```cpp
struct CookedScenePayload {
  data::AssetKey scene_key;
  std::string virtual_path;
  std::string descriptor_relpath;
  std::vector<std::byte> descriptor_bytes; // .oscene payload
};

struct WorkResult {
  std::string source_id;
  std::optional<CookedScenePayload> cooked;
  std::vector<ImportDiagnostic> diagnostics;
  bool success = false;
};
```

---

## Public API (Pattern)

```cpp
class ScenePipeline final {
public:
  struct Config {
    size_t queue_capacity = 8;
    uint32_t worker_count = 1;
  };

  explicit ScenePipeline(co::ThreadPool& thread_pool, Config cfg = {});

  void Start(co::Nursery& nursery);

  [[nodiscard]] auto Submit(WorkItem item) -> co::Co<>;
  [[nodiscard]] auto TrySubmit(WorkItem item) -> bool;
  [[nodiscard]] auto Collect() -> co::Co<WorkResult>;

  void Close();
  void Cancel();

  [[nodiscard]] auto HasPending() const noexcept -> bool;
  [[nodiscard]] auto PendingCount() const noexcept -> size_t;
  [[nodiscard]] auto GetProgress() const noexcept -> PipelineProgress;
};
```

---

## Worker Behavior

For each work item:

1) Check cancellation; if cancelled, return `success=false`.
2) Compute scene naming:
   - `scene_name = BuildSceneName(request)`
   - `virtual_path = request.loose_cooked_layout.SceneVirtualPath(scene_name)`
   - `descriptor_relpath = request.loose_cooked_layout.SceneDescriptorRelPath(
      scene_name)`
   - `scene_key` based on `AssetKeyPolicy`
3) Build node table (preorder traversal):
   - `node_name = BuildSceneNodeName(authored_name, request, ordinal,
      parent_name)`
   - `NodeRecord` per node, parent indices, flags resolved
   - Apply coordinate conversion via `coord::ApplySwapYZIfEnabled`
   - `node_id = MakeDeterministicAssetKey(virtual_path + "/" + node_name)`
   - String table begins with `\0`, names appended as UTF-8
4) Apply `NodePruningPolicy`:
   - `kKeepAll`: keep all nodes.
   - `kDropEmptyNodes`: drop nodes with no geometry/camera/light components.
   - When dropping a node, reparent its children to the nearest kept ancestor and
     recompute local transforms to preserve world-space transforms.
5) Attach components:
   - Renderables for nodes whose mesh exists in `geometry_map`
   - Perspective/orthographic cameras based on `ufbx_camera`
   - Lights based on `ufbx_light` (directional, point, spot)
6) Light fallback:
   - If lights exist in `scene.lights` without direct node light components,
     attach them based on instance nodes.
7) Sort component tables by `node_index`.
8) Serialize descriptor payload in packed order:
   - `SceneAssetDesc` (version = `data::pak::v3::kSceneAssetVersion`)
   - `NodeRecord[]`
   - string table
   - component tables (renderables, cameras, lights)
   - component directory (`SceneComponentTableDesc[]`)
9) Append trailing SceneEnvironment block:
   - `SceneEnvironmentBlockHeader` + system records
   - `byte_size` covers header + records
10) Compute `header.content_hash` over descriptor bytes + environment block.
11) Return `CookedScenePayload`.

All errors must be converted to `ImportDiagnostic`; no exceptions cross async
boundaries.

---

## Pipeline Stages (Complete)

1) **Node traversal**
   - Preorder traversal starting at `scene.root_node`.
   - Node indices are the traversal order.
   - If no nodes, emit a synthetic root named `"root"`.

2) **Naming and keys**
   - Node name uses `BuildSceneNodeName` with naming strategy.
   - Node asset key is deterministic from node virtual path.

3) **Transforms**
   - Use local TRS from `ufbx_node::local_transform`.
   - Apply swap-YZ coordinate conversion when enabled.

4) **Node flags**
   - `kSceneNodeFlag_Visible`: from source visibility.
   - `kSceneNodeFlag_Static`: from importer override or source metadata.
   - `kSceneNodeFlag_CastsShadows` / `kSceneNodeFlag_ReceivesShadows`: from
     light/mesh properties when available.
   - `kSceneNodeFlag_RayCastingSelectable` and
     `kSceneNodeFlag_IgnoreParentTransform`: from import options or source tags.

5) **Node pruning**
   - Apply `NodePruningPolicy` with transform preservation and reparenting.

6) **Renderables**
   - Emit `RenderableRecord` when mesh is found in `geometry_map`.

7) **Cameras**
   - Perspective: `fov_y` from `field_of_view_deg.y` (degrees → radians),
     near/far are absolute with swap if inverted.
   - Orthographic: half extents from `orthographic_size`, near/far absolute.
   - Unsupported camera projection modes are skipped with diagnostics.

8) **Lights**
   - Directional, point, spot are supported.
   - Area/volume lights are converted to point lights with diagnostic
     `fbx.light.unsupported_type`.
   - Light properties read from FBX props when present (color, intensity,
     attenuation, cone angles, shadow flags).
   - Lights listed in `scene.lights` but missing node attachments are emitted
     via instance-node fallback.

9) **Component sorting**
   - All component tables are sorted by `node_index`.

10) **Scene payload layout**
   - `SceneAssetDesc.header.version = data::pak::v3::kSceneAssetVersion`
   - Component directory offset is after all component tables.
   - Append `SceneEnvironmentBlockHeader` and system records.

---

## Cooked Output Contract (PAK v3)

This pipeline targets `oxygen::data::pak::v3`. Layout changes that exceed
the existing structures must introduce a new PAK namespace (v5) and a new
scene asset version.

### Scene Descriptor (`.oscene`)

Packed binary blob:

```
SceneAssetDesc (256 bytes)
NodeRecord[ nodes.count ]
Scene string table (NUL-terminated UTF-8, starts with '\0')
Component tables (Renderables, Cameras, Lights)
SceneComponentTableDesc[ component_table_count ]
SceneEnvironmentBlockHeader + system records (always present)
```

Notes:

- Offsets are relative to the start of the descriptor payload.
- `nodes.entry_size` must equal `sizeof(NodeRecord)`.
- Component tables use `ComponentType` FourCC values (e.g., `kRenderable`).
- `SceneAssetDesc.header.version = data::pak::v3::kSceneAssetVersion`.
- `SceneAssetDesc.header.content_hash` must cover descriptor bytes + environment
  block bytes.

### Scene Environment Block

- Always present in v3 scenes (may be empty).
- `SceneEnvironmentBlockHeader.byte_size` includes the header and all records.
- Each record begins with `SceneEnvironmentSystemRecordHeader` and uses
  `record_size` for skipping unknown types.
- Supported system types include `SkyAtmosphere`, `VolumetricClouds`, `Fog`,
  `SkyLight`, `SkySphere`, and `PostProcessVolume`.

---

## Feature Completion Requirements

- **Node pruning**: `NodePruningPolicy` is enforced with transform preservation.
- **Node flags**: full flag set is populated from source metadata or import
  overrides.
- **Environment block**: v3 environment systems are serialized with valid
  headers and sizes.
- **Header metadata**: `header.version` and `header.content_hash` are populated.

### Importer/Orchestrator Responsibilities

- Geometry emission and `ImportedGeometry` mapping.
- Import content flags and naming strategy setup.
- Asset key policy (deterministic vs random).
- Environment system extraction (SkyAtmosphere, Fog, PostProcess, etc.).

---

## Separation of Concerns

- **ScenePipeline**: compute-only descriptor assembly.
- **AssetEmitter**: `.oscene` output and records.
- **GeometryPipeline**: produces `ImportedGeometry` mapping.

---

## Cancellation Semantics

Pipeline must honor `WorkItem.stop_token`. Cancellation returns
`success=false` without partial outputs.

---

## Backpressure and Memory Safety

Use bounded queues. Scenes are typically few, so default capacity is small.

---

## Progress Reporting

Pipeline tracks submitted/completed/failed counts and exposes
`PipelineProgress`. The job maps this to `ImportProgress`.

---

## Robustness Rules (Do Not Violate)

1) Pipeline never writes output files.
2) Scene payload must be packed with alignment = 1.
3) String table must start with `\0` so offset `0` maps to empty string.
4) Environment block header is always appended (even empty).
5) `header.content_hash` must cover payload + environment block.

---

## Integration Checklist (Phase 5)

- [ ] Implement `Async/Pipelines/ScenePipeline.h/.cpp`
- [ ] Implement node pruning and flag mapping
- [ ] Serialize v3 environment block records
- [ ] Integrate with `FbxImportJob::EmitScene` and glTF import
- [ ] Emit `.oscene` via `session.AssetEmitter().Emit(...)`
- [ ] Add unit tests for pruning, flags, and environment block offsets

---

## See Also

- [geometry_work_pipeline_v2.md](geometry_work_pipeline_v2.md)
- `src/Oxygen/Content/Import/emit/SceneEmitter.*` (legacy behavior)
- `src/Oxygen/Data/PakFormat.h` (SceneAssetDesc/NodeRecord/Component tables)
