# Nexus slot retirement and completion-controlled reuse

## 1. Purpose and ownership

Separate invalidating a CPU slot handle from returning its index to an allocator.
Retirement invalidates the handle immediately. Finalization returns the index only
after the owning subsystem has finished every use of that allocation.

Nexus owns the generation and slot state. The caller owns the free list, resources,
use counts, and completion condition. A graphics caller obtains completion from
Graphics; Nexus does not inspect fences or own GPU resources.

The implementation extracts the lifecycle code from
[`FrameDrivenIndexReuse.h`](../FrameDrivenIndexReuse.h) into `IndexReuse.h` and
retains `FrameDrivenIndexReuse` as its frame-reclamation adapter.
[`GenerationTracker`](../GenerationTracker.h) remains the generation storage.
[`FrameDrivenSlotReuse`](../FrameDrivenSlotReuse.h) remains the descriptor-domain
adapter. `TimelineGatedSlotReuse` keeps its current API and behavior.

The rendering integration is specified in
[Conventional shadow sharing](../../../../design/vortex/lld/conventional-shadow-sharing.md).

## 2. Types and API

Keep `VersionedIndex<IndexType>` and the existing 32-bit generation representation.
Move its declaration, `IndexLike`, and the existing index-conversion helper into
`IndexReuse.h`; the frame adapter includes that header.
Existing includes of `FrameDrivenIndexReuse.h` continue to expose the type.

```cpp
template <IndexLike IndexType> class RetirementTicket;

template <IndexLike IndexType> class IndexReuse {
public:
  IndexReuse();
  auto ActivateSlot(IndexType index) -> VersionedIndex<IndexType>;
  auto IsHandleCurrent(VersionedIndex<IndexType> handle) const noexcept -> bool;
  auto TryRetire(VersionedIndex<IndexType> handle) noexcept
    -> std::expected<RetirementTicket<IndexType>, RetireError>;
  auto Close() noexcept -> void;
};

template <IndexLike IndexType> class RetirementTicket {
public:
  RetirementTicket(RetirementTicket&&) noexcept;
  auto operator=(RetirementTicket&&) noexcept -> RetirementTicket&;
  ~RetirementTicket() noexcept;
  auto Finalize() noexcept -> FinalizeResult<IndexType>;
};
```

`RetirementTicket` is move-only. It contains the index, the generation being
retired, and a shared reference to the issuing core state. It cannot be finalized
against another strategy instance. It contains no resource pointer, queue, frame
number, or allocator callback.

The core facade is noncopyable. Moving it transfers the shared state; destruction
of the moved-from facade performs no close. Destruction of the owning facade calls
Close. Move assignment closes the destination's old state before transferring the
source state. Ticket ownership, not a facade address, identifies its issuing state.

`RetireError` distinguishes `kOutOfRange`, `kStale`, `kAlreadyRetiring`, and
`kUnavailable`. `FinalizeResult` contains a disposition and, only for
`kReusable`, the index to publish to the owner's free list. The other dispositions
are `kExhausted`, `kClosed`, and `kAlreadyResolved`.

`ActivateSlot` returns a handle only for a reusable index. An invalid index throws
`std::out_of_range`; a closed core or non-reusable phase throws `std::logic_error`,
before changing the slot. Storage growth can throw; failed activation
leaves the previous state intact. The caller still owns an index obtained from its
allocator until activation succeeds.
Valid raw indices range from zero through `UINT32_MAX - 1`. Reject negative
indices and values at or above `UINT32_MAX` before growing storage.

`TryRetire`, ticket moves, `Finalize`, ticket destruction, and `Close` perform no
heap allocation and invoke no user callback.

## 3. State and synchronization

One shared core state contains:

- The existing `GenerationTracker`.
- A phase for each initialized index: `Reusable`, `Live`, `Retiring`, or
  `Unavailable`.
- The generation of the outstanding retirement, if any.
- The unavailable reason: generation exhaustion, abandoned ticket, or closed pool.
- The open/closed flag and telemetry counters.
- One mutex protecting generations, phases, resize, and transitions.

Use this mutex for complete transitions. Do not build a second atomic generation
table or add a lock-free state machine. Allocation and retirement are infrequent
relative to draw recording; neither operation belongs in the draw loop.

