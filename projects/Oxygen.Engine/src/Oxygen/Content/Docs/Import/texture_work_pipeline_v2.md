# Texture Pipeline (v2)

**Status:** Aligned Design (Phase 5)
**Date:** 2026-01-15
**Parent:** [async_import_pipeline_v2.md](async_import_pipeline_v2.md)

---

## Overview

This document specifies the **TexturePipeline** used by async imports to
parallelize texture cooking within **a single import job**. The pipeline is
compute-only and must preserve **byte-for-byte parity** with the legacy
synchronous texture importer, including placeholder behavior and cooked payload
format.

Core properties:

- **Decode → process → mips → format conversion/compression → packing**
- **No indices**: returns `CookedTexturePayload` for the job to emit
- **Job-scoped**: created per job and started in the job’s child nursery
- **ThreadPool offload**: heavy stages run on `co::ThreadPool`
- **Optional async file reads**: when configured with an `IAsyncFileReader`, the
  pipeline may resolve `WorkItem::source_path` into bytes (read-only).
- **Reuse existing cookers**: `TextureCooker` and `TextureEmissionUtils` define
  the canonical behavior
- **Planner‑gated**: the planner submits work only when dependencies are ready
  (textures typically have no upstream dependencies)
- **Configurable hashing**: payload `content_hash` is optional and is
  controlled by `ImportOptions::with_content_hashing`, cascading into the
  pipeline config. When disabled, the pipeline MUST NOT compute hashes.

The concrete stage graph and data contracts are specified in
**Pipeline Stages (Legacy Cooker Parity)** and **Cooked Output Contract**.

---

## Alignment With Current Architecture

The pipeline-agnostic concurrency and lifetime model is defined in the parent
document [design/async_import_pipeline_v2.md](design/async_import_pipeline_v2.md)
under **Concurrency, Ownership, and Lifetime (Definitive)**. This texture design
assumes that model (pipelines are job-scoped, started in the job’s child nursery;
job orchestration acquires sources, submits, collects, and commits via emitters).

### Pipeline vs Emitter Responsibilities

- **TexturePipeline**: produces `CookedTexturePayload` (compute-only, with the
  exception of loading external texture sources), including deterministic
  placeholders when requested.
- **TextureEmitter**: ensures fallback index `0`, deduplicates by signature,
  assigns stable indices immediately, and performs all async I/O to
  `textures.data` and `textures.table`.
- **ImportSession::Finalize()**: waits for emitter I/O, writes tables, then
  writes `container.index.bin` **last**.

---

## Data Model

### WorkItem

The pipeline operates on source bytes already in memory. **Source acquisition is
not the pipeline’s job**:

- Embedded textures: importer provides `std::span<const std::byte>` plus an owner
  to keep the memory alive.
- File-backed textures: importer reads bytes via `IAsyncFileReader`.

A `WorkItem` must represent everything the legacy synchronous
`TextureImporter + CookTexture(...)` path can cook today, including single-source
textures, cubemap face sets, or pre-decoded `ScratchImage`.

```cpp
enum class FailurePolicy : uint8_t { kStrict, kPlaceholder };

struct SourceBytes {
  std::span<const std::byte> bytes;
  std::shared_ptr<const void> owner;  // Keeps bytes alive
};

using SourceContent = std::variant<SourceBytes, TextureSourceSet, ScratchImage>;

struct WorkItem {
  std::string source_id;     // Diagnostic id + decode extension hint

  // Optional file path used to resolve bytes via `IAsyncFileReader`. When the
  // `source` field already contains bytes or a decoded `ScratchImage`, leave
  // this empty. The pipeline will use the configured file reader when present.
  std::filesystem::path source_path;

  std::string texture_id;    // Canonical dedupe key (NormalizeTexturePathId /
                             // embedded:sha256)
  const void* source_key{};  // Opaque correlation (e.g. ufbx_texture*)
  TextureImportDesc desc;    // Existing type
  std::string packing_policy_id;  // "d3d12" | "tight"
  bool output_format_is_override = false; // True when config explicitly set
                                         // output format
  FailurePolicy failure_policy = FailurePolicy::kPlaceholder;
  bool equirect_to_cubemap = false;
  uint32_t cubemap_face_size = 0; // Required when equirect_to_cubemap = true
  CubeMapImageLayout cubemap_layout = CubeMapImageLayout::kUnknown;
  SourceContent source;

  // Optional callbacks invoked on the import thread when a worker begins and
  // ends processing this item. Useful for telemetry and tests.
  std::function<void()> on_started;
  std::function<void()> on_finished;

  std::stop_token stop_token;
};
```

