# Scene Pipeline (v2)

**Status:** Complete Design (Phase 5)
**Date:** 2026-01-26
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
- **Geometry-aware**: links nodes to geometry assets via `geometry_keys`.
- **PAK v4 container, v3 scene asset**: uses the v3 scene asset layout as
  defined in [src/Oxygen/Data/PakFormat.h](../src/Oxygen/Data/PakFormat.h).
- **Planner‑gated**: the planner submits scene work only after referenced
  geometry assets are ready.

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

The pipeline is intentionally narrow: adapters produce an in-memory
SceneBuild (node records, strings, and component record arrays) and the
pipeline serializes and packages that build into the final `.oscene` bytes.

```cpp
struct SceneEnvironmentSystem {
  uint32_t system_type = 0; // EnvironmentComponentType
  std::vector<std::byte> record_bytes; // Includes record header
};

// Intermediate scene build data produced by adapters.
struct SceneBuild {
  std::vector<data::pak::NodeRecord> nodes;
  std::vector<std::byte> strings; // string table bytes (must start with '\0')

  std::vector<data::pak::RenderableRecord> renderables;
  std::vector<data::pak::PerspectiveCameraRecord> perspective_cameras;
  std::vector<data::pak::OrthographicCameraRecord> orthographic_cameras;
  std::vector<data::pak::DirectionalLightRecord> directional_lights;
  std::vector<data::pak::PointLightRecord> point_lights;
  std::vector<data::pak::SpotLightRecord> spot_lights;
};

// Input to adapter scene stage.
struct SceneStageInput {
  std::string_view source_id;
  std::span<const data::AssetKey> geometry_keys;
  const ImportRequest* request = nullptr;
  observer_ptr<NamingService> naming_service;
  std::stop_token stop_token;
};

// Result returned by the adapter's scene stage.
struct SceneStageResult {
  SceneBuild build;
  bool success = false;
};

// Concept for adapters that can run the scene stage.
// Adapter must implement: SceneStageResult BuildSceneStage(const SceneStageInput&, std::vector<ImportDiagnostic>&)

// Work submission item. Adapters supply a typed adapter instance which
// is stored as an opaque owner plus a function pointer that calls
// `BuildSceneStage` on the concrete adapter type.
struct WorkItem {
  std::string source_id;
  std::shared_ptr<const void> adapter_owner;
  using BuildStageFn = SceneStageResult (*)(const void* adapter,
    const SceneStageInput& input, std::vector<ImportDiagnostic>& diagnostics);
  BuildStageFn build_stage = nullptr;

  std::vector<data::AssetKey> geometry_keys;
  std::vector<SceneEnvironmentSystem> environment_systems;

  // Optional callbacks observable by the orchestrator.
  std::function<void()> on_started;
  std::function<void()> on_finished;

  ImportRequest request;
  observer_ptr<NamingService> naming_service;
  std::stop_token stop_token;

  // Adapter helpers construct a WorkItem from a typed adapter + inputs.
};

// Work completion result.
struct WorkResult {
  std::string source_id;
  std::optional<CookedScenePayload> cooked;
  std::vector<ImportDiagnostic> diagnostics;
  ImportWorkItemTelemetry telemetry; // contains cook duration
  bool success = false;
};
```

Notes:

- Adapters are responsible for producing a fully-formed `SceneBuild`:
  node traversal, naming, node IDs, local TRS, node flags, node pruning,
  coordinate conversion (to engine space) and attaching component records
  (renderables/cameras/lights). The pipeline does NOT perform these
  operations — it expects the `SceneBuild` to be adapter-provided.
- The adapter-provided `strings` must start with a leading NUL byte so
  offset `0` maps to the empty string.
- `request` provides naming strategy and asset key policy; the pipeline
  performs final scene naming only to build asset keys and paths.
- `environment_systems` encodes the trailing scene environment block (PAK v3).
  The pipeline validates system record headers and computes the block size.
- `geometry_keys` must contain resolved geometry keys; the planner must
  ensure geometry assets are ready before submission. The adapter’s mesh
  traversal order defines the index mapping used by renderable records.

---

## Adapter + Planner Integration (Definitive)

