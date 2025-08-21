# Bindless Deferred Slot Reuse Design

## Problem Statement

Bindless rendering requires stable, shader-visible descriptor indices for the
lifetime of each resource. A descriptor slot can be recycled only after the
resource’s last GPU use has definitively completed. The system must prevent slot
aliasing across frame-rotating work and long‑running queue batches, and provide
reliable CPU‑side stale‑handle validation.

## Solution Overview

Introduce a renderer‑layer generation policy that bumps a slot’s generation at
the exact reclamation point. Two complementary strategies cover distinct
synchronization models:

- Strategy A (frame‑based): reuse on the next cycle of the same in‑flight frame
  slot
- Strategy B (timeline‑based): reuse only after an explicit GPU queue timeline
  completion

Both strategies share a `GenerationTracker` and a `DomainIndexMapper` for
thread‑safe reuse and CPU‑side handle validation.

## Multi‑threaded rendering architecture

### Current implementation

The system now uses a single global `DescriptorAllocator` at the device level
and a global resource registry. Per‑renderer ownership is removed in favor of a
unified index space across all surfaces.

### Resource ownership patterns

Engines naturally partition GPU resources by lifecycle and sharing behavior.

#### Frame‑linked / renderer‑specific

Short‑lived, view‑specific, high allocation frequency.

- Scene constants (camera matrices, lighting, view‑dependent data)
- Draw‑item CBVs (per‑object transforms, material instance parameters)
- Transient render targets (G‑buffer, shadow maps, post‑processing buffers)
- Dynamic data (particles, dynamic vertices, compute dispatch parameters)
- Frame‑specific uploads (streaming, animation buffers)

Ownership: individual `RenderController` instances; no cross‑surface sharing.

#### Shared engine‑wide

Long‑lived, asset‑derived, shared across views.

- Asset textures
- Static geometry (vertex/index buffers)
- Material definitions and parameter blocks
- Static lookup data (environment maps, BRDF LUTs, noise textures, font atlases)
- Global constants (engine settings, time, global lighting)

Ownership: engine‑wide cache with checkout/check‑in for thread safety.

### Simplified architecture

Adopt a single global approach for bindless deferred reuse with Nexus‑owned
strategies.

#### Global bindless heap management

- One global `DescriptorAllocator` for all bindless resources (Graphics)
- One global `GenerationTracker` (Nexus)
- One global `DomainIndexMapper` (Nexus)
- One or more strategy instances managing slot reuse (Nexus)

#### Strategy selection by lifecycle

- Strategy A (frame‑based): frame‑synchronized resources (scene constants,
  draw‑item CBVs, per‑frame uploads). Each `RenderController` releases frame
  resources via the global strategy at frame boundaries. Lifecycle: allocate →
  use within frame → release at frame boundary.
- Strategy B (timeline‑based): independent subsystems (upload/copy, background
  work with separate timelines). Lifecycle: allocate → use across frames →
  release when the subsystem’s GPU timeline completes.

#### Tracking scope

- Bindless‑heap resources: asset textures, static geometry, material CBVs — all
  tracked with the global `GenerationTracker` and strategy
- Non‑heap: root constants, root descriptor table bindings, immediate descriptor
  writes — no slot reuse, thus no generation tracking

### Threading and synchronization

Single strategy instance accessed by multiple render threads.

- Allocate/Release: thread‑safe entry points
- Generation table: single `std::atomic<uint32_t>[]` supports concurrent access
- Strategy synchronization: internal coordination for concurrent operations
- Backend coordination: the global allocator supports multi‑threaded alloc/free

Lifecycle threading:

- Frame resources: `RenderController`s release via the global strategy at frame
  boundaries
- Upload resources: independent threads release via the global strategy when
  their timelines complete
- Ownership: resources remain single‑owner; only the global strategy is shared

### Deferred reuse strategy deployment

Nexus‑owned strategy instances manage bindless slot reuse across surfaces.

#### Strategy A: frame‑based reclamation

Use for frame‑synchronized resources.

- Lifecycle: allocate → use within frame → release at frame boundary
- Integration: each `RenderController` releases its frame resources via the
  global strategy
- Timing: BeginFrame/OnBeginFrame for a slot implies prior GPU work for that
  slot has completed

#### Strategy B: timeline‑based reclamation

Use for subsystems with separate GPU timelines.

- Lifecycle: allocate → use across frames → release when the queue’s fence value
  completes
- Integration: release calls are paired with the queue/timeline; reclamation is
  polled, non‑blocking
- Timing: process opportunistically (e.g., once per frame after submissions)

Results:

- Unified management of bindless slot reuse
- Clear separation by lifecycle (frame vs independent timeline)
- Thread‑safe access across render threads
- Reduced complexity without artificial partitioning

## Implementation plan (prioritized checklist)

Legend: ✅ complete · 🟡 partial · ⬜ not started

### 1. Foundations (build first)

- ✅ GenerationTracker utility
  - ✅ Implement per-slot std::atomic<uint32_t> table and load/bump API
    (acquire/release semantics).
  - ✅ Edge cases: lazy init to 1, never reset on reuse.
  - ✅ Dynamic resize support with generation preservation
  - ✅ Comprehensive thread-safety testing