Growth first reserves phase storage, then grows `GenerationTracker`, then exposes
the new phase entries. A failed allocation must not expose a new index or discard
an existing generation. Uninitialized capacity is not an active slot.
Construct the replacement generation vector with its size directly. MSVC's
checked-iterator empty-vector constructor is noexcept but allocates a proxy;
default construction followed by resize would terminate on that allocation
failure instead of preserving the strong exception guarantee.

| Operation            | Required state                                 | Result                                                                             |
| -------------------- | ---------------------------------------------- | ---------------------------------------------------------------------------------- |
| First activation     | Initialized `Reusable` slot                    | Set `Live`; return generation 1                                                    |
| Later activation     | `Reusable`                                     | Set `Live`; return its current generation                                          |
| Current-handle check | Pool open, `Live`, equal generation            | Return true                                                                        |
| Retirement           | `Live`, equal generation g                     | Set `Retiring`; invalidate g; return one ticket                                    |
| Finalize             | Matching `Retiring` slot and unresolved ticket | Complete retirement and consume ticket                                             |
| Abandon ticket       | Matching `Retiring` slot                       | Set `Unavailable`; consume ticket; never publish index                             |
| Close                | Any                                            | Reject activation and new handle acquisition; permit existing retirement to finish |

For generation g below `UINT32_MAX`, retirement increments the generation to
g+1 while holding the mutex. For g equal to `UINT32_MAX`, retirement does not
increment; phase `Retiring` invalidates the handle. Finalization makes that index
`Unavailable` with reason `generation exhaustion`. It is never activated again.

Exhaustion handling is in `IndexReuse`, before calling `GenerationTracker::Bump`.
This change does not alter the standalone tracker used by `TextureBinder` or the
existing timeline strategy.

`TryRetire` distinguishes a duplicate of the outstanding retirement from an older
stale handle using the stored retiring generation. It makes no preliminary
`IsHandleCurrent` call outside the transition lock.

### 3.1 Finalization order

Under the core mutex, `Finalize`:

1. Checks ticket identity and unresolved state.
2. Completes pending and reclaimed counters.
3. Sets `Reusable`, or `Unavailable` for exhaustion/closure.
4. Marks the ticket consumed and constructs the result.

It releases the mutex before returning. The owner then publishes the returned
index. Nexus performs no further writes for that retirement after publication.
An owner callback can therefore reactivate and retire the index without old
cleanup overwriting the new state.

The owner must not expose an index before `Finalize` returns `kReusable`. The
owner's publication operation must be nonthrowing. Reserve free-list capacity
when increasing the number of physical slots; recycling must not grow a vector.

### 3.2 Ticket destruction and closure

Destroying an unresolved ticket abandons its index. It does not infer completion,
return the index, throw, or allocate. Increment `abandoned_retirements` so an
owner can detect the programming error. Moving over an unresolved ticket applies
the same rule to the destination's previous ticket.

`Close` is idempotent. It does not manufacture completion for live or retiring
slots. Existing live owners can still retire their matching handles after close.
Finalizing after close returns `kClosed` and publishes no index. Shared core state
survives the facade until the last ticket is resolved or destroyed.

Nexus closure does not destroy the owner's physical allocations. The owner closes
its resources through its own lifetime contract.

## 4. Frame-driven adapter

`FrameDrivenIndexReuse<IndexType, ContextType>` retains `ActivateSlot`, `Release`,
`IsHandleCurrent`, `OnBeginFrame`, and its telemetry interface. Internally it owns
`IndexReuse<IndexType>` plus prepared frame actions. It no longer owns a separate
generation/pending implementation.

### 4.1 Reserve before activation

Retirement scheduling must succeed without allocating after a slot becomes live.
Extend the existing `graphics::detail::DeferredReclaimer` with:

```cpp
auto PrepareDeferredAction(std::function<void()> action) -> PreparedDeferredAction;
auto CommitDeferredAction(PreparedDeferredAction&& action) noexcept -> void;
```