ScenePipeline work items are produced **directly from the native importer
scene** (ufbx/cgltf) with **no intermediate scene graph**. The format adapter
walks the native node hierarchy and implements a `BuildSceneStage` that
produces a `SceneBuild` (nodes, strings, component tables). The adapter
must produce fully-converted/validated `SceneBuild` data (see notes above)
and may use the provided `WorkItem` helper to create a submission container
that holds an opaque `adapter_owner` and an adapter `build_stage` function.
The adapter must not re-pack or clone node data in a way that loses the
original import semantics.

Planner integration rules:

- The adapter registers a **scene plan item** and stores a
  `WorkPayloadHandle` that points to the ScenePipeline `WorkItem` storage
  owned by the job.
- The planner **gates** scene submission on geometry readiness using
  dependencies declared for each `RenderableRecord::geometry_key`.
- The job executes the plan steps; for the scene step it waits on
  `PlanStep.prerequisites`, then submits the stored `WorkItem` to the
  ScenePipeline.

This keeps the pipeline compute-only while ensuring the planner enforces
geometry prerequisites without introducing new scene representations.

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

    //! Enable or disable scene content hashing.
    /*!
     When false, the pipeline MUST NOT compute `content_hash`.
    */
    bool with_content_hashing = true;
  };

  explicit ScenePipeline(co::ThreadPool& thread_pool, Config cfg = {});

  ~ScenePipeline() override;

  //! Start worker coroutines in the given nursery.
  void Start(co::Nursery& nursery);

  //! Submit work (may suspend if the queue is full).
  [[nodiscard]] auto Submit(WorkItem item) -> co::Co<>;

  //! Try to submit work without blocking.
  [[nodiscard]] auto TrySubmit(WorkItem item) -> bool;

  //! Collect one completed result (suspends until ready or closed).
  [[nodiscard]] auto Collect() -> co::Co<WorkResult>;

  //! Close the input queue.
  void Close();

  //! Whether any submitted work is still pending completion.
  [[nodiscard]] auto HasPending() const noexcept -> bool;

  //! Number of submitted work items not yet collected.
  [[nodiscard]] auto PendingCount() const noexcept -> size_t;

  //! Get pipeline progress counters.
  [[nodiscard]] auto GetProgress() const noexcept -> PipelineProgress;

  //! Number of queued items waiting in the input queue.
  [[nodiscard]] auto InputQueueSize() const noexcept -> size_t;

  //! Capacity of the input queue.
  [[nodiscard]] auto InputQueueCapacity() const noexcept -> size_t;

  //! Number of completed results waiting in the output queue.
  [[nodiscard]] auto OutputQueueSize() const noexcept -> size_t;

  //! Capacity of the output queue.
  [[nodiscard]] auto OutputQueueCapacity() const noexcept -> size_t;
};
```

---

## Worker Behavior

At runtime each worker coroutine processes items from the input channel and
follows this behavior (implementation notes & diagnostics below):

1) Check cancellation: if `WorkItem.stop_token` is requested the pipeline
   sends a cancelled `WorkResult` (diagnostic `import.canceled`) and continues.
2) Validate adapter presence: if `adapter_owner` or `build_stage` is missing
   the pipeline emits `scene.adapter_missing` and fails the item.
3) Run the adapter scene stage on the `ThreadPool` (`BuildSceneStage`) using
   the typed adapter via the opaque `build_stage` function pointer. The
   stage is cancellable; the pipeline collects any diagnostics emitted by the
   stage.
4) If the stage was cancelled the pipeline sends a cancelled result; if the
   stage failed without diagnostics the pipeline emits `scene.stage_failed`.
5) On stage success the pipeline:
   - Sorts all component arrays by `node_index`.
   - Builds final scene naming/paths for the asset key (uses
     `request.GetSceneName()` + `request.loose_cooked_layout` to produce
     `virtual_path` and descriptor `relpath`).
   - Serializes the payload (packed alignment=1) to an in-memory buffer in
     this order: `SceneAssetDesc`, `NodeRecord[]`, string-table bytes,
     component tables, component directory, then the SceneEnvironment block.
   - When serializing the environment systems the pipeline validates each
     `SceneEnvironmentSystemRecordHeader` (size >= header size and matches
     the available record bytes); failures emit blocking diagnostics
     (`scene.environment.*`).
   - Writer failures produce `scene.serialize_failed` diagnostics.
6) If serialization succeeded and `Config::with_content_hashing` is true
   the pipeline computes the `content_hash` on the `ThreadPool` (cancellable)
   and patches `SceneAssetDesc.header.content_hash` when the computed hash is
   non-zero.
7) The worker constructs a `WorkResult` with collected diagnostics and
   `ImportWorkItemTelemetry` (cook duration) and sends it to the output
   channel. If `WorkItem.on_finished` is provided it is invoked after work
   completes.

Notes:

- `WorkItem.on_started` is called just before the stage begins processing.
- All errors and validation failures are expressed as `ImportDiagnostic`.
- The pipeline never throws exceptions across coroutine boundaries; failures
  are reported via diagnostics and `success=false` in `WorkResult`.

---

## Pipeline Stages (Complete)

The pipeline is split between an adapter-provided "scene stage" and a
pure-serialization stage performed by `ScenePipeline` workers.

Adapter responsibilities (the adapter's `BuildSceneStage` must do these):

- Node traversal & naming: produce `NodeRecord` entries with stable
  `scene_name_offset` values into the string table.
- Deterministic node IDs, disambiguation and uniqueness enforcement.
- Apply coordinate conversion (to engine space) if indicated by source
  metadata; re-normalize rotations after conversion.
- Apply `NodePruningPolicy` and preserve world transforms when reparenting.
- Populate node flags from source metadata and import overrides.
- Attach components (renderables/cameras/lights) and populate component
  records with `node_index` referring to the node table.
- Ensure the string table begins with a leading NUL byte.

Pipeline responsibilities (performed by `ScenePipeline`):

- Sort each component array by `node_index`.
- Serialize the scene asset into a packed in-memory blob in this order:
  `SceneAssetDesc`, `NodeRecord[]`, string table, component tables,
  component directory, SceneEnvironmentBlock.
- Validate environment system records (record header size, payload size).
- Compute and patch `SceneAssetDesc.header.content_hash` when hashing is
  enabled (computed on the `ThreadPool`).
- Emit diagnostics for serialization and environment validation failures.

Notes:

- `SceneAssetDesc.header.version` is set to `data::pak::kSceneAssetVersion`.
- Unknown environment system types are preserved if their record size is
  valid; invalid records block the work item with diagnostics.

---

## Cooked Output Contract (PAK v4 Container, v3 Scene Asset)

This pipeline targets the latest PAK container while emitting the
`data::pak::v3` scene asset layout. Layout changes that exceed the existing
structures must introduce a new PAK namespace and a new scene asset version.

### Scene Descriptor (`.oscene`)

Packed binary blob:

```text
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
  block bytes and is computed on the ThreadPool after dependencies are ready
  **when hashing is enabled**.

