# Texture Pipeline (v2)

**Status:** Updated Design (Phase 5)
**Date:** 2026-01-15
**Parent:** [async_import_pipeline_v2.md](async_import_pipeline_v2.md)

---

## Overview

This document specifies the **TexturePipeline** used by async imports to
parallelize texture cooking within **a single import job**.

The TexturePipeline is a **compute-only** component:

- Performs **decode → content processing → mip generation → format conversion/compression → packing**.
- Does **not** write cooked output files.
- Does **not** assign resource indices.
- Returns `CookedTexturePayload` to the caller, who commits via
  `ImportSession::TextureEmitter()`.

The concrete stage graph and the inputs/outputs of each stage are specified in
the section **Pipeline Stages (Legacy Cooker Parity)**.

The pipeline follows the established pipeline patterns already implemented in
code (see `BufferPipeline`):

- bounded input/output queues (`co::Channel`)
- explicit `Start(nursery)` lifecycle
- `Submit()` / `TrySubmit()` with backpressure
- `Collect()` to retrieve results
- cooperative cancellation via `std::stop_token`
- explicit error reporting via `ImportDiagnostic` (no exceptions crossing async boundaries)

---

## Alignment With Current Architecture

The pipeline-agnostic concurrency and lifetime model is defined in the parent
document [design/async_import_pipeline_v2.md](design/async_import_pipeline_v2.md)
under **Concurrency, Ownership, and Lifetime (Definitive)**.

This texture design assumes and follows that model (pipelines are job-scoped,
started in the job’s child nursery; job orchestration acquires sources, submits,
collects, and commits via emitters).

### Pipeline vs Emitter Responsibilities

- **TexturePipeline**: produces `CookedTexturePayload` (compute-only).
- **TextureEmitter**: assigns stable indices immediately and performs all async
  I/O (`WriteAt*`) to `textures.data` and `textures.table`.
- **ImportSession::Finalize()**: waits for emitter I/O, writes tables, then writes
  `container.index.bin` **last**.

---

## Data Model

### WorkItem

The pipeline operates on source bytes already in memory.

**Source acquisition is not the pipeline’s job**:

- Embedded textures: the importer provides a `std::span<const std::byte>` plus an
  owner to keep the memory alive.
- File-backed textures: the importer reads bytes via `IAsyncFileReader` and then
  submits the bytes to the pipeline.

Rationale:

- Keeps the pipeline compute-only.
- Lets the bounded input channel provide backpressure without turning the
  pipeline into an I/O dispatcher.

A `WorkItem` must be able to represent everything the legacy synchronous
`TextureImporter` + `CookTexture(...)` path can cook today.

That includes:

- single-source textures from one image file / embedded blob
- multi-source cube maps (6 faces), where the bytes are already in memory

A minimal `WorkItem` shape that preserves legacy feature parity:

```cpp
struct SingleSourceBytes {
  std::span<const std::byte> bytes;
  std::shared_ptr<const void> bytes_owner;
};

struct CubeSourceBytes {
  // The cooker needs per-face source_id for extension hints and diagnostics.
  // Face ordering uses the same convention as TextureSourceSet/CubeFace.
  std::array<std::string, 6> face_source_ids;
  std::array<std::span<const std::byte>, 6> face_bytes;
  std::array<std::shared_ptr<const void>, 6> face_bytes_owner;
};

struct WorkItem {
  // Correlation keys for the caller.
  std::string source_id;      // e.g. "mat:Foo/baseColor"
  std::string texture_id;     // normalized identifier (for dedup maps upstream)
  const void* source_key{};   // optional opaque identity (e.g. ufbx_texture*)

  // Cooking configuration.
  TextureImportDesc desc;
  std::string packing_policy_id; // e.g. "d3d12"; resolved by pipeline

  // Source content (compute-only; bytes are already in memory).
  std::variant<SingleSourceBytes, CubeSourceBytes, ScratchImage> source;

  // Cooperative cancellation.
  std::stop_token stop_token;
};
```

Notes:

