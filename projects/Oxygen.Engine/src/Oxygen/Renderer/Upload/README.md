# Upload Module

The Upload module provides a unified, deterministic path to stage and submit
buffer/texture uploads to the GPU. It centralizes footprint planning, staging
allocation, command recording, submission, and completion tracking behind a
small, renderer-internal, coroutine-friendly API.

## Quick tour (what it does)

- Plans copy footprints/regions: `UploadPlanner` for buffers, 2D/3D/cube
  textures.
- Allocates CPU-visible staging: `StagingProvider` implementations return
  persistently mapped regions (or mapped-on-demand) for filling.
  - Single buffer: `SingleBufferStaging` (pinned or per-op mapping).
  - Ring/linear arena: `RingBufferStaging` (partitioned per frame; caller
    selects alignment and active partition).
- Records and submits copy commands on a transfer queue when available, else
  the graphics queue.
- Tracks completion and basic stats via GPU fence values: `UploadTracker`.

- Uses `UploadPolicy` for alignment constants and batching scaffolding.

End-to-end flow: Submit → Plan → Stage/Fill → Record copy → Submit → Register
ticket → RetireCompleted polls fence and notifies providers for recycling.

## Key components and responsibilities

- UploadCoordinator
  - Orchestrates planning, staging, command recording, submission, and
    completion tracking.
  - Source: `UploadCoordinator.h/.cpp`.
- UploadPlanner
  - Computes buffer coalescing and texture subresource regions using
    `UploadPolicy` row/placement alignment and `FormatInfo`.
  - Merges contiguous regions and groups by destination buffer, minimizing copy
    operations and state transitions.
  - Source: `UploadPlanner.h/.cpp`.
- StagingProvider (abstract)
  - Contract for CPU-visible staging allocations; returns an `Allocation`
    with buffer, offset, size, and pointer.
  - Can recycle in `RetireCompleted(fence)` and receive per-frame ticks via
    `OnFrameStart(Slot)`.
  - Source: `StagingProvider.h`.
  - Implementations:
    - SingleBufferStaging: one upload buffer that grows with a slack factor;
      mapping policy is pinned or per-op. Source: `SingleBufferStaging.h`.
    - RingBufferStaging: one persistently mapped upload buffer, partitioned by
      frames-in-flight; bump-allocates per active partition; alignment is
      required (power-of-two). Source: `RingBufferStaging.h/.cpp`.
- UploadTracker
  - Fence-based ticketing with blocking and coroutine-friendly waits;
    aggregates `UploadStats`.
  - Frame-slot-based cleanup: discards all entries created for the same frame
    slot than the current one, but ina previous frame cycle.
  - Source: `UploadTracker.h/.cpp`, `UploadDiagnostics.h`.
- UploadPolicy
  - Policy constants: batch limits, row/placement/buffer copy alignments,
    and time-slice knobs. Source: `UploadPolicy.h/.cpp`.

## Public API at a glance

See `UploadCoordinator.h` for complete signatures. Highlights:

- Submissions (provider-aware and default):
  - `Submit(const UploadRequest&, std::shared_ptr<StagingProvider>)` →
    `UploadTicket`
  - `SubmitMany(std::span<const UploadRequest>, std::shared_ptr<StagingProvider>)`
    → `std::vector<UploadTicket>`
  - Convenience overloads with an internal default provider
    (`SingleBufferStaging` pinned): `Submit(...)`, `SubmitMany(...)`
- Waiting and results:
  - `IsComplete(UploadTicket)`, `TryGetResult(UploadTicket)`
  - `Await(UploadTicket)`, `AwaitAll(span<UploadTicket>)`
- Async helpers (OxCo):
  - `SubmitAsync(...)`, `SubmitManyAsync(...)`
  - `AwaitAsync(UploadTicket)`, `AwaitAllAsync(span<UploadTicket>)`
- Frame control and diagnostics:
  - `Flush()` submits deferred command lists to queues.
  - `RetireCompleted()` polls queue fences, advances `UploadTracker`, and
    notifies active providers via `RetireCompleted(fence)`.
  - `GetStats()` returns `UploadStats` (submitted, completed, in_flight,
    bytes_submitted, bytes_completed).
  - `Cancel(UploadTicket)` best-effort cancellation.

## Usage examples

### Synchronous buffer upload (default provider)

```cpp
std::vector<std::byte> data = /* ... */;

oxygen::engine::upload::UploadRequest req {
  .kind = oxygen::engine::upload::UploadKind::kBuffer,
  .debug_name = "MyBufferUpload",
  .desc = oxygen::engine::upload::UploadBufferDesc{
    .dst = my_device_buffer,
    .size_bytes = static_cast<uint64_t>(data.size()),
    .dst_offset = 0,
  },
  .data = oxygen::engine::upload::UploadDataView{ std::span<const std::byte>(data) },
};

auto ticket = upload.Submit(req);
upload.Flush();
auto result = upload.Await(ticket);
```

### Provider-aware batch of buffers (coalesced staging)

