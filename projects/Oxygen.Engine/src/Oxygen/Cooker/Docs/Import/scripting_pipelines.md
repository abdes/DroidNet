# Scripting Pipelines (v2)

**Status:** Implemented (Reference)
**Date:** 2026-03-01
**Parent:** [async_import_pipeline_v2.md](async_import_pipeline_v2.md)

---

## Overview

This document specifies the internal architecture for the **ScriptAssetImportPipeline** and **ScriptingSidecarImportPipeline** used by async imports. The pipelines are compute and serialization-heavy stages responsible for compiling scripts, managing global script metadata arrays, and dynamically patching scene dependencies natively within the cooking framework.

Core properties:

- **Compute & Serialization**: Parses JSON, resolves scene paths, merges component boundaries, and serializes patched descriptor payloads.
- **Job-scoped**: Pipelines are created per-job by `ScriptAssetImportJob` and `ScriptingSidecarImportJob` and run in the job's child nursery.
- **Global Payload Appends**: Merges definitions logically across large unstructured data sets (`scripts.data`, `script-bindings.data`).
- **In-place Scene Patching**: The sidecar pipeline relies on loading cooked `SceneAssetDesc` contexts, modifying `ScriptingComponentRecord` entries, and immediately re-emitting the patched scenes natively.

---

## Alignment With Current Architecture

The concurrency and lifetime models strictly follow the patterns mandated by [design/async_import_pipeline_v2.md](async_import_pipeline_v2.md).

### Pipeline vs Job Responsibilities

- **Job**: Instantiates the pipeline, invokes the Async File Reader to fetch the initial payload, computes overall runtime telemetry, orchestrates pipeline starts/awaits/collects, and tears down the nursery.
- **Pipeline**: Computes hashes, verifies data structures, performs compilation (via callbacks), and emits diagnostic errors and output payloads.

---

## Data Model

The scripting pipelines use the standard pipeline bounding channel representations.

### Script Asset Pipeline

```cpp
struct WorkItem {
  std::string source_id;
  std::vector<std::byte> source_bytes;
  observer_ptr<ImportSession> session;
  observer_ptr<LooseCookedIndexRegistry> index_registry;
  AsyncImportService::ScriptCompileCallback script_compile_callback;

  std::function<void()> on_started;
  std::function<void()> on_finished;
  std::stop_token stop_token;
};

struct WorkResult {
  std::string source_id;
  std::vector<ImportDiagnostic> diagnostics;
  ImportWorkItemTelemetry telemetry; // Populates cook_duration
  bool success = false;
};
```

### Scripting Sidecar Pipeline

```cpp
struct WorkItem {
  std::string source_id;
  std::vector<std::byte> source_bytes; // JSON Payload
  observer_ptr<ImportSession> session;
  observer_ptr<LooseCookedIndexRegistry> index_registry;

  std::function<void()> on_started;
  std::function<void()> on_finished;
  std::stop_token stop_token;
};

struct WorkResult {
  std::string source_id;
  std::vector<ImportDiagnostic> diagnostics;
  ImportWorkItemTelemetry telemetry;
  bool success = false;
};
```

---

## Public API (Pattern)

Both pipelines map directly to the standardized Async Cooker public API signatures:

```cpp
class [PipelineName] final {
public:
  struct Config {
    size_t queue_capacity = 16;
    uint32_t worker_count = 1;
  };

  explicit [PipelineName](Config config = {});
  ~[PipelineName]();

  void Start(co::Nursery& nursery);
  [[nodiscard]] auto Submit(WorkItem item) -> co::Co<>;
  [[nodiscard]] auto TrySubmit(WorkItem item) -> bool;
  [[nodiscard]] auto Collect() -> co::Co<WorkResult>;
  void Close();

  [[nodiscard]] auto HasPending() const noexcept -> bool;
  [[nodiscard]] auto PendingCount() const noexcept -> size_t;
  [[nodiscard]] auto GetProgress() const noexcept -> PipelineProgress;
  [[nodiscard]] auto OutputQueueSize() const noexcept -> size_t;
  [[nodiscard]] auto OutputQueueCapacity() const noexcept -> size_t;
};
```

---

## Worker Behavior

### Script Asset Pipeline Worker

For each `WorkItem`, the worker performs the following tasks:

1. **Validation**: Enforces `.import_kind == kScriptAsset`. Ensures that if `compile_scripts=true`, a valid `script_compile_callback` was mapped.
2. **Setup Descriptor**: Builds the base `ScriptAssetDesc` with `AssetKey` resolution (Random vs Deterministic) and populates the script name (truncated safely).
3. **Compilation Mode**: If required, yields to the compiler to generate the bytecode payload representation from `source_bytes`.
4. **Data Embedding** (When StorageMode = `kEmbedded`):
   - Opens the global `scripts.table` and `scripts.data` files for appending.
   - Pushes a `ScriptResourceDesc` representing the `kSource` layout boundary into the table, and appends the raw text stream into the blob.
   - Repeats the embedding path to associate `kBytecode` outputs on successful compilation.
   - Dynamically records the generated `ResourceIndexT` identifiers into the `ScriptAssetDesc`.
