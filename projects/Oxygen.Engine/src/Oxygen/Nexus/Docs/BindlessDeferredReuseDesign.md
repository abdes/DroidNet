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
  - ✅ OnBeginFrame(FrameSlotIndex) already exists and executes frame-specific
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

### 4. Strategy B — TimelineGatedSlotReuse

- ⬜ Implement class skeleton (ctor takes AllocateFn, FreeFn,
  DescriptorAllocator&)
- ⬜ Pending-free structure keyed by (timeline ptr, fenceValue); Release()
  enqueues, Process() polls GetCompletedValue()
- ⬜ Convenience ReleaseAfterSubmission(recorder): Signal();
  QueueSignalCommand(value)
- ⬜ Unit tests (GPU-free)
  - ⬜ Fake queue/timeline; no reuse before completed<K; reuse after Process()
    when completed>=K; generation+1
  - ⬜ Multiple timelines/fence values; double release ignored
  - ⬜ Engine-wide resource cache integration patterns

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
- void OnBeginFrame(frame::FrameSlotIndex frame_slot); // executes bucket from
  previous render of this slot

### Notes (Strategy A)

- No explicit fences; safety comes from RenderController’s BeginFrame contract.
- Debug helper: IsHandleCurrent compares h.generation against
  generation_table[h.index].

## Strategy B — Explicit fence driven

Motivation: subsystems like upload/copy may produce long batches decoupled from
frame index rotation. They need reclamation keyed to their own GPU timeline.

### API (Renderer-layer wrapper around injected allocate/free)

```cpp
// Concepts that match existing engine types — no new structs/classes needed
template <typename T>
concept QueueTimeline = requires(const T& t) {
  { t.GetCompletedValue() } -> std::same_as<uint64_t>;
};

template <typename R>
concept RecorderOnQueue = requires(R& r) {
  { r.GetTargetQueue() } -> std::same_as<graphics::CommandQueue*>;
};

// Reserve a fence value on the recorder's queue and enqueue a GPU-side signal
// with existing APIs — no new renderer hooks needed.

class TimelineGatedSlotReuse {
public:
  explicit TimelineGatedSlotReuse(AllocateFn allocate,
                                  FreeFn free,
                                  oxygen::graphics::DescriptorAllocator& allocator);

  VersionedBindlessHandle Allocate(DomainKey domain, ResourceKey key /*view descs*/);
  template <QueueTimeline TL>
  void Release(DomainKey domain,
               VersionedBindlessHandle h,
               const TL& timeline,
               uint64_t fenceValue);

  // Convenience: tie release to a recorder's submission point using its target queue.
  template <RecorderOnQueue R>
  void ReleaseAfterSubmission(DomainKey domain,
                              VersionedBindlessHandle h,
                              R& recorder) {
    auto* q = recorder.GetTargetQueue();
    const uint64_t fv = q->Signal();
    q->QueueSignalCommand(fv);
    Release(domain, h, *q, fv);
  }
  void Process() noexcept; // non-blocking; reclaims any entries with counter->GetCompletedValue() >= fenceValue
  bool IsHandleCurrent(VersionedBindlessHandle h) const noexcept; // debug
};
```

### Implementation outline (Strategy B)

- Holds its own GenerationTracker and DomainIndexMapper.
- Maintains a pending-free min-heap or bucketed map keyed by (timeline ptr,
  fenceValue). Each PendingFree contains {absIndex, domain, timelineWeak,
  fenceValue}.
- Release enqueues PendingFree; it does not call backend free immediately.
- Process() (invoked by the renderer at convenient points, e.g., once per frame)
  iterates timelines that have pending frees, queries GetCompletedValue(), and
  reclaims all entries with fenceValue <= completed:
  - generation.bump(absIndex) with release semantics; then
  - free(domain, oxygen::bindless::Handle{absIndex}).
- Allocate mirrors Strategy A for stamping VersionedBindlessHandle.

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
  - By name: `auto queue = gfx->GetCommandQueue(queues.GraphicsQueueName());`
  - By role: `auto queue = gfx->GetCommandQueue(queues.TransferQueueName());`

### How fences are injected and advanced (Strategy B)

- After recording the last GPU use and before ending the recorder, reserve a
  fence value on the recorder’s target CommandQueue via `q->Signal()`.
  Immediately enqueue a GPU-side `q->QueueSignalCommand(value)` on the same
  queue. This ensures the fence reaches the reserved value once the recorded
  work is executed.
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
- Strategy B enqueues a PendingFree keyed by (counter, fenceValue) to be
  reclaimed during Process() once counter->GetCompletedValue() >= fenceValue.

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

## Strategy ownership: Graphics vs Nexus (assessment)

Graphics‑owned

- Pros: direct proximity to allocator; simple access to device objects
- Cons: couples lifecycle policy to low‑level device; harder to reuse across
  backends; risks constructor/initialization coupling; not primarily a graphics
  concern

Nexus‑owned (recommended)

- Pros: separates policy from mechanism; backend‑agnostic; testable in
  isolation; natural home for generation tracking and domain mapping; aligns
  with multi‑surface coordination needs
- Cons: requires clean APIs for Graphics/Renderer to provide timelines and to
  call Process()/OnBeginFrame hooks

## Fence id type and semantics (Strategy B)

- Type: uint64_t fence value, monotonically increasing, as returned by
  ID3D12Fence::GetCompletedValue via the engine’s abstraction.
- Semantics: Fence is completely independent from frame index. The renderer
  calls TimelineGatedSlotReuse::Process(), which queries timelines and reclaims
  accordingly. No calls to OnBeginFrame/PerFrameResourceManager are required.

### C++20 concept-based usage aligned with MainModule flow

```cpp
auto recorder = render_controller->AcquireCommandRecorder(
  graphics::SingleQueueStrategy().GraphicsQueueName(), "Main Window Command List", /*immediate*/ false);

// ... record commands for last use ...

// Option A: explicit queue + fence value (using existing APIs)
auto* queue = recorder->GetTargetQueue();
const uint64_t fenceValue = queue->Signal();
queue->QueueSignalCommand(fenceValue);
timelineReuse.Release(DomainKey{ ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible }, handle, *queue, fenceValue); // QueueTimeline auto&

// Option B: convenience API using RecorderOnQueue
timelineReuse.ReleaseAfterSubmission(DomainKey{ ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible }, handle, *recorder);

// Later (EndFrame) submission happens and the queued signal is injected.
timelineReuse.Process();
```

```cpp
// Global strategy instance (owned by Graphics device, shared by all RenderControllers)
// Strategy A — frame-based reclamation
auto hA = globalFrameReuse.Allocate(DomainKey{ ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible }, key, view);
globalFrameReuse.Release(DomainKey{ ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible }, hA);
// Which surface's frame index to use? This is the fundamental problem with Strategy A in multi-surface scenarios.

// Strategy B — timeline-based reclamation (preferred for multi-surface)
// Each surface contributes its timeline to the global strategy
auto hB = globalTimelineReuse.Allocate(DomainKey{ ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible }, key, view);
globalTimelineReuse.ReleaseAfterSubmission(DomainKey{ ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible }, hB, *surface1_recorder);
// Resource is safely reclaimed only when all dependent GPU timelines have progressed
globalTimelineReuse.Process(); // Called periodically by Graphics device
```

## Reclamation scheduling

- Strategy A: Renderer thread only, during BeginFrame(frame::FrameSlotIndex).
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
  value to drive reclamation.

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