Notes:

- The pipeline **must** assign `desc.source_id = source_id` and
  `desc.stop_token = stop_token` before calling the cooker.
- `SourceBytes.owner` must keep the bytes alive across any suspension; owners may
  be empty only when storage has a guaranteed static lifetime.
- `TextureSourceSet` owns its bytes; each `TextureSource.source_id` is used for
  extension hints and diagnostics. Cubemap faces use `CubeFace` ordering.
- `packing_policy_id` is resolved via `TexturePackingPolicy` helpers. Unknown
  IDs fall back to the default policy (D3D12 on Windows) and should emit a
  warning diagnostic.
- `texture_id` canonicalization must match sync import:
  - file textures: `NormalizeTexturePathId(resolved_path)`
  - embedded textures: `"embedded:" + sha256(bytes)`
- `equirect_to_cubemap` triggers HDR panorama → cubemap conversion (single
  source only). Requires `cubemap_face_size > 0`; the conversion operates on
  RGBA32F input and uses `desc.mip_filter` as the sampling filter.
- `cubemap_layout` enables layout extraction from a single image (auto/strip/
  cross). `kUnknown` disables layout extraction. If both equirect and layout
  flags are set, equirect conversion takes precedence.
- `output_format_is_override` should mirror whether the job explicitly
  overrides the output format.
- When `output_format_is_override == false`, the pipeline must preserve the
  decoded format (set `desc.output_format` to the decoded format and set
  `desc.bc7_quality = kNone`).

### WorkResult

```cpp
struct WorkResult {
  std::string source_id;
  std::string texture_id;
  const void* source_key{};

  std::optional<CookedTexturePayload> cooked;
  bool used_placeholder = false; // True -> caller should map to fallback texture index 0
  std::vector<ImportDiagnostic> diagnostics;

  // Per-item telemetry captured during pipeline execution (timings, IO).
  ImportWorkItemTelemetry telemetry;

  bool success = false;
};
```

Notes:

- `success` is `true` when a cooked payload is produced (placeholders count as
  success).
- `used_placeholder` is `true` when `failure_policy == kPlaceholder` and the
  pipeline generated a deterministic placeholder payload.
- `success == false` is reserved for strict errors or cancellation.
- Importer-specific diagnostics (e.g., `"fbx.texture_decode_failed"`) are owned
  by the job/orchestrator, not the pipeline.
- Job-level failure handling maps a failed texture to the error texture index
  (`std::numeric_limits<ResourceIndexT>::max()`) and continues the job.
- `ImportWorkItemTelemetry` captures per-item timings (including any observed
  IO durations) and is populated into `WorkResult.telemetry`.

---

## Public API (Pattern)

The TexturePipeline API mirrors `BufferPipeline` and the
**ResourcePipeline** concept described in `async_import_pipeline_v2.md`.