5. **Data Externalization** (When StorageMode = `kExternal`):
   - Sets flags `kAllowExternalSource` and embeds the physical file system path inside the descriptor's reserved string arrays explicitly without updating blob repositories.
6. **Emission**: Passes the fully generated descriptor back to the central `AssetEmitter` logic.

### Scripting Sidecar Pipeline Worker

The sidecar execution is complex as it performs "patch operations".

1. **JSON Parsing**: Ingests bytes via `nlohmann::json`. Validates `bindings` schemas, extracts scalar bounds, mapping paths (`script_virtual_path`), `slot_id` elements, and dynamically-typed script `params` structures. Verifies identity boundaries resolving conflicts.
2. **Context Resolution**: Maps `target_scene_virtual_path` utilizing the pipeline's available `VirtualPathResolver` resolving scene dependencies from `inflight_scene_contexts` or querying `CookedInspectionContext`.
3. **Global Extrapolations**: Fetches the underlying `script-bindings.table` (contains `ScriptSlotRecord`) and `script-bindings.data` (contains `ScriptParamRecord`). Ensures no orphaned structures exist.
4. **Merge Strategy**: Evaluates prior sidecar application bounds dynamically loaded from parsed `ScriptingComponentRecord` indices on the Scene context.
   - New overlapping bounds dynamically re-baseline records, copying preceding valid arguments from tables without overwriting unrelated node mappings logically assigned within that scene.
   - Generates and writes out serialized bindings appending changes.
5. **Descriptor Patching**: Maps bytes spanning the target `SceneAssetDesc` and overwrites internal node boundaries updating `component_table_count` layouts dynamically mapping the updated indexing variables gracefully. Restores trailing sequence bytes (`SceneEnvironmentBlockHeader`).
6. **Finalization**: Emits re-calculated content hashes of the updated binary blob, writes parameter binaries globally, explicitly registers changes within `LooseCookedIndexRegistry`, and pushes patched scene objects via `AssetEmitter`.

---

## Cooked Output Contract

### Script Assets

- Emits standard `.oscript` descriptor blocks sized reliably at `< 1KB`.
- Contains binary parameters: `flags`, `source_resource_index`, `bytecode_resource_index`, `external_source_path`, `name`.

### Global Repositories

The pipeline encapsulates definitions into single global resources:

- `scripts.table`: Divisible precisely by `sizeof(ScriptResourceDesc)`. Accommodates `encoding`, limits, byte properties, and 64-bit content signatures. Modifiable dynamically via appending indices.
- `scripts.data`: Contiguous payload mappings. Bounds scale up to `DataBlobSizeT::max()`.
- `script-bindings.table`: Maintains collections of `ScriptSlotRecord`. Resolves virtual hierarchies matching `ScriptAssetKey` IDs seamlessly across multiple bound instances.
- `script-bindings.data`: Tightly packed sequence of `ScriptParamRecord` configurations linked universally through Offset parameters tracked cleanly via `params_array_offset`.

---

## Progress Reporting

Standard `submitted`, `completed`, `failed` counts track atomic states effectively mapped onto the core Importer job contexts natively via `PipelineProgress`. Telemetry timing records precise metric outputs accurately reflecting compilation wait periods via `ImportWorkItemTelemetry::cook_duration`.

---

## Robustness Rules (Do Not Violate)

The pipeline throws deterministic `ImportDiagnostic` logs strictly mapping precise failure causes safely. It does NOT generate unhandled C++ exceptions globally.
Key diagnostic behaviors track precisely mapped contexts to provide clear tooling feedback:

- `script.asset.compile_failed`: Identifies explicit issues with native compilation boundaries.
- `script.sidecar.payload_invalid`: Emits detailed contextual keys dynamically pointing directly mapping back sequentially like `bindings[0].node_index` enabling exact issue correction directly mapped into UI elements.
- `script.sidecar.patch_map_invariant_failure`: Bounds verifications isolating global pointer alignment mismatches isolating race conflicts cleanly.
- Pipeline gracefully catches `nlohmann::json` parsing failures via safe try-blocks isolating structural anomalies deterministically emitting `script.sidecar.parse_failed`.
- Never overwrites parameters out-of-bounds logically protecting parameter stability enforcing robust bounds checking on index boundaries reliably.

---

## See Also

- `src/Oxygen/Cooker/Import/Internal/Pipelines/ScriptImportPipelineCommon.h`
- `src/Oxygen/Data/PakFormat.h` (for ScriptAssetDesc, SceneAssetDesc semantics)
- [async_import_pipeline_v2.md](async_import_pipeline_v2.md)