- The pipeline MUST ensure cooperative cancellation reaches the cooker by
  setting `desc.stop_token = stop_token` before calling `CookTexture(...)`.
- For `SingleSourceBytes` and `CubeSourceBytes`, the owning handles are required
  to keep the spans alive across coroutine suspension and ThreadPool execution.
- **Invariant:** every `std::span<const std::byte>` in the work item must point
  into memory whose lifetime is anchored by the corresponding `*_owner` for the
  entire duration from `Submit()` until the worker has finished cooking
  (including any `co_await` and ThreadPool execution). Owners may be empty only
  when the referenced storage is independently guaranteed to outlive pipeline
  processing (e.g., static read-only data).
- `source_key` is carried through only to help the caller map results back to its
  own data structures; the pipeline never interprets it.

### WorkResult

```cpp
struct WorkResult {
  std::string source_id;
  std::string texture_id;
  const void* source_key{};

  std::optional<CookedTexturePayload> cooked;
  std::vector<ImportDiagnostic> diagnostics;
  bool success = false;
};
```

---

## Public API (Pattern)

The TexturePipeline API mirrors `BufferPipeline`.

```cpp
class TexturePipeline final {
public:
  struct Config {
    size_t queue_capacity = 64;
    uint32_t worker_count = 2;
  };

  explicit TexturePipeline(co::ThreadPool& thread_pool, Config cfg = {});

  void Start(co::Nursery& nursery);

  [[nodiscard]] auto Submit(WorkItem item) -> co::Co<>;
  [[nodiscard]] auto TrySubmit(WorkItem item) -> bool;
  [[nodiscard]] auto Collect() -> co::Co<WorkResult>;

  void Close();
  void Cancel();

  [[nodiscard]] auto HasPending() const noexcept -> bool;
  [[nodiscard]] auto PendingCount() const noexcept -> size_t;

private:
  [[nodiscard]] auto Worker() -> co::Co<>;
};
```

---

## Worker Behavior

Workers run as coroutines on the import thread, draining the bounded input queue.
Each item is processed as:

1) Check cancellation (`stop_token.stop_requested()`).
2) Resolve packing policy (by ID).
3) Offload expensive cooking to `co::ThreadPool`.
4) Convert errors to `ImportDiagnostic`.
5) Send `WorkResult` to the bounded output queue.

The pipeline must reuse existing synchronous cooking logic:

- `CookTexture(std::span<const std::byte>, const TextureImportDesc&, const ITexturePackingPolicy&)`
- `CookTexture(ScratchImage&&, const TextureImportDesc&, const ITexturePackingPolicy&)`
- `CookTexture(const TextureSourceSet&, const TextureImportDesc&, const ITexturePackingPolicy&)`

The ThreadPool boundary should look like:

```cpp
auto cooked_or_error = co_await thread_pool_.Run([bytes, desc, &policy]() {
  return CookTexture(bytes, desc, policy);
});
```

No exceptions should cross coroutine boundaries; failures must be reported as
`ImportDiagnostic` + `success=false`.

---

## Per-Job Orchestration (No Hidden Entities)

This section replaces the vague “submitter/collector” terminology with a
concrete description of what runs where.

### What runs in the pipeline vs in the job

- **TexturePipeline workers**: job-scoped coroutines started in the job’s child
  nursery. They drain the pipeline’s input channel and offload cooking to the
  thread pool.
- **Job orchestration**: the concrete job’s implementation (e.g.
  `ImportJob::ExecuteAsync()` in a derived job), running on the import thread as
  part of processing one job.

### Minimal orchestration algorithm

Within one job, the orchestrator does:

1) Discover texture sources (embedded blobs and/or file paths).
2) For each source:
   - Acquire bytes (embedded: already in memory; file-backed: `co_await` reader).
   - Build `WorkItem` (`desc` + `packing_policy_id` + bytes owners + cancellation).
   - `co_await texture_pipeline.Submit(item)`.
   - Increment `submitted_count`.