```cpp
class TexturePipeline final {
public:
  struct Config {
    size_t queue_capacity = 64;
    uint32_t worker_count = 4;
    bool with_content_hashing = true;

    // Optional async file reader for resolving WorkItem::source_path when
    // the submitted WorkItem does not already contain source bytes.
    observer_ptr<IAsyncFileReader> file_reader = {};

    // Optional callback to observe async I/O durations (microseconds).
    std::function<void(std::chrono::microseconds)> on_io_duration;
  };

  explicit TexturePipeline(co::ThreadPool& thread_pool, Config cfg = {});

  void Start(co::Nursery& nursery);

  // Note: `Start` must be called on the import thread; the nursery will own
  // the worker coroutines.

  [[nodiscard]] auto Submit(WorkItem item) -> co::Co<>;
  [[nodiscard]] auto TrySubmit(WorkItem item) -> bool;
  [[nodiscard]] auto Collect() -> co::Co<WorkResult>;

  void Close();

  [[nodiscard]] auto HasPending() const noexcept -> bool;
  [[nodiscard]] auto PendingCount() const noexcept -> size_t;
  [[nodiscard]] auto GetProgress() const noexcept -> PipelineProgress;

  // Queue introspection helpers (available on the import-thread):
  [[nodiscard]] auto InputQueueSize() const noexcept -> size_t;
  [[nodiscard]] auto InputQueueCapacity() const noexcept -> size_t;
  [[nodiscard]] auto OutputQueueSize() const noexcept -> size_t;
  [[nodiscard]] auto OutputQueueCapacity() const noexcept -> size_t;
};
```

---

## Worker Behavior

Workers run as coroutines on the import thread and drain the bounded input queue. The number of worker coroutines is controlled by `Config::worker_count` (default 4). CPU-heavy work is offloaded to the configured `co::ThreadPool`.

For each work item:

1) Invoke `WorkItem.on_started()` (if provided) on the import thread and check cancellation (`stop_token.stop_requested()`); if canceled, return `success=false` with no cooked payload.
2) Resolve packing policy via `TexturePackingPolicy` helpers.
3) If `WorkItem::source` does not contain bytes and `WorkItem::source_path` is set, attempt to resolve bytes using the configured `IAsyncFileReader` and report I/O latency via `Config::on_io_duration` when present.
4) Build a local `TextureImportDesc`:
   - copy from `WorkItem.desc`
   - set `source_id` and `stop_token`
5) Cook using the appropriate `CookTexture(...)` overload:
   - `SourceBytes`: decode with extension hints and, if
     `output_format_is_override == false`, preserve the decoded format before
     calling `CookTexture(ScratchImage&&, ...)`.
   - `TextureSourceSet`: decode all sources, verify matching dimensions/format,
     preserve the decoded format when `output_format_is_override == false`,
     assemble cubemaps, 2D arrays with pre-authored mips, or 3D depth slices.
   - `ScratchImage`: skip decode and cook directly.
6) On error:
   - If `failure_policy == kPlaceholder` and the error is **not** cancellation,
     return `success = false` with `used_placeholder = true`. The job/orchestrator
     maps this to fallback texture index `0`.
   - Otherwise, translate `TextureImportError` into `ImportDiagnostic` and
     return `success=false`.
7) Invoke `WorkItem.on_finished()` (if provided) on the import thread and send `WorkResult` to the output queue.

No exceptions cross coroutine boundaries; all failures are reported as data.

---

## Per-Job Orchestration (No Hidden Entities)

Within a job:

1) Discover texture sources (embedded blobs and/or file paths).
2) Build `TextureImportDesc` via presets (`DetectPresetFromFilename`,
   `TextureImportPresets`) and job-level tuning.
3) Resolve `texture_id` and deduplicate **before** submission (use
   `source_key` and `texture_id` maps for embedded/file textures).
4) Acquire bytes (the importer may either acquire bytes itself and populate `WorkItem::source`, or pass a `source_path` and let the pipeline resolve bytes via the configured `IAsyncFileReader`), build `WorkItem`, and `co_await texture_pipeline.Submit(item)` (bounded backpressure).
5) Collect results (`Collect()` or `HasPending()` loop):
   - On success: emit via `session.TextureEmitter().Emit(...)` (index is stable).
   - On `used_placeholder`: add warning diagnostics and map to the fallback
     texture index `0` (no payload emission).
   - On failure: add diagnostics and map to the error texture index
     (`std::numeric_limits<ResourceIndexT>::max()`).

