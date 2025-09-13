# Upload Module – Concise Overview

The Upload module provides a unified, deterministic path to stage and submit
buffer/texture uploads to the GPU. It centralizes footprint planning, staging
allocation, command recording, submission, and completion tracking behind a
small API that is renderer-internal and coroutine-friendly.

---

## What it does (at a glance)

- Plans copy footprints/regions: `UploadPlanner` (buffers, 2D/3D/cube).
- Allocates CPU-visible staging: `StagingProvider` (persistently mapped).
  - Default providers: `SingleBufferStaging` (pinned/per-op) and
    `RingBufferStaging` (partitioned, per-frame bump allocator; requires
    explicit alignment set by the caller, typically the element stride).
- Records and submits copy commands on a transfer- or graphics-queue.
- Tracks completion and stats via fence values: `UploadTracker`.
- Policy-driven alignment/limits: `UploadPolicy`.

Flow: Submit → Plan → Stage/Fill → Record copy → Submit → Register ticket →
RetireCompleted advances fence and recycles staging (future arena).

---

## API highlights

- Submit: `UploadCoordinator::Submit`, `SubmitMany` → `UploadTicket`.
- Wait: `IsComplete`, `TryGetResult`, `Await`, `AwaitAll`.
- Coroutines: `SubmitAsync`, `AwaitAsync`, `AwaitAllAsync`.
- Frame control: `Flush()` (ensure submission), `RetireCompleted()` (advance
  completed fence; staging recycling in future arena).
- Stats: `GetStats()` returns submitted/completed/in-flight and bytes.

See [`UploadCoordinator.h`](../Upload/UploadCoordinator.h) for the full API.

---

## Components

- **UploadCoordinator**: Orchestrates planning, staging, recording, submit,
  and tracking.
- **UploadPlanner**: Computes buffer coalescing and texture subresource
  regions with alignment from `UploadPolicy`.
- **StagingProvider**: Minimal contract used by the coordinator to obtain
  mapped CPU-visible staging regions and retire completed work.
  - Provided implementations:
    - `SingleBufferStaging`: single upload buffer with pinned or per-op map.
    - `RingBufferStaging`: single mapped upload buffer partitioned by
      frames-in-flight; per-partition bump allocator; owner calls
      `SetActivePartition(slot)` each frame.
  - Providers do NOT manage GPU device-local buffers or SRVs; subsystems like
    `TransformUploader` own destination buffers and bindless indices.
- **UploadTracker**: Fence-based ticketing with blocking and coroutine waits;
  collects lightweight stats and supports best-effort cancellation.
- **UploadPolicy**: Alignments (row/placement/buffer), batching thresholds
  scaffolding (for future splitting/coalescing).

---

## Minimal example

```cpp
// Synchronous buffer upload using a direct data view
std::vector<std::byte> data = /* fill */;
UploadRequest req = {
  .kind = UploadKind::kBuffer,
  .desc = UploadBufferDesc{ .dst = buffer, .size_bytes = data.size(), .dst_offset = 0 },
  .data = UploadDataView{ std::span<const std::byte>(data) },
  .debug_name = "MyBufferUpload",
};
auto t = upload.Submit(req);
upload.Flush();
auto r = upload.Await(t);
```

---

## Known limitations (current implementation)

These reflect the actual code paths and are targeted for iteration:

1) TransformUploader still builds upload requests via `RingUploadBuffer`
  (legacy path) instead of owning device-local buffers and using
  `StagingProvider` directly for staging. SRV/bindless ownership needs to
  move fully into `TransformUploader`.
2) Queue selection is fixed to a simple “prefer transfer, else graphics”
   strategy; it isn’t policy-driven nor integrated with renderer config.
3) Buffer uploads select a steady post-copy state from usage flags, but
   textures use minimal state handling (no per-usage steady-state selection).
4) Staging providers rely on the caller to select the active partition per
  frame (`RingBufferStaging::SetActivePartition`). There is no centralized
  frame-tick integration yet.
5) `RingBufferStaging` requires explicit alignment (no default). Callers must
  pass a power-of-two alignment (e.g., element stride for structured buffers).
6) Unit tests for provider behaviors and coordinator-provider integration are
  minimal.

Existing constraints retained by design:

- Submission is immediate; there’s no pending, thread-safe queue yet.
- Cancellation is best-effort: it doesn’t prevent GPU copy if already queued.

---

## TODOs & enhancement summary

| Area                         | Status / Limitation                                                | Planned Action / Note                                        |
|------------------------------|--------------------------------------------------------------------|--------------------------------------------------------------|
| Thread-safe pending queue    | Not implemented; Submit* records immediately                       | Add MPSC queue; move plan/stage/record into `Flush()`        |
| Batch splitting              | No splitting by thresholds or priority                             | Extend batcher for max regions/bytes; sort by priority       |
| Staging providers            | Providers exist (SingleBufferStaging, RingBufferStaging) but API rough edges | Make alignment explicit (no default) in RingBufferStaging; add asserts and docs; expose capacity/usage stats |
| Large batch splitting        | Not implemented (max regions/bytes/time-slice)                     | Split by `UploadPolicy::Batching` and `Timeouts`             |
| Diagnostics                  | Minimal (counts, bytes)                                            | Add per-frame latency, arena usage, queue/fence labeling     |
| Producer callback            | Only boolean-fill supported                                        | Add streaming/offset-based producer variant                  |
| Test coverage                | Good, but extend as features evolve                                | Add planner/allocator/queue-strategy test cases              |
| Queue strategy (NEW)         | Fixed heuristic; not policy/config-integrated                      | Introduce policy-driven `QueueStrategy`; device capability checks |
| Texture steady states (NEW)  | Minimal post-copy state handling for textures                      | Add per-usage texture state transitions post-copy            |
| Texture batching (NEW)       | `SubmitMany` coalesces buffers; textures remain per-request        | Group by dst/mip/format to record multi-region copies per CL |

## Migration plan reference

For the authoritative migration plan for `TransformUploader` (resident atlas +
upload ring + dynamic SRV, indirection table, and fence-keyed reclamation),
see the enhanced solution document: [upload-enhanced-solution.md](./upload-enhanced-solution.md).
That document tracks the step-by-step TODOs and acceptance criteria. This
README keeps the high-level module overview concise and defers detailed plan
and rationale there.


## Notes on correctness

- Planner uses `UploadPolicy` alignments (row/placement/buffer) and
  `FormatInfo` to compute row/slice pitches and total staging bytes.
- Staging buffer is created with `BufferMemory::kUpload`, mapped once, and
  unmap is guaranteed by RAII in `Allocation`.
  A custom provider must also return CPU-visible (upload) memory and guarantee
  mapped lifetime for the returned span.
- Buffer post-copy steady state is derived from `BufferUsage` flags; textures
  are copied with minimal state handling (see TODO above).
- Fences are registered per submission; `RetireCompleted()` advances the
  completed fence and finalizes tickets. Cancellation marks tickets completed
  with `kCanceled` but cannot stop in-flight GPU copies.

---

## References

- [UploadCoordinator.h](../Upload/UploadCoordinator.h)
- [UploadPlanner.h](../Upload/UploadPlanner.h)
- [UploadTracker.h](../Upload/UploadTracker.h)
- [UploadPolicy.h](../Upload/UploadPolicy.h)
- [UploadDiagnostics.h](../Upload/UploadDiagnostics.h)
- [StagingProvider.h](../Upload/StagingProvider.h)
- Tests: see `../Test/Upload*`

For implementation details, see the corresponding `.cpp` files.