3) After submission completes (or stops due to cancellation), collect exactly
   `submitted_count` results:
   - `auto r = co_await texture_pipeline.Collect()`.
   - On success: commit via `session.TextureEmitter().Emit(...)`.
   - On failure: record diagnostics.

This achieves concurrency because cooking is parallelized by the pipeline
workers and the thread pool; the job itself remains simple and deterministic.

### Cancellation semantics (job-safe)

- Per-job cancellation is expressed by cancelling the job’s child nursery.
- Work items still carry cancellation via `WorkItem.stop_token` so the cooker
  observes cancellation via `TextureImportDesc::stop_token`.
- `TexturePipeline::Cancel()` may be used for early-abort decisions inside the
  job, but the normal cancellation mechanism is nursery cancellation.

## Pipeline Stages (Legacy Cooker Parity)

This section documents what “cooking” concretely means today, based on the
current synchronous implementation in `CookTexture(...)`.

Important scoping notes:

- The **pipeline** orchestrates these stages but remains compute-only.
- The **ThreadPool boundary** is an implementation detail; conceptually, the
  stages below happen in order. In practice, the pipeline may call one of the
  `CookTexture(...)` overloads on the pool, which performs the full sequence.

### Stage Summary

Each stage has explicit inputs/outputs; failures are returned as a
`TextureImportError` which must be translated to `ImportDiagnostic`.

1) **Resolve packing policy**
   - Input: `WorkItem.packing_policy_id`
   - Output: `const ITexturePackingPolicy&` (e.g. D3D12, TightPacked)
   - Failure: unknown policy id

2) **Pre-decode validation**
   - Input: `TextureImportDesc`
   - Output: ok or `TextureImportError`
   - Checks: dimension rules, depth/type rules, mip policy, HDR vs output format,
     BC7 config consistency

3) **Decode** (single-source and multi-source)
   - Input: image bytes + decode options
   - Output: `ScratchImage` (working formats currently produced by decoder)
   - Decode options:
     - `flip_y_on_decode`
     - `force_rgba_on_decode`
     - `extension_hint` derived from `source_id` / face `source_id`

4) **(Multi-source only) Assemble cube**
   - Input: 6 decoded face `ScratchImage`s + face mapping
   - Output: one `ScratchImage` representing a cubemap
   - Current parity note: multi-source cooking currently supports **cubemaps
     only**; non-cube arrays are not yet implemented.

5) **Post-decode validation**
   - Input: `TextureImportDesc` + decoded/assembled `ScratchImageMeta`
   - Output: ok or `TextureImportError`
   - Checks: decoded dimensions non-zero, descriptor dimensions match if
     explicitly provided, and `TextureImportDesc::Validate()` on resolved shape

6) **Convert to working format**
   - Input: `ScratchImage`
   - Output: `ScratchImage` in a processing-friendly format
   - Current implementation note: this is mostly a pass-through today because
     decoding already yields RGBA8 or RGBA32Float.

7) **Apply content processing**
   - Input: working `ScratchImage` + `TextureImportDesc`
   - Output: processed `ScratchImage`
   - HDR handling:
     - if HDR input and LDR output: `hdr_handling` selects auto tonemap vs error
       behavior; `exposure_ev` is applied when tonemapping
     - `bake_hdr_to_ldr` is honored for explicit baking
   - Normal maps:
     - `intent == kNormalTS`: optional `flip_normal_green`

8) **Generate mips**
   - Input: processed `ScratchImage` + mip settings
   - Output: `ScratchImage` with mip chain
   - Behavior:
     - `MipPolicy::kNone`: keep single mip
     - `MipPolicy::kFullChain`: down to 1x1
     - `MipPolicy::kMaxCount`: capped by `max_mip_levels`
     - normal maps use a specialized mip generator and may renormalize per mip
     - 3D textures use a 3D mip generator
     - `mip_filter` + `mip_filter_space` control filtering