```cpp
using namespace oxygen;
using namespace oxygen::engine::upload;

auto provider = std::make_shared<SingleBufferStaging>(gfx.shared_from_this());

std::array<UploadRequest, 2> reqs = {
  UploadRequest{
    .kind = UploadKind::kBuffer,
    .debug_name = "Mesh.IB",
    .desc = UploadBufferDesc{ .dst = index_buffer, .size_bytes = ib_bytes, .dst_offset = 0 },
    .data = UploadDataView{ ib_view },
  },
  UploadRequest{
    .kind = UploadKind::kBuffer,
    .debug_name = "Mesh.VB",
    .desc = UploadBufferDesc{ .dst = vertex_buffer, .size_bytes = vb_bytes, .dst_offset = 0 },
    .data = UploadDataView{ vb_view },
  },
};

auto tickets = upload.SubmitMany(reqs, provider);
upload.Flush();
auto results = upload.AwaitAll(tickets);
```

### Texture2D upload with subresource regions

```cpp
using namespace oxygen::engine::upload;

UploadRequest tex_req {
  .kind = UploadKind::kTexture2D,
  .debug_name = "AlbedoAtlas",
  .desc = UploadTextureDesc{ .dst = my_texture, .width = 0, .height = 0, .depth = 1 },
  .subresources = { UploadSubresource{ .mip = 0, .array_slice = 0, .x = 0, .y = 0,
                                       .width = 256, .height = 256 } },
  .data = UploadDataView{ /* contiguous texel bytes for the planned regions */ },
};

auto t = upload.Submit(tex_req);
upload.Flush();
auto r = upload.Await(t);
```

## How planning and recording work

- Buffer batches: `UploadPlanner::PlanBuffers` filters valid requests, sorts by
  destination, assigns aligned staging offsets, optionally merges contiguous
  regions, and returns a total staging size plus per-copy regions. The
  coordinator fills one staging allocation and records multiple `CopyBuffer`
  calls, minimizing state transitions per destination.
- Texture2D/3D/Cube: planners compute row/slice pitches from
  `oxygen::graphics::detail::GetFormatInfo`, apply row/placement alignment from
  `UploadPolicy`, and output `TextureUploadRegion` entries. The coordinator then
  records `CopyBufferToTexture` for those regions.
- State transitions:
  - Buffers: steady post-copy state derives from `BufferUsage` (index, vertex,
    constant, storage → shader resource). See `UploadCoordinator.cpp`.
  - Textures: currently returned to `ResourceStates::kCommon`.
- Queue selection: prefers transfer queue if available, else graphics. See
  `ChooseUploadQueueKey` in `UploadCoordinator.cpp`.

## Providers and lifecycle

- SingleBufferStaging
  - Grows a single upload buffer with a slack factor; supports pinned mapping
    or per-op map/unmap.
  - Unmaps on `RetireCompleted` when in per-op mode.
- RingBufferStaging
  - One persistently mapped buffer partitioned by frames-in-flight; bump
    allocates within the active partition; caller must call
    `SetActivePartition(Slot)` (or rely on `OnFrameStart`) each frame.
  - Alignment is required (power-of-two), typically the element stride or 256.
  - Retire is a no-op; reuse happens when the active partition changes.
- Allocation lifetime and unmap
  - `StagingProvider::Allocation` will unmap its buffer in its destructor if it
    still holds the buffer and it is mapped.
  - The coordinator moves the staging buffer into a deferred reclaimer that
    unmaps and releases it after GPU completion, so the allocation’s own unmap
    is typically bypassed for submissions (by design).

## Limitations and future enhancements

The list below reflects current code behavior and intended improvements.

Current limitations

- Queue strategy is a fixed heuristic (prefer transfer, else graphics); not
  policy- or config-driven yet. Ref: `ChooseUploadQueueKey`.
- Texture steady states are minimal (post-copy → `kCommon`). No steady-state
  selection based on usage yet.
- `RingBufferStaging` requires explicit alignment and external per-frame slot
  selection; there is no centralized coordinator-driven frame tick beyond
  `OnFrameStart(Slot)`.
- Batch splitting by size/region/time is not implemented; large batches are
  recorded as-is if planned.
- Producer callback is a simple boolean fill (`bool(span<byte>)`). No
  streaming/chunked producers yet.
- Provider-focused unit tests are minimal; most coverage targets planning and
  coordinator flows. See tests under `../Test/`.

Planned enhancements

- Introduce a policy-driven queue strategy integrated with renderer config and
  device capabilities.
- Add per-usage texture steady-state transitions post-copy.
- Implement batch splitting limits via `UploadPolicy::Batching` and time-slice
  knobs.
- Extend producer API to support streaming/chunked writes and offset-based
  fills.
- Improve diagnostics: per-frame latency, arena usage, queue/fence labeling,
  and provider-capacity reporting.
- Expand test coverage to include provider behaviors and coordinator-provider
  integration edge cases.

## References (source of truth)

- UploadCoordinator: `./UploadCoordinator.h`, `./UploadCoordinator.cpp`
- UploadPlanner: `./UploadPlanner.h`, `./UploadPlanner.cpp`
- UploadPolicy: `./UploadPolicy.h`, `./UploadPolicy.cpp`
- UploadTracker & stats: `./UploadTracker.h`, `./UploadTracker.cpp`,
  `./UploadDiagnostics.h`
- Staging contract: `./StagingProvider.h`
- Providers: `./SingleBufferStaging.h`, `./RingBufferStaging.h`,
  `./RingBufferStaging.cpp`
- Tests: `../Test/Upload*` (planner, tracker, and coordinator scenarios)

Related design notes: see `./upload-enhanced-solution.md` for the step-by-step
plan and acceptance criteria for future enhancements.