---

## Pipeline Stages (Legacy Cooker Parity)

These stages follow the current synchronous implementation in
`TextureCooker.cpp`.

### Stage Summary

1) **Resolve packing policy**
   - Input: `WorkItem.packing_policy_id`
   - Output: `const ITexturePackingPolicy&`
   - Behavior: unknown IDs fall back to default policy and emit a warning

2) **Pre-decode validation**
   - Input: `TextureImportDesc`
   - Checks:
     - width/height both set or both zero
     - depth rules for non-3D textures
     - mip policy and max mip levels
     - HDR intent vs output format
     - BC7 quality vs output format

3) **Resolve source bytes (optional)**
   - Input: `WorkItem.source_path` + configured `IAsyncFileReader`
   - Behavior: when the submitted `WorkItem.source` does not already contain
     bytes or decoded images, the pipeline will attempt to read the file at
     `source_path` using the configured file reader. IO latency may be reported
     via `Config::on_io_duration`. This step is a read-only convenience for
     imports that submit file paths instead of pre-acquired bytes.

4) **Decode**
   - Input: bytes + `DecodeOptions`
   - Options:
     - `flip_y_on_decode`
     - `force_rgba_on_decode`
     - `extension_hint` from `source_id` or per-face `TextureSource.source_id`
   - Output: `ScratchImage` (RGBA8 or RGBA32Float)

5) **(Single-source only) Cubemap transforms**
   - **Equirectangular → cube**: decode to RGBA32F, validate ~2:1 aspect,
     convert to cubemap faces using `EquirectToCubeOptions` and
     `desc.mip_filter` as the sampling filter.
   - **Layout extraction**: when `cubemap_layout != kUnknown`, extract faces
     from a single layout image (auto/strip/cross). Validation includes layout
     detection and face size.

6) **(Multi-source only) Assemble subresources**
   - Input: decoded sources mapped by `SubresourceId`
   - Output: assembled `ScratchImage`
   - Cube maps: 6 faces mapped by `CubeFace`.
   - 2D arrays: assemble array layers and pre-authored mips (depth slice must
     be 0). All layers and mip levels must be present.
   - 3D textures: assemble depth slices (array_layer = 0, mip_level = 0) into
     a single 3D `ScratchImage` with contiguous depth slices.

7) **Post-decode validation**
   - Input: `TextureImportDesc` + decoded meta
   - Checks: non-zero dimensions, explicit dimension match, and
     `TextureImportDesc::Validate()` on resolved shape

8) **Convert to working format**
   - Currently pass-through (decoder outputs already in working formats)

9) **Apply content processing**
   - HDR handling:
     - `kTonemapAuto`: auto bake HDR→LDR (uses `exposure_ev`)
     - `kError`: only bake if `bake_hdr_to_ldr` is true, otherwise error later
     - `kKeepFloat`: force float output for HDR input (override to RGBA32F,
       disable BC7, and skip baking).
   - Normal maps: `intent == kNormalTS` → optional `flip_normal_green`

10) **Generate mips**
    - `MipPolicy::kNone`, `kFullChain`, or `kMaxCount`
    - Normal maps use specialized mips; 3D textures use 3D mip generator
    - `mip_filter` + `mip_filter_space` control filtering

11) **Convert to output format / compress**
    - Supported formats:
      - `Format::kRGBA8UNorm`, `Format::kRGBA8UNormSRGB`
      - `Format::kRGBA16Float`, `Format::kRGBA32Float`
      - `Format::kBC7UNorm`, `Format::kBC7UNormSRGB`
    - BC7 path converts float → RGBA8 first if needed
    - HDR → LDR mismatch yields `kHdrRequiresFloatFormat` unless baked earlier
      (except when `HdrHandling::kKeepFloat` overrides output to float)
    - sRGB reinterpretation: if storage is RGBA8/BC7, requested sRGB variant is
      preserved in the final descriptor