9) **Convert to output format / compress**
   - Input: mipmapped `ScratchImage` + output settings
   - Output: `ScratchImage` in the final stored format
   - Supported output formats today:
     - `Format::kRGBA8UNorm`, `Format::kRGBA8UNormSRGB`
     - `Format::kRGBA16Float`, `Format::kRGBA32Float`
     - `Format::kBC7UNorm`, `Format::kBC7UNormSRGB` (requires `bc7_quality != kNone`)
   - HDR → LDR mismatch behavior:
     - If HDR content remains and output is LDR, the cook fails with
       `kHdrRequiresFloatFormat` unless HDR was baked earlier.

10) **Pack subresources**
   - Input: output-format `ScratchImage` + packing policy
   - Output:
     - `payload_data` (`std::vector<std::byte>`) containing the packed data region
     - `layouts` describing each subresource’s offset/row pitch/size
   - Ordering requirement (parity-critical): layer-major ordering
     (array layer outer, mip inner) to match D3D12 subresource indexing.

11) **Build final payload**
   - Input: layouts + packed data + packing policy id
   - Output: `CookedTexturePayload.payload` containing:
     - `TexturePayloadHeader`
     - layouts table
     - aligned data region
   - `content_hash` is computed over the final payload bytes using the current
     `detail::ComputeContentHash(...)` implementation.

12) **Return cooked result**
   - Output: `CookedTexturePayload`:
     - `desc` (shape, mip count, final format, packing policy id, content hash)
     - `payload` (complete cooked blob)
     - `layouts` (subresource layouts for runtime)

### Cancellation Observability

The synchronous cooker checks `TextureImportDesc::stop_token` at multiple stages
and the BC7 encoder honors it. The pipeline must wire cancellation through by
copying the `WorkItem.stop_token` into the `TextureImportDesc` passed to the
cooker.

---

## Feature Parity With Legacy Synchronous Import

This pipeline design is feature-parity aligned with the existing synchronous
import path, by splitting responsibilities the same way the code already does.

### Parity Owned by the Pipeline (Cooker Parity)

The following behaviors are implemented by `CookTexture(...)` and therefore must
be preserved by the pipeline by calling the appropriate overload based on the
work item:

- Decode options: `flip_y_on_decode`, `force_rgba_on_decode`, extension hint via `source_id`.
- HDR handling: `hdr_handling`, `bake_hdr_to_ldr`, `exposure_ev`.
- Normal map handling: green channel flip, normal-map mip generation.
- Mip policy: none/full/max count + filter selection + filter color space.
- Output formats: RGBA8 (linear/sRGB), RGBA16F, RGBA32F, BC7 (linear/sRGB) with quality tiers.
- Packing: packing policy selection affects layout and alignment; payload header includes policy id.
- Deterministic payload layout: layer-major ordering for subresources.
- Content hashing: `content_hash` computed from final payload bytes.
- Multi-source cooking: cubemap cooking from 6 faces is supported; non-cube arrays are currently unsupported (matching the legacy cooker’s current behavior).

### Parity Owned by the Importer/Orchestrator (Importer Parity)

The following behaviors are part of the legacy synchronous `TextureImporter` and
remain outside the pipeline (still required for end-to-end parity):

- Preset auto-detection from filename (`DetectPresetFromFilename`).
- File I/O: read bytes from disk.
- Cubemap face discovery by naming convention (px/nx/..., posx/negx/..., right/left/...).
  This is **texture-domain** logic (Skybox is not optional) and must be reusable
  by standalone texture import as well as any scene importer (FBX/GLB/etc).
  It stays out of the pipeline because it requires path/policy decisions and
  existence checks.
- Construction of the correct `TextureImportDesc` (including intent, color space, mip policy, output format).

In the async design:

- **Orchestrator (acquire + submit)** performs detection/discovery and acquires
  bytes (reader or embedded), then submits work items to the pipeline.
- **TexturePipeline** cooks (compute-only) and returns `CookedTexturePayload`.
- **Commit path (collect + emit)** collects results and emits cooked textures via
  the `TextureEmitter`.

These are roles within the job’s orchestration code, not additional framework
classes.

Implementation strategy (no class explosion):