Preparation allocates the action node and its callable storage. Commit links that
node into a prepared-node list in the observed frame bucket under the existing
bucket mutex. Commit does not allocate. Keep the current vector storage for
ordinary `RegisterDeferredAction` calls; those calls do not acquire a new per-action
node allocation.

Stamp both kinds of enqueue with one bucket-local sequence under that mutex.
Draining detaches both containers and merges them in enqueue order. Reset the
bucket's sequence when detaching its containers. Actions registered from a running
callback enter the next drain. Both containers use the existing frame-completion
event and callback thread; there is no second reclaimer or second frame delay.
Catch an ordinary callback exception at dispatch and continue the detached batch.
Nexus completion callbacks themselves are nonthrowing.

Each activation in the frame adapter prepares one release payload and action
before activating the core slot. The payload has space for the retirement ticket
and `ContextType`. Supported contexts must be nothrow movable; the existing
`std::monostate` and `DomainKey` contexts satisfy this requirement. A duplicate
activation must not replace the current activation's prepared payload.

On `Release(handle, context)`:

1. Claim the matching activation's prepared payload under the adapter lock.
2. Call `TryRetire`. On rejection, preserve the live activation's payload and
   record the rejection; schedule nothing.
3. Move the returned ticket and context into the claimed payload.
4. Commit its prepared action. No allocation occurs in steps 1-4.

The adapter lock always precedes the core lock. Deferred execution never acquires
the adapter lock while holding the core lock.

### 4.2 Stable callback targets

The deferred action owns its payload, core ticket, and a shared recycle-target
state. It captures neither adapter `this` nor a raw resource owner. The target
state stores the existing recycle function and a closed flag; it does not own the
adapter or its array of prepared actions. This keeps the ownership graph acyclic.

The action finalizes the ticket, then invokes the target only for `kReusable`
while the target is open. The callback is nonthrowing and publishes to storage
reserved during allocation. The reclaimer detaches each action before invoking
it and never touches the recycled slot afterward.

`FrameDrivenSlotReuse` captures its free function by value through this stable
target. Remove its current callback capture of wrapper `this`.

Owners close their recycle target on the owner thread before destroying free-list
storage. A target already executing on that thread finishes before closure. A
committed action that runs later consumes its ticket without accessing a closed
owner. Worker release may enqueue actions; it may not mutate the owner's free
list. A callback may reenter allocation after finalization has returned.

Stop calls into the adapter before destroying its facade. Tickets and committed
actions may outlive the facade; method calls through a destroyed facade may not.
The frame reclaimer outlives its adapters, as in the existing owner layout.

Owner shutdown first performs its required GPU drain, then drains frame actions
while the target is open, then closes the target/core. Closing without recycling
is also supported when the owner is destroying the entire pool.

### 4.3 Allocation rollback

`FrameDrivenSlotReuse::Allocate` returns its freshly allocated descriptor to the
backend free function if preparation or activation fails. That descriptor was
never published to a GPU consumer and requires no frame delay.

For direct `FrameDrivenIndexReuse::ActivateSlot(index)` callers, the caller keeps
the index on failure. Geometry, material, and shadow owners must restore a popped
free-list entry or undo a newly appended entry before propagating failure.
Material activation also owns unpublished atlas elements. Atlas growth reserves
free-list and frame-retirement return capacity before exposing new elements, so
rolling those elements back cannot fail while unwinding activation failure.

## 5. Completion-controlled integration

The physical allocation owner starts retirement once its logical ownership
closes. It stores the ticket in its existing allocation record. Previously
acquired recording pins remain valid; they are not new acquisitions through the
retired CPU handle.

The completion owner calls `Finalize` only when all three counts are zero:

1. Logical/cache owners.
2. Unsubmitted recording pins.
3. Submitted uses that have not completed or been resolved by device teardown.

For GPU sharing, Graphics supplies completion and releases internal use pins.
Vortex owns the physical-slot record and Nexus ticket. Its completion callback
publishes the index on the renderer thread. No additional frame-ring delay is
applied after completion has been established.

An uncertain GPU submission keeps the slot unavailable for reuse by retaining its
ticket and use pins. A successful drain can authorize finalization. Device loss
closes the pool; finalization then destroys ownership without publishing indices
back to the lost device.

## 6. Existing callers