### Scene Environment Block

- Always present in v3 scenes (may be empty).
- `SceneEnvironmentBlockHeader.byte_size` includes the header and all records.
- Each record begins with `SceneEnvironmentSystemRecordHeader` and uses
  `record_size` for skipping unknown types.
- Supported system types include `SkyAtmosphere`, `VolumetricClouds`, `Fog`,
  `SkyLight`, `SkySphere`, and `PostProcessVolume`.

---

## Feature Completion Requirements

- **Adapter stage** (adapter must implement):
  - Node traversal, disambiguated naming, deterministic `node_id` generation.
  - Coordinate conversion to engine space (if required) and quaternion
    re-normalization.
  - `NodePruningPolicy` with transform preservation and reparenting.
  - Populate complete node flags and component records (renderables/cameras/lights).
  - Provide a string table that starts with a leading NUL byte.

- **Pipeline**:
  - Sort component tables by `node_index`.
  - Serialize `SceneAssetDesc` and trailing payload with packed alignment.
  - Validate and append the SceneEnvironment block; reject invalid system records.
  - Populate `SceneAssetDesc.header.version` and compute `header.content_hash`
    when hashing is enabled.

### Importer/Orchestrator Responsibilities

- Geometry emission and `geometry_keys` population.
- Import content flags and naming strategy setup.
- Asset key policy (deterministic vs random).
- Environment system extraction (SkyAtmosphere, Fog, PostProcess, etc.).

---

## Separation of Concerns

- **ScenePipeline**: compute-only descriptor assembly.
- **AssetEmitter**: `.oscene` output and records.
- **GeometryPipeline**: produces geometry asset keys; the job assembles
  `geometry_keys` for the scene work item.