- ✅ Domain and DomainIndexMapper
  - ✅ Define DomainKey = { ResourceViewType, DescriptorVisibility }.
  - ✅ Build mapping using only DescriptorAllocator:
    - start = GetDomainBaseIndex(vt, vis)
    - capacity = GetAllocatedDescriptorsCount(vt, vis) +
      GetRemainingDescriptorsCount(vt, vis)
  - ✅ API: GetDomainRange(DomainKey) and ResolveDomain(Handle)
  - ✅ PIMPL pattern for ABI stability
  - ✅ Immutable design for thread-safety
  - ✅ Comprehensive unit tests with mock allocator

### 2. Per‑frame infrastructure

- ✅ PerFrameResourceManager augmentation
  - ✅ OnBeginFrame(oxygen::frame::Slot) already exists and executes frame-specific
    deferred actions
  - ✅ Underlying deferred action infrastructure exists via std::function<void()>
    vectors
  - ✅ Exposed `RegisterDeferredAction(std::function<void()>)` API for
    arbitrary callbacks (thread-safe)
  - ✅ Thread-safety analysis and implementation for registration from worker
    threads completed; registration uses a lock-free SPSC queue for common
    producer patterns and falls back to a small mutex for rare multi-producer
    cases
  - ✅ Integrated with RenderController frame lifecycle: `OnBeginFrame` now
    drains and executes registered deferred actions for the corresponding
    frame slot

### 3. Strategy A — FrameDrivenSlotReuse

- ✅ Implement class skeleton (ctor takes AllocateFn, FreeFn,
  DescriptorAllocator&, PerFrameResourceManager&)
- ✅ Hook Release() to per‑frame deferred actions; reclaim on
  OnBeginFrame(frameSlot): bump generation then free
- ✅ IsHandleCurrent() using GenerationTracker for stale handle detection
- ✅ Thread-safe double-release protection using atomic CAS operations
- ✅ EnsureCapacity() with lazy buffer growth and concurrent resize protection
- ✅ Unit tests
  - ✅ No reuse before same-slot cycle; reuse after cycle with generation+1
  - ✅ Stale handle detection after reclamation
  - ✅ Safe double-release behavior (ignored, no side effects)
  - ✅ Multi-threaded frame resource allocation patterns
  - ✅ High-volume concurrent allocation/release stress testing
  - ✅ Buffer growth for large indices
  - ✅ Edge cases: invalid handles, concurrent double-release protection

### 4. Strategy B — TimelineGatedSlotReuse (Design Complete)

- 🟡 Design complete, implementation not started
  - 🟡 `TimelineGatedSlotReuse(AllocateFn allocate, FreeFn free, DescriptorAllocator& allocator)` ctor
  - 🟡 `oxygen::VersionedBindlessHandle Allocate(DomainKey)` and stamp generation (acquire semantics)
  - 🟡 `void Release(DomainKey, oxygen::VersionedBindlessHandle, const oxygen::graphics::CommandQueue&, oxygen::graphics::FenceValue)`
  - 🟡 `void ReleaseBatch(const oxygen::graphics::CommandQueue&, oxygen::graphics::FenceValue, std::span<const std::pair<DomainKey, oxygen::VersionedBindlessHandle>>)`
  - 🟡 `void Process() noexcept` and `void ProcessFor(const oxygen::graphics::CommandQueue&) noexcept`

- 🟡 Pending-free bookkeeping and data structures
  - 🟡 Per-timeline keying: `std::unordered_map<QueueKey, TimelineBuckets>` where `QueueKey` is `oxygen::graphics::CommandQueue*`.
  - 🟡 `TimelineBuckets` is an ordered (ascending) map from `oxygen::graphics::FenceValue` → small vector of `{DomainKey, absIndex}`.
  - 🟡 Atomic pending flag array (e.g., `std::atomic<uint8_t>[]`) for double-release protection.
  - 🟡 Per-timeline append paths use either a small mutex or lock-free SPSC for common producer patterns; `Process()` is the single consumer.

- 🟡 Monotonic processing and reclamation algorithm
  - 🟡 `Release` / `ReleaseBatch` only enqueue pending frees; they must CAS the pending flag per index before enqueue.
  - 🟡 `ProcessFor(queue)` queries `queue->GetCompletedValue()` and reclaims buckets with `fence <= completed` in ascending order.
  - 🟡 Reclamation: `generation.bump(absIndex)` (memory_order_release) then call injected `free(domain, oxygen::bindless::Handle{absIndex})`.
  - 🟡 Ensure iteration is safe if `GetCompletedValue()` increases between reads; only front buckets are processed.

- 🟡 Capacity and growth handling
  - 🟡 `EnsureCapacity_` to grow `GenerationTracker` and pending flags when `Allocate` returns indices beyond current capacity.
  - 🟡 On ReleaseBatch where indices originate from this strategy, `EnsureCapacity_` can be elided for performance; otherwise, call defensively.

- 🟡 Debug, validation and instrumentation
  - 🟡 `IsHandleCurrent(oxygen::VersionedBindlessHandle)` implementation using `GenerationTracker`.
  - 🟡 OXY_DEBUG gated checks for out-of-order fence values per timeline (DCHECK) and for queue lifetime (null checks).
  - 🟡 Throttled logging for stuck fences (pending-free growth) and optional `FlushAndDrain()` helper for tests/tools.