| Caller                              | Required integration                                                                                                                                                        |
| ----------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `GeometryUploader`                  | Keep real slot activation/release; reserve free-index capacity when growing entries; retain its shared free-list callback target and destructor drain                       |
| `MaterialBinder`                    | Same treatment as geometry; retain frame-start eviction processing                                                                                                          |
| `ConventionalShadowTargetAllocator` | Use the core retirement ticket for shared physical slots; stable pool state owns slot metadata/free indices                                                                 |
| `TransformUploader`                 | Use its dense counter and `GenerationTracker` directly; preserve current generation stamps and validity checks; remove frame-slot retirement machinery that has no releases |
| `DrawMetadataEmitter`               | Use its existing dense counter directly; it does not consume the generated handle generation; remove its unused reuse strategy                                              |
| `TextureBinder`                     | Keep direct `GenerationTracker` use, resident texture leases, descriptor repointing, and stale upload rejection unchanged                                                   |
| `TimelineGatedSlotReuse`            | Keep its existing API and scheduling; do not route managed shadow use through it                                                                                            |

Update the two dense publishers' statistics to describe emitted records and
stamps. Do not retain synthetic Nexus allocation/release telemetry for operations
that no longer use reusable slots.

## 7. Telemetry

Preserve the existing allocation, release, stale rejection, duplicate rejection,
reclaimed, and pending counters. Add only `exhausted_slots` and
`abandoned_retirements`. A rejected release does not increase pending. A consumed
ticket decreases pending once, including close and exhaustion. Snapshot counters
under the state lock so the relationship between pending and resolved tickets is
consistent.

## 8. Acceptance tests

| ID  | Test                                                                             | Required result                                                                                  |
| --- | -------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------ |
| N01 | Activate, retire, finalize, reactivate                                           | Old handle invalid immediately; index reusable only after finalization; generation advances once |
| N02 | Pause stale release after it begins; retire/recycle/reactivate on another thread | Stale release cannot retire the new generation                                                   |
| N03 | Recycle callback reactivates and releases the same index                         | New retirement remains pending; old callback performs no later state write                       |
| N04 | Duplicate release, duplicate finalize, moved-from ticket                         | One retirement and one reuse publication                                                         |
| N05 | Concurrent growth and handle checks/releases                                     | No invalid storage access or lost transition                                                     |
| N06 | Initialize generation near `UINT32_MAX`                                          | Last generation retires; no zero/one wrap; exhausted index never returns                         |
| N07 | Inject failure during preparation/growth                                         | No live slot created, lost free index, or changed generation                                     |
| N08 | Release/finalize with allocator failure injection enabled                        | No allocations and no exception                                                                  |
| N09 | Destroy facade/close target with outstanding actions                             | Stable state survives; closed owner is never called                                              |
| N10 | Drop unresolved ticket                                                           | Index becomes unavailable; abandoned counter increments once                                     |
| N11 | Two views and multiple frame cycles through dense publishers                     | Same indices, stamps, published data, and handle validity as before migration                    |
| N12 | Geometry/material eviction and texture stale-upload controls                     | Existing behavior preserved; no new allocation or queue wait on unchanged frames                 |

Use barriers/latches to control interleavings in N02/N05. Do not rely on stress
timing to establish the transition order. Test reentrant callbacks with storage
already reserved, so the test exercises ordering rather than allocation failure.
Use a test-only friend accessor to seed GenerationTracker before the N06 test
starts. Add no public generation setter or configurable production ceiling.

## 9. Files and dependency boundary

The owning implementation files are `Nexus/IndexReuse.h`,
`Nexus/FrameDrivenIndexReuse.h`, `Nexus/FrameDrivenSlotReuse.cpp`, and the existing
`Graphics/Common/Detail/DeferredReclaimer` files. Tests belong in the corresponding
Nexus and Graphics Common test directories. Caller edits are limited to the
classes in section 6.

`IndexReuse.h` includes Base/Core generation and index types, not Graphics queue
types. The Nexus module already depends on Graphics Common for its frame adapter.
Graphics Common must not include or link Nexus. Completion callbacks passed from
Vortex keep the dependency direction `Vortex -> Nexus -> Graphics Common`.