12) **Pack subresources**
    - Compute layouts via `ComputeSubresourceLayouts(meta, policy)`
    - **Ordering requirement**: layer-major (array layer outer, mip inner)
    - Use policy alignment for row pitch and subresource offsets

13) **Build final payload**
    - `TexturePayloadHeader` (28 bytes) + `SubresourceLayout[]` + aligned data
    - `data_offset_bytes = AlignSubresourceOffset(layouts_offset + layouts_bytes)`
    - `content_hash = detail::ComputeContentHash(payload)` computed on the
      ThreadPool **only when hashing is enabled** (first 8 bytes of SHA-256
      over the full payload)

14) **Return cooked result**
    - `CookedTexturePayload.desc` includes shape, mip count, final format,
      `packing_policy_id`, and `content_hash`
    - `payload` contains the complete PAK v4 payload
    - `layouts` mirrors the packed subresource layouts

### Cancellation Observability

`TextureImportDesc::stop_token` is checked throughout the cooker and the BC7
encoder. The pipeline must wire the job’s stop token into the descriptor.

---

## Cooked Output Contract (PAK v4)

The pipeline must produce payload bytes identical to the sync cooker.

### `textures.data` Payload Layout

```text
TexturePayloadHeader (28 bytes, magic "OTX1")
SubresourceLayout[subresource_count] (12 bytes each)
[padding to data_offset_bytes]
Subresource data (layer-major order)
```

- `subresource_count = array_layers * mip_levels`
- `layouts_offset_bytes` is typically `sizeof(TexturePayloadHeader)`
- `data_offset_bytes` is aligned via `policy.AlignSubresourceOffset(...)`

### `textures.table` Entry (Emitter Conversion)

`TextureEmitter::Emit()` converts `CookedTexturePayload` into
`data::pak::TextureResourceDesc` using its internal descriptor conversion:

- `compression_type = 7` for BC7, `0` otherwise
- `alignment = policy.AlignRowPitchBytes(1)` (256 for D3D12, 1 for tight)
- `size_bytes = payload.size()`
- `content_hash` comes from the payload header and is computed on the
  ThreadPool only when hashing is enabled

### Data File Alignment

Emitter offsets in `textures.data` are reserved with
`util::kRowPitchAlignment` (256 bytes) regardless of packing policy.

### Dedup Signature

`TextureEmitter` deduplicates by a stable signature derived from the emitted
`TextureResourceDesc` fields: `content_hash`, `width`, `height`, `mip_levels`,
`format`, `alignment`, and `size_bytes`.

### Placeholder Contract

Placeholder policy maps to the fallback texture index `0`. The fallback
payload is created by `TextureEmitter` and reused across all failures.

---

## Feature Parity With Legacy Synchronous Import

### Parity Owned by the Pipeline (Cooker Parity)

- Decode options: `flip_y_on_decode`, `force_rgba_on_decode`, extension hints
- Pre/post validation rules (dimension pairing, depth/type, mip policy, BC7)
- HDR handling: `kTonemapAuto`, `kError` behavior, `exposure_ev` usage
- Normal map handling: `flip_normal_green`, normal-map mip generation
- Mip policies and filters, including `mip_filter_space`
- Output formats: RGBA8 (linear/sRGB), RGBA16F, RGBA32F, BC7 (linear/sRGB)
- sRGB reinterpretation for RGBA8/BC7 when the storage is bit-identical
- Packing policy alignment and layer-major subresource ordering
- Payload header/layout format and `content_hash` calculation (SHA-256 first 8
  bytes) are performed on the ThreadPool **only when hashing is enabled**
- Multi-source cooking: cubemaps and 2D arrays with pre-authored mips
- Single-source cubemap transforms: equirect → cube and layout extraction
- Placeholder generation (when enabled) matches sync path exactly

### Parity Owned by the Importer/Orchestrator (Importer Parity)