- 🟡 Unit tests (GPU-free, deterministic)
  - 🟡 `allocate_then_release_single_then_reclaim_when_completed` — single release with fake queue: no reuse until completed < K, reclaimed when completed >= K and generation increments.
  - 🟡 `batch_release_then_reclaim_all_under_same_fence` — release batch under same (queue, fence) and reclaim together.
  - 🟡 `multi_timeline_reclaim` — two queues with different completed values; only eligible buckets reclaimed per queue.
  - 🟡 `double_release_ignored` — duplicate releases ignored due to pending flag guard.
  - 🟡 `capacity_growth_on_allocate_and_release` — ensure `EnsureCapacity_` works for large indices.
  - 🟡 `is_handle_current_detects_stale_handles` — IsHandleCurrent returns false after reclamation.

Notes:

- The implementation should avoid taking ownership of `oxygen::graphics::CommandQueue` pointers; `QueueKey` is a non-owning raw pointer stable for device lifetime. `Process()` runs on the renderer thread; enqueue paths may be invoked from workers.
- The strategy must remain backend-agnostic: adapters convert backend `uint64_t` fence values into `oxygen::graphics::FenceValue` at call sites.

### 5. Debug/validation & feature toggles

- ⬜ ValidateHandleCurrent helper and OXY_DEBUG‑gated checks/logging
- ⬜ Throttled warnings for exhaustion / stuck fences; optional FlushAndDrain()
  tool path
- ⬜ Multi-threaded validation for shared vs frame resource separation

### 6. Migration (non‑breaking)

- ⬜ Integrate strategies at renderer admission/release sites (centralized swap
  from direct Allocate/Release)
- ⬜ Feature flag to enable versioned policy; fallback remains unchanged
- ⬜ Hybrid architecture deployment separating frame vs shared resource
  management

### 7. Quality gates

- ⬜ Build and lint pass (CMake/Conan profiles in repo)
- ⬜ Unit test suite green (both strategies and mapper)
- ⬜ Small smoke in example app to exercise both paths
- ⬜ Multi-threaded validation for descriptor allocation patterns

Acceptance criteria mapping:

- ⬜ Authoritative generation bump at reclaim after fence completion — requires
  ReclaimUntil and release/acquire ordering implementation
- ⬜ Freed slots recorded with pending fence and not reused early — requires
  pending‑free heap per segment implementation
- ⬜ Allocation returns VersionedBindlessHandle — requires new Allocate APIs
- ⬜ CPU stale‑handle validation — requires IsHandleCurrent/ValidateHandleCurrent
  implementation
- ⬜ Localized changes — requires allocator/segment/registry/renderer hook
  implementation
- ⬜ Tests verifying sequence — requires comprehensive test suite
- ⬜ Conventions respected — requires C++20, encapsulation, debug hooks, strong
  types implementation
- ⬜ Multi-threaded architecture with hybrid resource management patterns —
  requires full architecture implementation

## Common contracts and utilities

The following Nexus types provide the bindless-reuse building blocks. Refer to
the headers for full APIs, invariants, and usage examples.

- **oxygen::nexus::DomainKey** — Strong key identifying a bindless descriptor
  domain as a pair {ResourceViewType, DescriptorVisibility}. See
  src/Oxygen/Nexus/Types/Domain.h
- **oxygen::nexus::DomainRange** — Absolute range in the global bindless heap:
  {start: bindless::Handle, capacity: bindless::Capacity}. See
  src/Oxygen/Nexus/Types/Domain.h
- **oxygen::nexus::GenerationTracker** — Thread-safe per-slot generation table
  used to stamp VersionedBindlessHandle and detect stale handles; supports lazy
  init and resize. See src/Oxygen/Nexus/GenerationTracker.h
- **oxygen::nexus::DomainIndexMapper** — Immutable mapper between DomainKey and
  absolute heap ranges with reverse lookup from handle→domain; constructed from
  DescriptorAllocator state. See src/Oxygen/Nexus/DomainIndexMapper.h
- **oxygen::nexus::FrameDrivenSlotReuse** — Frame-driven deferred reuse strategy
  with generation tracking and thread-safe operations. See
  src/Oxygen/Nexus/FrameDrivenSlotReuse.h

## Strategy A — Frame lifecycle driven (implemented)

API (Renderer-layer wrapper around injected allocate/free):

```cpp
class FrameDrivenSlotReuse {
public:
  //! Type-erased backend allocate function: returns absolute handle index.
  using AllocateFn = std::function<oxygen::bindless::Handle(DomainKey)>;

  //! Type-erased backend free function.
  using FreeFn = std::function<void(DomainKey, oxygen::bindless::Handle)>;

  //! Construct the strategy with backend hooks and per-frame infrastructure.
  explicit FrameDrivenSlotReuse(AllocateFn allocate, FreeFn free,
    oxygen::graphics::DescriptorAllocator& allocator,
    oxygen::graphics::detail::PerFrameResourceManager& per_frame);

  //! Allocate a bindless slot in the specified domain.
  auto Allocate(DomainKey domain) -> oxygen::VersionedBindlessHandle;

  //! Release a previously allocated handle; reclamation is deferred.
  void Release(DomainKey domain, oxygen::VersionedBindlessHandle h);

  //! Returns true if the handle's generation matches the current slot state.
  auto IsHandleCurrent(oxygen::VersionedBindlessHandle h) const noexcept -> bool;

  //! Forward the frame-begin event to the per-frame buckets.
  void OnBeginFrame(oxygen::frame::Slot fi);
};
```

### Implementation outline (Strategy A)