- Keep cubemap face discovery as a small, reusable helper in the texture import
  module (today it exists as an internal helper in the synchronous
  `TextureImporter`; Phase 5 should make it shareable so both standalone texture
  imports and scene importers can call the same logic).
- The orchestrator uses that helper to resolve the 6 face sources, reads those
  bytes via `IAsyncFileReader`, then submits a `CubeSourceBytes` work item.

---

## Separation of Concerns (AsyncImporter vs Pipeline vs Emitter vs Importers)

This design keeps responsibilities aligned with existing code boundaries.

- `detail::AsyncImporter` (LiveObject):
  - owns the long-lived nursery on the import thread
  - runs the job loop and creates a child nursery per job
  - provides shared infrastructure (ThreadPool, async I/O drivers)
  - calls into format importers
- Format importer (FBX/GLB/etc):
  - discovers textures and their semantics (intent/preset)
  - resolves sources (embedded vs file-backed; cubemap face resolution)
  - builds `TextureImportDesc` and `WorkItem`s
  - maps results back to the scene/material graph
- `TexturePipeline`:
  - compute-only cook (calls `CookTexture(...)` on the thread pool)
  - never performs file I/O
  - never calls emitters
- `TextureEmitter`:
  - assigns stable indices
  - performs all output writes (`textures.data`, `textures.table`)
  - is mutated from the import thread only (single-writer rule)

### Single-writer rule

Only the job’s commit path mutates emission state:

- `TextureEmitter` is mutated only by the commit path.
- Any importer-side maps (dedup indices, material readiness, etc.) are mutated
  only by the commit path.

This mirrors the design goal: no locks needed for commit state.

---

## Cancellation Semantics

Canonical cancellation behavior (job-safe pipeline usage, draining requirements,
and shutdown semantics) is specified in the parent design
[design/async_import_pipeline_v2.md](design/async_import_pipeline_v2.md) under
**Cancellation Design**.

Texture-specific requirement: the pipeline must wire `WorkItem.stop_token` into
`TextureImportDesc::stop_token` so the synchronous cooker and BC7 encoder can
observe cancellation.

---

## Backpressure and Memory Safety

The bounded input queue is the primary backpressure mechanism.

Correct usage pattern:

- Read bytes for one texture.
- Immediately `co_await pipeline.Submit(...)`.
- Only then proceed to read the next texture.

This prevents unbounded accumulation of large in-memory texture buffers.

---

## Progress Reporting

The pipeline remains UI-agnostic.

- The pipeline tracks internal counts (submitted/completed/failed).
- The job orchestrator converts that to `ImportProgress` and invokes the
  `ImportProgressCallback`.
- Diagnostics should be forwarded incrementally as results are collected.

---

## Robustness Rules (Do Not Violate)

1) Pipeline never writes output files.
2) Pipeline never calls `TextureEmitter`.
3) Only the job’s commit path mutates emission/dedup state.
4) All heavyweight work runs on `co::ThreadPool`.
5) Errors cross boundaries as data (`ImportDiagnostic`), not exceptions.
6) Bounded channels are mandatory.

---

## Integration Checklist (Phase 5)

- [ ] Implement `src/Oxygen/Content/Import/Async/Pipelines/TexturePipeline.h/.cpp`
- [ ] Add unit tests: `Submit/Collect`, cancellation, and error reporting
- [ ] Provide a packing-policy resolver by ID (e.g. `"d3d12"`)
- [ ] Integrate into `FbxImporter::ImportAsync` with orchestrator + commit path
- [ ] Keep `ImportSession::Finalize()` as the durability boundary

---

## See Also

- [async_import_pipeline_v2.md](async_import_pipeline_v2.md)
- [async_import_implementation_plan.md](async_import_implementation_plan.md)
- `src/Oxygen/Content/Import/Async/Pipelines/BufferPipeline.*` (pipeline pattern)
- `src/Oxygen/Content/Import/Async/Emitters/TextureEmitter.*` (I/O + indices)
- `src/Oxygen/Content/Import/TextureCooker.*` (sync cooking implementation)