---

## Cancellation Semantics

The pipeline honors `WorkItem.stop_token` at all cancellable points (adapter
stage and thread-pool hashing). Cancellation causes the pipeline to emit a
cancelled `WorkResult` with diagnostic `import.canceled` and `success=false`.
Partial serialized buffers are not emitted as successful outputs.

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
3) The adapter must provide a string table that starts with `\0` so offset
   `0` maps to the empty string; the pipeline will serialize the bytes as
   provided.
4) Environment block header is always appended (even empty) and system record
   headers must be validated by the pipeline.
5) `header.content_hash` (when hashing is enabled) must cover the entire
   descriptor payload including the environment block; the pipeline computes
   the hash on the `ThreadPool` and patches the header only when the hash is
   non-zero.

Diagnostic codes emitted by the pipeline include (non-exhaustive):

- `scene.adapter_missing` (missing adapter/build-stage),
- `scene.stage_failed` (adapter stage failed without diagnostics),
- `scene.serialize_failed` (writer errors),
- `scene.environment.record_too_small`,
- `scene.environment.record_size_invalid`,
- `scene.environment.record_size_mismatch`,
- `import.canceled` (cancellation).

---

## Coordinate Conversion Policy (Definitive)

The engine space is **right-handed, Z-up, forward = -Y** (see
[src/Oxygen/Core/Constants.h](../src/Oxygen/Core/Constants.h)). Coordinate
conversion is **one-shot** and must be applied by the adapter during its
`BuildSceneStage` when the source space differs from engine space.

Rules:

1) The importer/adapter must supply source-space metadata per scene.
2) The adapter must apply at most one conversion to produce final engine
   space (translations, rotations, scales) and re-normalize quaternions.
3) No pipeline-side "unmapping" or additional remappings are allowed.
4) If source space already matches engine space the adapter should be a no-op.

Missing or inconsistent source-space metadata is a **blocking error** (the
adapter should emit a blocking diagnostic so the pipeline can fail the
work item).

---

## Deterministic Naming and Node IDs

- The adapter is responsible for disambiguating `node_name` values and
  ensuring deterministic `node_id` generation. A stable strategy (name +
  traversal ordinal) should be used; append `_N` suffixes in traversal order
  for any remaining duplicates and emit a warning.
- `node_id` is typically generated with a deterministic asset key policy such
  as `MakeDeterministicAssetKey(virtual_path + "/" + node_name)`.

---

## Node Pruning (Deterministic Rules)

Node pruning and reparenting are performed by the adapter during the scene
stage. When a node is dropped the adapter must reparent its children to the
nearest kept ancestor and recompute each child’s local transform as:

$$
T_{local}' = T_{parent}^{-1} \cdot T_{world}
$$

where $T_{world}$ is the original world transform and $T_{parent}$ is the new
parent’s world transform. Use float32 math; if inversion fails (singular), the
adapter should emit a blocking diagnostic and keep the original parent.

---

## Environment Block Validation

- Validate each `SceneEnvironmentSystemRecordHeader`:
  - `record_size >= sizeof(SceneEnvironmentSystemRecordHeader)`.
  - `record_size` does not exceed remaining bytes.
- Unknown `system_type` values are preserved if `record_size` is valid.
- If validation fails, emit a blocking diagnostic and fail the work item.

---

## Validation and Limits (Implementation Behavior)

- Most structural validation (node counts, string table size, camera/light
  value sanity) is the responsibility of the adapter during the scene stage.
  The adapter should emit diagnostics and fail the stage for blocking errors.
- The pipeline enforces and validates environment system records, serializes
  the payload, and reports writer/serialization failures.
- All counts and offsets are written using fixed-size fields from the PAK
  schema; adapters must ensure their sizes fit target field types.

---

## See Also

- [geometry_work_pipeline_v2.md](geometry_work_pipeline_v2.md)
- `src/Oxygen/Content/Import/Internal/Pipelines/ScenePipeline.*` (implementation)
- `src/Oxygen/Content/Import/emit/SceneEmitter.*` (legacy behavior)
- `src/Oxygen/Data/PakFormat.h` (SceneAssetDesc/NodeRecord/Component tables)
- `src/Oxygen/Content/Loaders/SceneLoader.*` (loader-side validation)