- Holds its own GenerationTracker and DomainIndexMapper.
- Uses PerFrameResourceManager buckets keyed by frame index. Release enqueues a
  lambda into the current frame’s bucket. On the next cycle of the same frame
  index, per-frame executes the lambda which:
  - generation.bump(absIndex) with release semantics; then
  - free(domain, oxygen::bindless::Handle{absIndex}).
- Allocate calls allocate(domain, ...) to obtain an index, reads
  generation.load(absIndex) with acquire, and returns
  VersionedBindlessHandle{absIndex, gen}.

### Threading and lifecycle (Strategy A)

- Release is thread-safe via PerFrameResourceManager’s registration API.
  Execution happens on the renderer thread during OnBeginFrame for the in-flight
  frame slot that is cycling in.
- No explicit fences are used. Safety follows the RenderController guarantee:
  when BeginFrame(i) starts, GPU work from the previous render of i has
  completed.

### PerFrameResourceManager requirements

- void RegisterDeferredAction(std::function<void()> action); // thread-safe (implemented)
- void OnBeginFrame(oxygen::frame::Slot frame_slot); // executes bucket from
  previous render of this slot

### Notes (Strategy A)

- No explicit fences; safety comes from RenderController’s BeginFrame contract.
- Debug helper: IsHandleCurrent compares h.generation against
  generation_table[h.index].

## Strategy B — Suggested API for TimelineGatedSlotReuse

Strategy B provides explicit, timeline-based reclamation decoupled from frame rotation. Callers pair each release with a concrete `oxygen::graphics::CommandQueue` and a strong `oxygen::graphics::FenceValue` captured from `CommandQueue::Signal()` and recorded into the command list via `QueueSignalCommand(...)`. The API is intentionally minimal and backend‑agnostic: no recorder conveniences, no templates — just queue + fence. This makes it a natural fit for multi-surface rendering and long‑running upload/copy batches where the owning timeline is not the renderer's frame index.# Solution Overview

### When to choose TimelineGatedSlotReuse (Strategy B)

Strategy B is aimed at subsystems whose resource lifetime cannot be safely
expressed using the renderer's frame index rotation. Prefer TimelineGated
reuse in the following situations:

- Multi-surface or multi-swapchain submission: when a resource may be used by
  several surfaces that progress on independent presentation/timing paths. The
  renderer's frame slot for surface A may advance at a different pace than
  surface B; only a queue-aligned fence can guarantee the resource's final
  GPU use has completed for all dependent timelines.
- Background upload/copy/streaming pipelines: long-running transfer batches
  recorded on a transfer/async queue and flushed infrequently. These batches
  produce work that outlives a single frame and therefore cannot rely on
  frame-based rotation to ensure safe reclamation.
- Cross-queue dependencies: when a resource is produced on one queue and later
  consumed on another (for example, copy on transfer queue then used on
  graphics queue), pairing the release with the originating queue's fence is
  a robust way to express the moment of last GPU use.
- Off-main-thread recorders: recorders that submit work from worker threads
  where the per-frame lifecycle is not directly visible to the producer. The
  producer can capture its command queue and fence value and hand them to the
  global timeline strategy without accessing per-frame buckets.
- Subsystems that require explicit determinism for reclamation timing: tools,
  capture/replay, or testing harnesses that need precise, reproduceable
  reclamation points should use explicit fence-based releases.

Decision guidance — quick checklist

- If the resource is strictly frame-local (allocated, used and released within
  one frame slot) → use FrameDrivenSlotReuse (Strategy A).
- If the resource is shared engine-wide or used across frames but always
  released from the renderer thread and tightly bound to BeginFrame semantics
  → Strategy A may still be acceptable.
- If the resource's last GPU use is recorded into a command list that is not
  guaranteed to complete within the same frame index (long transfer batches,
  multi-surface presentation, cross-queue workflows) → prefer TimelineGated
  (Strategy B).

Practical examples

- Texture streaming: staging textures are uploaded on the transfer queue in
  large batches; reclamation must wait for the transfer queue's fence to
  complete before freeing staging descriptors. Use TimelineGated and batch
  releases with the transfer queue's fence value.
- Staging descriptor views created by asset importers running on worker
  threads: those threads can record their own recorder and supply the queue+
  fence into ReleaseBatch without touching per-frame data structures.
- Cross-surface readbacks: a texture used for readback on the GPU and then
  sampled by multiple windows with independent frame pacing — reclamation is
  only safe after the queue that performed the readback has signalled.

When not to use Strategy B

- High-frequency, per-frame dynamic allocations where the overhead of
  per-queue bookkeeping outweighs benefits. FrameDrivenSlotReuse is simpler
  and faster for truly frame-local workloads.
- When the code path cannot access a CommandQueue or a fence value at the
  point of release; in that case, prefer migrating the call site to a
  renderer-mediated release (Strategy A) or provide an adapter that obtains
  the queue/fence for the producer.

### Summary of TimelineGatedSlotReuse design

Strategy B provides explicit, timeline-based reclamation decoupled from frame rotation. Callers pair each release with a concrete `graphics::CommandQueue` and a strong `graphics::FenceValue` captured from `CommandQueue::Signal()` and recorded into the command list via `QueueSignalCommand(...)`. The API is intentionally minimal and backend‑agnostic: no recorder conveniences, no templates — just queue + fence. This makes it a natural fit for multi-surface rendering and long‑running upload/copy batches where the owning timeline is not the renderer’s frame index.

