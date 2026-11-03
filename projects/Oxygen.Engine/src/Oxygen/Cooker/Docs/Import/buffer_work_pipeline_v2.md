# Buffer Pipeline (v2)

**Status:** Implemented (Reference)
**Date:** 2026-01-26
**Parent:** [async_import_pipeline_v2.md](async_import_pipeline_v2.md)

---

## Overview

This document describes the **BufferPipeline** as implemented in
`src/Oxygen/Cooker/Import/Internal/Pipelines/BufferPipeline.*`.
The pipeline is compute-only and is responsible for the SHA‑256
SHA‑256 computation for buffer payloads used by async imports.

Core properties:

- **Compute‑only**: no I/O, no resource index assignment (`BufferEmitter` does
  emission/deduplication and file writes).
- **Lifetime**: created and started by the importer; workers are started in a
  provided `co::Nursery` and must be started on the import thread via
  `Start(co::Nursery&)`.
- **ThreadPool offload**: hashing is offloaded to a shared `co::ThreadPool`.
- **Always hashed**: the pipeline hashes the bytes of every item into
  `cooked.data`, a `HashedBytes` (`Cooker/Import/HashedBytes.h`): the bytes plus
  their SHA‑256, computed when constructed and immutable after. No import reads
  a data file again to hash it.
- **Bounded queues**: input/output queues have a bounded capacity (`Config::queue_capacity`);
  `Submit` may suspend when full, `TrySubmit` is provided for non-blocking submits.

---

## Alignment With Current Architecture

Refer to [design/async_import_pipeline_v2.md](design/async_import_pipeline_v2.md)
for the canonical concurrency and lifetime model. The `BufferPipeline` follows
that pattern: producers submit `WorkItem`s, worker coroutines run on the
import thread and offload CPU-bound hashing to the `co::ThreadPool`.

### Pipeline vs Emitter Responsibilities

- **BufferPipeline**: compute-only post-processing (SHA‑256 of the payload).
- **BufferEmitter**: deduplication, stable index assignment and writing
  `buffers.data` / `buffers.table`. It refuses an empty payload
  (`std::invalid_argument`); the buffer container job refuses an empty buffer
  source with `buffer.container.source_empty`.
- **Deduplication**: a buffer reuses an existing table row only when every
  field of the row but its offset matches (usage flags, stride, format, size,
  SHA‑256) and the existing row's offset satisfies the requested alignment;
  otherwise it gets a new row at an aligned offset.
- **Buffer table**: written when the last import session of the cooked root's
  cohort ends.

---

## Data Model

### WorkItem

```cpp
struct WorkItem {
  std::string source_id;          // Correlation id (mesh/buffer name)
  std::vector<std::byte> bytes;   // Buffer bytes to hash
  CookedBufferPayload cooked;     // Metadata; `data` is empty

  // Optional lifecycle callbacks executed on the import thread.
  std::function<void()> on_started;
  std::function<void()> on_finished;

  std::stop_token stop_token; // Cancellation token checked during processing
};
```

Notes:

- `cooked` carries metadata only; the pipeline hashes `bytes` on the thread
  pool into `cooked.data`.
- The planner/orchestrator must submit only finalized payloads.

### WorkResult

```cpp
struct WorkResult {
  std::string source_id;               // Echoed from WorkItem
  CookedBufferPayload cooked;          // `data` filled on success
  std::vector<ImportDiagnostic> diagnostics;
  ImportWorkItemTelemetry telemetry;   // Per-item telemetry captured by pipeline
  bool success = false;
};
```

Notes:

- `success == false` indicates the item was canceled or failed; a cancelled
  item has an empty `cooked.data`.
- Any errors during processing are surfaced as `ImportDiagnostic` entries.

---

## Public API

The implementation exposes the following API (high-level):

```cpp
struct Config { size_t queue_capacity = 64; uint32_t worker_count = 2; };
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
  3. Offload SHA-256 computation over `bytes` to the shared
     `co::ThreadPool`. The pipeline stores the bytes and digest into
     `CookedBufferPayload::data` on success. A failure is reported as
     an `ImportDiagnostic` propagated in the `WorkResult`.
  4. Capture per-item telemetry and diagnostics, invoke `on_finished()` and
     enqueue a `WorkResult` for collection.

- All errors are converted to `ImportDiagnostic`; exceptions are not allowed
  to propagate across async boundaries.

---

## Cooked Output Contract

- `CookedBufferPayload::data` (a `HashedBytes`: the buffer bytes and their
  SHA‑256) is built on the `co::ThreadPool` for every payload. The payload has
  no separate hash field.
- The pipeline never modifies buffer bytes and preserves the other
  `CookedBufferPayload` fields.

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

- `src/Oxygen/Cooker/Import/Internal/Pipelines/BufferPipeline.h`
- `src/Oxygen/Cooker/Import/BufferImportTypes.h`
- `texture_work_pipeline_v2.md` and other pipeline docs for the general pattern