- Preset auto-detection (`DetectPresetFromFilename`) and `TextureImportPresets`
- `TextureImportDesc` construction from presets and tuning
- File and embedded texture resolution:
  - `ResolveFileTexture`, `TextureIdString`, `NormalizeTexturePathId`
  - embedded `texture_id = "embedded:" + sha256(bytes)`
- Cubemap face discovery by naming convention and `TextureSourceSet.AddCubeFace`
- Use `FailurePolicy::kPlaceholder` for scene imports and emit warnings with the
  same diagnostic codes as the sync importer
- Dedup before submission via `texture_id`/`source_key` maps
  (job-scoped only; global dedupe is deferred to the packer)

### Known Async Gaps (Must Be Closed for Parity)

- (none for texture assembly; volume depth-slice assembly is supported).

---

## Separation of Concerns (AsyncImporter vs Pipeline vs Emitter vs Importers)

- `detail::AsyncImporter`:
  - owns the import-thread nursery and shared infrastructure
- Format importer (FBX/GLB/etc):
  - discovers textures, presets, and intent
  - resolves sources (embedded vs file-backed; cubemap face resolution)
  - builds `WorkItem`s (including `output_format_is_override`)
  - maps results back to materials and emits diagnostics
- `TexturePipeline`:
  - compute-only cook (`CookTexture(...)` overloads)
  - may perform async reads of `WorkItem::source_path` when configured with an `IAsyncFileReader` (reads only)
  - never writes output files and never calls emitters
- `TextureEmitter`:
  - ensures fallback index `0`
  - deduplicates by signature
  - writes `textures.data` and `textures.table`

### Single-writer rule

Only the job’s commit path mutates emission state:

- `TextureEmitter` is mutated only by the commit path.
- Importer-side maps (dedup, material readiness) are mutated only by the commit
  path.

---

## Cancellation Semantics

Cancellation semantics are defined in the parent design under **Cancellation
Design**. Texture-specific requirements:

- Pipelines do not provide a direct cancel API; cancellation is expressed by
  cancelling the job nursery and by checking per-item `stop_token` values.
- Wire `WorkItem.stop_token` into `TextureImportDesc::stop_token`.
- Cancellation returns `success=false` and **does not** emit placeholders.

---

## Backpressure and Memory Safety

The bounded input queue is the primary backpressure mechanism:

- Read bytes for one texture.
- Immediately `co_await pipeline.Submit(...)`.
- Only then proceed to the next texture.

---

## Progress Reporting

The pipeline is UI-agnostic.

- `GetProgress()` returns `PipelineProgress` (submitted/completed/failed/in_flight).
- Queue introspection helpers (`InputQueueSize`, `InputQueueCapacity`, `OutputQueueSize`, `OutputQueueCapacity`) are available on the import thread for realtime UI feedback.
- The job orchestrator converts pipeline progress into `ImportProgress`.
- Diagnostics are forwarded incrementally as results are collected.

---

## Robustness Rules (Do Not Violate)

1) Pipeline never writes output files.
2) Pipeline never calls `TextureEmitter`.
3) Only the job’s commit path mutates emission/dedup state.
4) All heavyweight work runs on `co::ThreadPool`.
5) Errors cross boundaries as data (`ImportDiagnostic`), not exceptions.
6) Bounded channels are mandatory.
7) The pipeline may perform file reads only when configured with an `IAsyncFileReader`; such IO must be read-only and limited to resolving `WorkItem::source_path`.
8) Placeholder policy maps to fallback texture index `0` (no payload emitted).
9) Payload headers/layouts must conform to PAK v4 (`TexturePayloadHeader`).

---

## See Also

- [async_import_pipeline_v2.md](async_import_pipeline_v2.md)
- [async_import_implementation_plan.md](async_import_implementation_plan.md)
- `src/Oxygen/Content/Import/Async/Pipelines/BufferPipeline.*` (pipeline pattern)
- `src/Oxygen/Content/Import/Async/Emitters/TextureEmitter.*` (I/O + indices)
- `src/Oxygen/Content/Import/TextureCooker.*` (sync cooking implementation)