Internally, the strategy owns a `GenerationTracker` and a `DomainIndexMapper`, and keeps per‑queue pending frees bucketed by monotonically increasing `FenceValue`s. `Release`/`ReleaseBatch` only enqueue entries; `Process()` (or `ProcessFor(queue)`) polls `queue->GetCompletedValue()` and reclaims all buckets with `fence <= completed`, bumping the slot generation before calling the injected backend `free`. The path is non‑blocking and thread‑safe, with double‑release protection via atomic flags and defensive capacity growth to cover large indices.

Ownership is device‑wide and Nexus‑scoped: a single instance serves the global bindless heap backed by the engine’s `DescriptorAllocator`. Typical use: after recording the last GPU use, obtain the target `CommandQueue*`, reserve `auto v = q->Signal();` and record `q->QueueSignalCommand(v);`, then call `Release`/`ReleaseBatch(*q, graphics::FenceValue{v}, ...)`. The renderer calls `Process()` opportunistically (e.g., once per frame after submissions). This keeps lifecycle policy separate from backend mechanics and avoids coupling to per‑frame scheduling.

 Motivation: subsystems like upload/copy may produce long batches decoupled from
frame index rotation. They need reclamation keyed to their own GPU timeline.

> Note: The suggested API, FenceValue type, processing rules, capacity/growth guidance and test matrix for Strategy B are in the "Suggested API for TimelineGatedSlotReuse" section below.

### Suggested API for TimelineGatedSlotReuse

This section provides the suggested API design for TimelineGatedSlotReuse.
The design is complete but implementation has not yet started. Other narrative
sections may discuss usage and examples, but the concrete API, types,
processing rules, and tests are specified here.

- Strong fence type: `oxygen::graphics::FenceValue` — IMPLEMENTED
  (see `src/Oxygen/Graphics/Common/Types/FenceValue.h`). This is a
  NamedType wrapper around `uint64_t` that supports comparison and basic
  printable/hashable skills. Values come from `CommandQueue::Signal()` and
  are observed via `CommandQueue::GetCompletedValue()`; monotonic per-queue.

- Public API (canonical):

  ```cpp
  class TimelineGatedSlotReuse {
  public:
    using AllocateFn = std::function<oxygen::bindless::Handle(DomainKey)>;
    using FreeFn = std::function<void(DomainKey, oxygen::bindless::Handle)>;

    explicit TimelineGatedSlotReuse(AllocateFn allocate,
                                    FreeFn free,
                                    oxygen::graphics::DescriptorAllocator& allocator);

    VersionedBindlessHandle Allocate(DomainKey domain);

    void Release(DomainKey domain,
                 VersionedBindlessHandle h,
                 const oxygen::graphics::CommandQueue& queue,
                 oxygen::graphics::FenceValue fence_value);

    void ReleaseBatch(const oxygen::graphics::CommandQueue& queue,
                      oxygen::graphics::FenceValue fence_value,
                      std::span<const std::pair<DomainKey, VersionedBindlessHandle>> items);

    void Process() noexcept;
    void ProcessFor(const oxygen::graphics::CommandQueue& queue) noexcept;

    bool IsHandleCurrent(VersionedBindlessHandle h) const noexcept;
  };
  ```

- Processing/ordering rules (canonical):
  - Releases only enqueue PendingFree entries keyed by (QueueKey, oxygen::graphics::FenceValue).
  - Pending frees are bucketed per-timeline and ordered by ascending oxygen::graphics::FenceValue.
  - `Release` must CAS an atomic per-index pending flag to prevent double-release
    prior to enqueueing an entry.
  - `ProcessFor(queue)` reads `queue->GetCompletedValue()` and reclaims
    buckets with `fence <= completed` in ascending order. For each reclaimed
    index: bump generation (release semantics) then call injected `free(domain, Handle{idx})`.
  - `Allocate` stamps the returned index with `GenerationTracker::Load()`
    (acquire) to produce a `VersionedBindlessHandle`.

- Capacity and growth (canonical):
  - `EnsureCapacity_` grows `GenerationTracker` and pending_flags to cover
    indices returned by backend allocate calls. Batched releases may elide
    growth when handles are known to come from this strategy, but callers may
    be defensive and call `EnsureCapacity_` when in doubt.

- Debugging and testing (canonical):
  - Provide `IsHandleCurrent` to validate stale handles via generation checks.
  - Offer a `FlushAndDrain()` helper only for tests/tools to force reclamation.
  - Tests should be GPU-free via a fake `CommandQueue` that can advance
    `GetCompletedValue()` deterministically.

- Minimal test matrix (canonical):
  - allocate_then_release_single_then_reclaim_when_completed
  - batch_release_then_reclaim_all_under_same_fence
  - multi_timeline_reclaim
  - double_release_ignored
  - capacity_growth_on_allocate_and_release

### Who calls Process() and when (Strategy B)

- Owner: Nexus (strategy instance managed by Nexus). Renderers and subsystems
  call into Nexus to release and process.
- Frequency: once per frame after queue submissions, and optionally after large
  upload/copy batches complete; non-blocking and cheap.
- Goal: opportunistically reclaim any entries whose timelines have advanced;
  correctness does not require high frequency, only eventual polling.

### Ownership and waiting (Strategy B)

- No thread actively “waits”. GPU queues signal their fence values as usual. The
  strategy only polls via Process(); it never blocks. Consumers may optionally
  call Process() immediately after advancing a fence to accelerate reclamation;
  not required for correctness.

### Timeline reference for consumers (Strategy B)

