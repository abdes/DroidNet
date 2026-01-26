# Buffer Pipeline (v2)

**Status:** Implemented (Reference)
**Date:** 2026-01-26
**Parent:** [async_import_pipeline_v2.md](async_import_pipeline_v2.md)

---

## Overview

This document describes the **BufferPipeline** as implemented in
`src/Oxygen/Content/Import/Internal/Pipelines/BufferPipeline.*`.
The pipeline is compute-only and is responsible for optional SHA‑256 based
`content_hash` computation for buffer payloads used by async imports.

Core properties:

- **Compute‑only**: no I/O, no resource index assignment (`BufferEmitter` does
  emission/deduplication and file writes).
- **Lifetime**: created and started by the importer; workers are started in a
  provided `co::Nursery` and must be started on the import thread via
  `Start(co::Nursery&)`.
- **ThreadPool offload**: hashing (when enabled) is offloaded to a shared
  `co::ThreadPool`.
- **Configurable hashing**: controlled by `BufferPipeline::Config::with_content_hashing`;
  when disabled the pipeline does not touch `content_hash`.
- **Bounded queues**: input/output queues have a bounded capacity (`Config::queue_capacity`);
  `Submit` may suspend when full, `TrySubmit` is provided for non-blocking submits.

---

## Alignment With Current Architecture

Refer to [design/async_import_pipeline_v2.md](design/async_import_pipeline_v2.md)
for the canonical concurrency and lifetime model. The `BufferPipeline` follows
that pattern: producers submit `WorkItem`s, worker coroutines run on the
import thread and offload CPU-bound hashing to the `co::ThreadPool`.

### Pipeline vs Emitter Responsibilities

- **BufferPipeline**: compute-only post-processing (optional `content_hash`).
- **BufferEmitter**: deduplication, stable index assignment and writing
  `buffers.data` / `buffers.table`.

---

## Data Model

### WorkItem

```cpp
struct WorkItem {
  std::string source_id;     // Correlation id (mesh/buffer name)
  CookedBufferPayload cooked; // Buffer payload

  // Optional lifecycle callbacks executed on the import thread.
  std::function<void()> on_started;
  std::function<void()> on_finished;

  std::stop_token stop_token; // Cancellation token checked during processing
};
```

Notes:

- `cooked.content_hash` may be zero on input and will be populated only when
  hashing is enabled and the value is zero.
- The planner/orchestrator must submit only finalized payloads.

### WorkResult

```cpp
struct WorkResult {
  std::string source_id;               // Echoed from WorkItem
  CookedBufferPayload cooked;          // May have `content_hash` filled
  std::vector<ImportDiagnostic> diagnostics;
  ImportWorkItemTelemetry telemetry;   // Per-item telemetry captured by pipeline
  bool success = false;
};
```

Notes:

- `success == false` indicates the item was canceled or failed.
- Any errors during processing are surfaced as `ImportDiagnostic` entries.

---

## Public API

The implementation exposes the following API (high-level):

```cpp
struct Config { size_t queue_capacity = 64; uint32_t worker_count = 2; bool with_content_hashing = true; };
explicit BufferPipeline(co::ThreadPool& thread_pool, Config config = {});
void Start(co::Nursery& nursery);                       // Must be called on import thread
auto Submit(WorkItem item) -> co::Co<>;                 // Suspends when input queue is full
auto TrySubmit(WorkItem item) -> bool;                  // Non-blocking submit
auto Collect() -> co::Co<WorkResult>;                   // Suspends until a result is available
void Close();                                           // Close input queue; does not cancel running ThreadPool tasks
auto HasPending() const noexcept -> bool;               // Any submitted work not yet collected
auto PendingCount() const noexcept -> size_t;           // Number of items not yet collected
auto GetProgress() const noexcept -> PipelineProgress;  // Counts: submitted / completed / failed / pending
auto InputQueueSize() const noexcept -> size_t;         // Current queued inputs
auto OutputQueueSize() const noexcept -> size_t;        // Current queued results
```

---

## Worker Behavior

- Workers are coroutines running on the import thread that drain the bounded
  input queue.
- For each `WorkItem`:
  1. Invoke `on_started()` (if provided) on the import thread.
  2. If the item was cancelled (`stop_token`), short-circuit and return a
     `WorkResult` with `success = false`.
  3. If `Config::with_content_hashing` is disabled, forward the payload
     unchanged (pipeline does not modify `content_hash`).
  4. If `cooked.content_hash != 0`, skip hashing.
  5. Otherwise, offload SHA-256 computation to the shared `co::ThreadPool`.
     The pipeline stores the first 8 bytes of the digest into
     `CookedBufferPayload::content_hash` on success. The ThreadPool task can
     return an `ImportDiagnostic` on failure which is propagated in the
     `WorkResult`.
  6. Capture per-item telemetry and diagnostics, invoke `on_finished()` and
     enqueue a `WorkResult` for collection.

- All errors are converted to `ImportDiagnostic`; exceptions are not allowed
  to propagate across async boundaries.

---

## Cooked Output Contract

- `content_hash` (first 8 bytes of SHA‑256) is computed on the `co::ThreadPool` when
  `Config::with_content_hashing` is enabled and the incoming `content_hash` is zero.
- The pipeline never modifies buffer bytes or metadata other than potentially
  `content_hash` and it preserves other `CookedBufferPayload` fields.

---

## Cancellation Semantics

- The pipeline does not provide a direct cancel API; cancel the job nursery and
  provide per-item `stop_token`s.
- `Close()` closes the input queue and allows workers to drain, but it does
  **not** cancel ThreadPool tasks that are already running. A canceled work
  item results in `success = false`.

---

## Progress & Telemetry

- The pipeline maintains atomic counters for `submitted`, `completed`,
  `failed`, and `pending` items and exposes them via `GetProgress()`.
- Per-item telemetry is returned in `WorkResult::telemetry` and the pipeline
  supports `InputQueueSize`/`OutputQueueSize` and capacity introspection.

---

## Implementation Notes

- The class is non-copyable and non-movable and is constructed with a
  reference to a shared `co::ThreadPool`.
- Internal helper coroutines include `Worker()`, `ComputeContentHash(...)`,
  and `ReportCancelled(...)` (implementation details are in the source files).

---

## See Also

- `src/Oxygen/Content/Import/Internal/Pipelines/BufferPipeline.h`
- `src/Oxygen/Content/Import/BufferImportTypes.h`
- `texture_work_pipeline_v2.md` and other pipeline docs for the general pattern
