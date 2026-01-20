# Buffer Pipeline (v2)

**Status:** Proposed Design (Phase 5)
**Date:** 2026-01-16
**Parent:** [async_import_pipeline_v2.md](async_import_pipeline_v2.md)

---

## Overview

This document specifies the **BufferPipeline** used by async imports to perform
CPU‑bound buffer post‑processing. The pipeline is compute‑only and currently
responsible for computing `content_hash` for buffer payloads.

Core properties:

- **Compute‑only**: no I/O, no resource index assignment.
- **Job‑scoped**: created per job and started in the job’s child nursery.
- **ThreadPool offload**: `content_hash` is computed on `co::ThreadPool` only.
- **Configurable hashing**: hashing is optional and is controlled by
  `ImportOptions::with_content_hashing`, cascading into the pipeline config.
  When disabled, the pipeline MUST NOT compute hashes.
- **Planner‑gated**: work is submitted only after dependencies are ready and the
  full buffer payload is known.

---

## Alignment With Current Architecture

The pipeline-agnostic concurrency and lifetime model is defined in the parent
[design/async_import_pipeline_v2.md](design/async_import_pipeline_v2.md) under
**Concurrency, Ownership, and Lifetime (Definitive)**.

### Pipeline vs Emitter Responsibilities

- **BufferPipeline**: computes `content_hash` for `CookedBufferPayload` when
  requested.
- **BufferEmitter**: assigns stable buffer indices and writes `buffers.data` /
  `buffers.table`.

---

## Data Model

### WorkItem

```cpp
struct WorkItem {
  std::string source_id;     // Correlation id (mesh/buffer name)
  CookedBufferPayload cooked; // Buffer payload
  std::stop_token stop_token;
};
```

Notes:

- `cooked.content_hash` may be zero on input and will be filled when hashing is
  enabled.
- The planner must ensure the payload is final before submission.

### WorkResult

```cpp
struct WorkResult {
  std::string source_id;               // Echoed from WorkItem
  CookedBufferPayload cooked;          // Possibly with content_hash filled
  std::vector<ImportDiagnostic> diagnostics;
  bool success = false;
};
```

Notes:

- `success == false` indicates cancellation or failure.
- Any diagnostics are returned in `diagnostics` (currently unused for hashing
  failures, which should be rare).

---

## Public API (Pattern)

```cpp
class BufferPipeline final {
public:
  struct Config {
    size_t queue_capacity = 64;
    uint32_t worker_count = 2;
    bool with_content_hashing = true;
  };

  explicit BufferPipeline(co::ThreadPool& thread_pool, Config cfg = {});

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

Workers run as coroutines on the import thread and drain the bounded input
queue. For each work item:

1) Check cancellation; if cancelled, return `success=false`.
2) If hashing is disabled, forward the payload unchanged.
3) If `content_hash` is already non‑zero, do not recompute.
4) Otherwise, compute `content_hash` on the ThreadPool using SHA‑256 and store
   the first 8 bytes in `CookedBufferPayload::content_hash`.
5) Return `WorkResult`.

All errors must be converted to `ImportDiagnostic`; no exceptions cross async
boundaries.

---

## Cooked Output Contract

- `content_hash` is **always** computed on the ThreadPool.
- `content_hash` is computed **only** after the buffer payload is finalized by
  upstream stages.
- The pipeline never modifies buffer bytes or metadata other than
  `content_hash`.

---

## Separation of Concerns

- **BufferPipeline**: compute‑only; no I/O.
- **BufferEmitter**: assigns indices and writes data files.
- **Planner/Orchestrator**: decides when buffer payloads are ready and submits
  work accordingly.

---

## Cancellation Semantics

- Cancellation is expressed via the job nursery and `stop_token`.
- A cancelled work item returns `success=false` with no additional side effects.

---

## Progress Reporting

Pipeline tracks submitted/completed/failed counts and exposes
`PipelineProgress`.