- Use the queue itself as the timeline. No wrappers are needed. Typical
  patterns:
  - By name: `auto queue = gfx->GetCommandQueue(queues.GraphicsQueueName());` (returns `std::shared_ptr<graphics::CommandQueue>`)
  - By role: `auto queue = gfx->GetCommandQueue(queues.TransferQueueName());`
  - In recorders: `recorder->GetTargetQueue()` exposes the `CommandQueue*` key needed by Strategy B.

### How fences are injected and advanced (Strategy B)

- After recording the last GPU use and before ending the recorder, reserve a
  fence on the recorder’s target queue:
  1) `auto* q = recorder->GetTargetQueue();`
  2) `const auto fv_raw = q->Signal();`
  3) `q->QueueSignalCommand(fv_raw);`
  4) Feed `FenceValue{fv_raw}` to `Release{,Batch}...`.
  This queues a GPU-side signal in the same command list so the fence reaches the value after work executes.
- Which command list: the one that carries the last GPU use of the resource
  being released. The releasing code pairs the reserved `fenceValue` with that
  same queue in `Release(..., queue, fenceValue)`.

### Interaction with per-frame lifecycle (Strategy B)

- None. Strategy B does not interact with PerFrameResourceManager or the
  frame-index counter. The renderer may call Process() once per frame for
  scheduling convenience, but there is no coupling or cross-waiting between the
  two timelines.

### Notes (Strategy B)

- Debug helper identical to Strategy A; independent and optional.

---

## Concurrency model (applies to both)

- Renderer thread performs Allocate and reclamation. Release can be called from
  workers; it enqueues into the strategy’s pending‑free structure under a small
  mutex or SPSC queue.
- Graphics backend locks remain internal and unchanged. The strategies only call
  backend Release during reclamation.
- Generation table entries are std::atomic<uint32_t> for lock‑free debug reads.
  Bump uses store(memory_order_release) (or fetch_add with release) before
  calling graphics Release; Allocate reads with load(memory_order_acquire)
  before returning the handle.

## Generation update algorithm and publication

On Allocate(domain, …) [both strategies]:

1) Call allocate(domain, …) → oxygen::bindless::Handle idx.
2) Read g = generation_table[idx].load(memory_order_acquire); if zero,
   initialize to 1.
3) Return VersionedBindlessHandle{ index=idx, generation=g }.

On Release(domain, h):

- Strategy A enqueues a lambda into the current frame’s bucket to be executed at
  the next cycle of this frame index.
- Strategy B enqueues a PendingFree keyed by (queue, FenceValue) to be
  reclaimed during Process() once `queue->GetCompletedValue() >= fenceValue.get()`.

Atomicity guarantees:

- memory_order_release on the generation bump happens-before publishing the free
  to the backend. A subsequent Allocate observes the bumped generation with
  acquire before returning a new VersionedBindlessHandle.

## Failure modes and recovery

- Fence never completes (Strategy B): pending‑free grows; allocation pressure
  may exhaust free_list. Allocate returns std::nullopt/invalid handle, caller
  falls back to default resources. Emit throttled warning; provide an emergency
  FlushAndDrain() path (renderer thread) for tools/tests.
- Ring is full / no slots: same as above; no aliasing occurs. Optional: soft
  reservation and back‑pressure counters.
- Double release: validated in debug; second release ignored.
- Out‑of‑order fence values: DCHECK monotonicity at OnFenceCompleted; ignore
  regressions.

## Ownership of pending‑free queue (design note)

- Decision: Each strategy instance (owned by Nexus) owns its pending‑free
  bookkeeping. Graphics allocator/segments remain unaware.
  - Pros: zero changes to backend; clearer separation of concerns; lifecycle
    policy lives in a higher‑level module; reusable across backends.
  - Cons: Requires explicit wiring so renderers/subsystems route release and
    processing calls through Nexus APIs.

## Strategy ownership: Nexus

All bindless slot reuse strategies are owned by Nexus, as the Unified GPU Resource Manager (UGRM). This design choice provides:

- **Policy separation**: Separates lifecycle policy from low-level graphics mechanisms
- **Backend agnostic**: Testable in isolation from specific graphics APIs
- **Natural coordination**: Nexus serves as the orchestration point for GPU residency decisions
- **Multi-surface support**: Aligns with multi-surface coordination needs across the engine
- **Clean abstractions**: Graphics/Renderer provide timelines via clean APIs; Nexus manages the deferred reuse logic

## Fence id type and semantics (Strategy B)

- Type: `oxygen::graphics::FenceValue` wrapping the raw `uint64_t` returned by
  `CommandQueue::Signal()` / `GetCompletedValue()`.
- Semantics: Fence is completely independent from frame index. The renderer
  calls TimelineGatedSlotReuse::Process(), which queries timelines and reclaims
  accordingly. No calls to OnBeginFrame/PerFrameResourceManager are required.

### Example usage aligned with MainModule flow

```cpp
auto recorder = render_controller->AcquireCommandRecorder(
  graphics::SingleQueueStrategy().GraphicsQueueName(), "Main Window Command List", /*immediate*/ false);

// ... record commands for last use ...

// Option A: explicit queue + fence value (using existing APIs)
auto* queue = recorder->GetTargetQueue();
const auto fenceValueRaw = queue->Signal();
queue->QueueSignalCommand(fenceValueRaw);
timelineReuse.Release(DomainKey{ ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible }, handle, *queue, oxygen::graphics::FenceValue{fenceValueRaw});

// Batch example (e.g., upload queue freeing many staging views)
std::array<std::pair<DomainKey, VersionedBindlessHandle>, 3> batch = { {
  { DomainKey{ ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible }, h0 },
  { DomainKey{ ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible }, h1 },
  { DomainKey{ ResourceViewType::kBuffer_CBV,   DescriptorVisibility::kShaderVisible }, h2 },
} };
timelineReuse.ReleaseBatch(*queue, oxygen::graphics::FenceValue{fenceValueRaw}, batch);

// Later (EndFrame) submission happens and the queued signal is injected.
timelineReuse.Process();
```

```cpp
// Global strategy instance (owned by Nexus, shared by all RenderControllers)
// Strategy A — frame-based reclamation
auto hA = globalFrameReuse.Allocate(DomainKey{ ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible }, key, view);
globalFrameReuse.Release(DomainKey{ ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible }, hA);
// Which surface's frame index to use? This is the fundamental problem with Strategy A in multi-surface scenarios.

// Strategy B — timeline-based reclamation (preferred for multi-surface)
// Each surface contributes its timeline to the global strategy
auto hB = globalTimelineReuse.Allocate(DomainKey{ ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible }, key, view);
// Explicit queue + fence usage only; recorder convenience removed.
// Resource is safely reclaimed only when all dependent GPU timelines have progressed
globalTimelineReuse.Process(); // Called periodically by Nexus
```

## Reclamation scheduling

- Strategy A: Renderer thread only, during BeginFrame(oxygen::frame::Slot).
  Rationale: Oxygen’s BeginFrame contract guarantees prior use for that slot has
  completed across queues (see RenderController::Frame.timeline_values), so
  reuse is safe without explicit fences.
- Strategy B: Renderer thread calls Process() opportunistically (e.g., once per
  frame or after queue submissions). No blocking waits.

## Generation publication order

- Ensure slots[s].generation.fetch_add(1, release) happens before
  free_list.push_back(s). Allocate reads generation with acquire after popping
  s. This guarantees a new allocate sees the incremented generation.

## Testing without a real GPU

- Strategy A: Leverage frame-index cycling: advance frame index N times and
  assert deferred actions for an index execute when that index cycles. No GPU
  required.
- Strategy B: Use a fake CommandQueue (or a thin test double) exposing
  GetCompletedValue()/Signal()/QueueSignalCommand with a controllable completed
  value to drive reclamation. Cover single and batched release flows.

---

## Integration points in the current codebase (validated)

Where to obtain a recorder and queue:

- `RenderController::AcquireCommandRecorder(queue_name, list_name, immediate)` returns a `std::unique_ptr<graphics::CommandRecorder>` with a custom deleter. The recorder exposes `CommandRecorder::GetTargetQueue()` (raw `graphics::CommandQueue*`).
- `Graphics::GetCommandQueue(name)` returns a `std::shared_ptr<graphics::CommandQueue>`; Strategy B uses the raw pointer as a stable timeline key. Lifetime is managed by Graphics (global for device lifespan).

How to reserve and inject a fence signal:

- `graphics::CommandQueue` APIs provide `Signal()` (returns `uint64_t`) and `QueueSignalCommand(uint64_t)`. Call these while the recorder is still recording, before `CommandRecorder::End()` is invoked by the deleter.
- D3D12 backend confirms these methods exist and are wired (see `Direct3D12/CommandQueue.[h|cpp]`).

When to enqueue releases:

- After recording the last GPU use and before destroying the recorder: get the queue from the recorder, reserve a fence (`q->Signal()`), record the GPU-side signal (`q->QueueSignalCommand(value)`), then call `Release(...)` or `ReleaseBatch(...)` with that `queue` and `oxygen::graphics::FenceValue{value}`.

When to process:

- Call `TimelineGatedSlotReuse::Process()` once per frame on the renderer thread after queue submissions. Optional `ProcessFor(queue)` can be used after heavy upload batches to accelerate reclamation for the transfer queue.

Notes on immediate vs deferred submission:

- `RenderController` supports both immediate and deferred list submission in its recorder deleter. The queued signal recorded via `QueueSignalCommand()` follows the command list; it will execute when the list is submitted — either immediately or during a later flush. Strategy B does not need to distinguish between the two.

Thread-safety:

- Release paths may be called from worker threads; Strategy B maintains a small mutex per timeline (or a global mutex) to append into buckets. `Process()` runs on the renderer thread.

---

## Scenarios and step-by-step flows

Scenario: Upload queue (transfer) batching

- A background worker records an upload/copy list using `RenderController::AcquireCommandRecorder(TransferQueueName(), ...)`.
- While recording, it stages N transient descriptors; for each, it collects `{domain, handle}` in a `std::vector`.
- Before finishing, it reserves a fence on the queue (`auto v = q->Signal(); q->QueueSignalCommand(v);`) and calls `timelineReuse.ReleaseBatch(*q, oxygen::graphics::FenceValue{v}, items)`. All items are enqueued into the `(queue, oxygen::graphics::FenceValue)` bucket.
- Later, renderer calls `timelineReuse.Process()`; Strategy reads `queue->GetCompletedValue()`, reclaims any buckets whose `oxygen::graphics::FenceValue <= completed`, bumps generations, and frees all indices.

Scenario: Graphics queue single release

- A streaming system updates a texture on the graphics queue. After the last use, it fetches the queue, reserves a fence, records the GPU-side signal, then calls `Release(domain, handle, *queue, oxygen::graphics::FenceValue{value})`.
  - Same reclamation flow as above; no batching required.

Edge cases handled

- Double release: guarded by atomic pending flag; second attempt ignored.
- Out-of-order fence values on a timeline: ignored; buckets are ordered, only front buckets are processed.
- Queue lifetime: raw pointer key comes from engine-owned queues; no ownership taken.
- Capacity growth: EnsureCapacity_ covers large indices; cost amortized.

---

## Tests to add (GPU-free)

- allocate_then_release_then_no_reuse_until_fence_single: completed < K → no reuse; after Process with completed >= K → reuse with generation+1; IsHandleCurrent(old) == false.
- allocate_then_batch_release_then_reclaim: enqueue M handles under same `(queue, oxygen::graphics::FenceValue)`; Process reclaims all when completed >= fence.
- multi_timeline_processing: two queues with different completed values; only eligible buckets reclaimed per queue.
- double_release_ignored_batched: batch contains duplicate handle → only one reclaim occurs.
- capacity_growth_paths: release/allocate indices beyond initial capacity; EnsureCapacity_ keeps types safe.

---

## Impact assessment (files/classes and rationale)

| File/Class | Path | Purpose of change | Risk |
|---|---|---|---|
| FixedDescriptorHeapSegment.{h,cpp} | src/Oxygen/Graphics/Common/Detail/ | No change | Low |
| BaseDescriptorAllocator.{h,cpp} | src/Oxygen/Graphics/Common/Detail/ | No change | Low |
| DescriptorAllocator.h | src/Oxygen/Graphics/Common/ | No change; SSoT queried by DomainIndexMapper | Low |
| ResourceRegistry.{h,cpp} | src/Oxygen/Graphics/Common/ | No change | Low |
| CommandQueue.{h,cpp} | src/Oxygen/Graphics/Common/ | No change; used by Strategy B (Signal/QueueSignalCommand/GetCompletedValue) | Low |
| CommandRecorder.{h,cpp} | src/Oxygen/Graphics/Common/ | No change; used by Strategy B (GetTargetQueue) | Low |
| RenderController.{h,cpp} | src/Oxygen/Graphics/Common/ | No change; BeginFrame contract for Strategy A | Low |
| Types/ResourceViewType.h | src/Oxygen/Graphics/Common/Types/ | No change; part of Domain key | Low |
| Types/DescriptorVisibility.h | src/Oxygen/Graphics/Common/Types/ | No change; part of Domain key | Low |
| BindlessHandle.h | src/Oxygen/Core/Types/ | No change; handle and versioned handle types | Low |
| PerFrameResourceManager.{h,cpp} | src/Oxygen/Graphics/Common/Detail/ | `RegisterDeferredAction(std::function<void()>)` added and thread-safe; OnBeginFrame already public | Low |
| FrameDrivenSlotReuse (new) | src/Oxygen/Nexus/ | Frame-driven strategy implementing deferred frees and generation stamping via per-frame buckets | Medium |
| TimelineGatedSlotReuse (new) | src/Oxygen/Nexus/ | Fence-driven strategy implementing deferred frees gated by explicit timelines | Medium |
| GenerationTracker | src/Oxygen/Nexus/ | Utility to load/bump per-slot generation with proper memory order | Low |
| DomainIndexMapper | src/Oxygen/Nexus/ | Utility to resolve BindlessHandle to its domain and expose domain ranges | Low |
| Renderer.{h,cpp} | src/Oxygen/Renderer/ | Integrate strategy at admission/release sites (migration step) | Medium |
| CMakeLists.txt | src/Oxygen/Renderer/ | Add new sources to build | Low |
| CMakeLists.txt | src/Oxygen/Nexus/Test/ | Add Nexus unit tests to build | Low |
| Tests | src/Oxygen/Nexus/Test/ | DomainIndexMapper_test.cpp, GenerationTracker_test.cpp, Link_test.cpp | Low |

Tests required:

- Strategy A:
  - allocate→release→allocate(before cycle) must not reuse; after next cycle of
    same frame index must reuse with generation+1.
  - stale handle detection after reuse.
  - double release ignored.
- Strategy B:
  - allocate→release(fence=K)→allocate(before completed<K) must not reuse; after
    Process() with completed=K it must reuse with generation+1.
  - multiple counters: only entries whose counter completed value >= fenceValue
    are reclaimed.
  - stale handle detection; double release ignored.

Backwards‑compatibility:

- No changes to graphics backend APIs or shader ABI. Existing users may continue
  to call the backend directly if they don’t need versioning; renderer code
  should use the Versioned facade.
- Migration: in renderer codepaths, replace direct backend Allocate/Release with
  BindlessSlotReuseStrategy calls. No mass refactor across passes; centralize at
  admission points.

## Test plan and harness sketch

- Test fixture builds a FixedDescriptorHeapSegment with small capacity (e.g.,
  4). A FakeFenceClock<uint64_t> provides advance(n) and value().
- Tests call Allocate/Release using injected fakes (allocate/free callables) and
  drive reclamation via frame cycling (Strategy A) or fake queue completed
  values (Strategy B).
- Assertions:
  - allocate_then_release_then_no_reuse_until_fence: same index is not returned
    before Fc; after Fc it is returned; generation increments by 1.
  - stale_handle_detected: IsHandleCurrent(oldHandle) == false after reuse.
