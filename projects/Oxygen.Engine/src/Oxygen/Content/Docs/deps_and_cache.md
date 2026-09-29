# Content identity, ownership and caching

Read [identities](#identities), [owning and borrowed access](#owning-and-borrowed-access),
[loading and dependencies](#loading-and-dependencies), and
[release processing](#release-processing). Implementation status belongs to
[ED-M08.1](../../../../../../design/editor/plan/ED-M08-runtime-parity-and-standalone-validation.md).

Content produces decoded CPU assets and resources. Vortex/Graphics own GPU upload,
residency and fence retirement; returning a Content usage never waits for the GPU.

## Identities

| Identity           | Fields                                                             |
| ------------------ | ------------------------------------------------------------------ |
| SourceOrigin       | Persistent SourceKey and runtime SourceInstanceId.                 |
| Asset              | SourceInstanceId and AssetKey.                                     |
| Cooked resource    | SourceInstanceId, ResourceKind and resource index.                 |
| Synthetic resource | ResourceKind and producer-owned serial.                            |
| ContentId          | Nonzero monotonic uint64, never reused within the loader lifetime. |

Full identity equality determines interning; hashes select buckets. One owning
identity map and a nonowning ID index serve assets and resources. Base NamedType
provides strong IDs. Nexus recycled-slot machinery serves different lifetimes;
Content does not recycle its IDs.

Identity registration is lazy, owner-thread work. Creating a new key can allocate
and fail; existing-ID lookup is allocation-free. Mounting does not pre-register
whole resource tables. An already-active immutable generation mount is idempotent.
Reopening a retired generation or refreshing mutable content creates a new runtime
source instance, even when its persistent key and indices are unchanged.

Retirement removes a source from current-winner selection. Retained immutable
sources remain exactly readable. Mutable refresh revokes the old read capability
before its files are reused; a new ID cannot preserve overwritten bytes. Decoded
assets carry their exact SourceOrigin, and bound dependencies preserve their own
origins across replacement of active roots.

Cooked locators survive payload eviction while their source is readable or retained.
Refresh, trim and clear prune expired sources after eviction notifications. Synthetic
locators last for the loader's producer lifetime, including payload eviction, so
provided bytes can reload the same key. Memory scales with distinct touched locators;
hash-table bucket capacity is retained for reuse and included in memory baselines.

## Owning and borrowed access

| Operation                  | Ownership                                                      |
| -------------------------- | -------------------------------------------------------------- |
| `Load*` / `StartLoad*`     | One external usage per accepted request.                       |
| `Get*`                     | One external usage of the cached or context-bound publication. |
| `Peek*` / `Has*`           | Allocation-free inspection; no residency usage.                |
| Dependency binding         | One internal usage per distinct successfully bound child.      |
| `PinAsset` / `PinResource` | A move-only ResidencyPin, independent of CPU-data pointers.    |

Owning results are aliasing shared_ptr controls. Copies share that request's usage;
separate requests acquire separate usages, even when decoding is coalesced. Dropping
the last copy returns the usage automatically. The cache stores bare decoded data,
never a control that reserves its own entry. Getters can allocate and propagate
failure. Wrong-type requests return empty results without changing counts.

Content peeks borrow until the next loader mutation or suspension. IBL metadata
inspection uses `PeekTexture`; it does not allocate request controls. Core AnyCache's
thread-safe `Peek` separately returns shared CPU storage without reserving residency.

A Core UsageTicket records cache identity, entry incarnation, internal/external role
and checkout/pin kind. Returning an old ticket cannot debit a replacement under the
same ContentId. Explicit invalidation retires a publication even when clients retain
its CPU data. A retained, uncached publication remains usable without charging any
new cache entry. Pin destruction returns only that pin's exact usage.

## Loading and dependencies

The owner-thread pipeline resolves an exact source and joins or starts shared decode.
Workers read and decode into CPU data plus an identity-only DependencyCollector.
Decoder registrations and scheduling inputs are copied on the owner thread before
work is queued. Workers do not mutate loader/cache state, start nested loads, or retain LoaderContext
readers and spans beyond the decode call.

After decode, typed owner-thread binders load dependencies and freeze the complete
binding bundle before cache publication:

| Parent                       | Bound children                                                                   |
| ---------------------------- | -------------------------------------------------------------------------------- |
| Material                     | Descriptor texture slots and additional collector texture references.            |
| Geometry                     | Buffers and materials; mesh/submesh pointers share those controls.               |
| Scene                        | Collected textures/buffers, renderable geometry, material overrides and scripts. |
| Script                       | Source/bytecode resources and additional collector script references.            |
| Input mapping context        | Mapping, linked-trigger and auxiliary input actions.                             |
| Physics scene / input action | Leaves in this loading pipeline.                                                 |

Each binder records attempted keys before awaiting, including failed loads. Repeated
references to one missing or malformed dependency therefore make one attempt per
parent. The supported dependency types form an acyclic hierarchy. New dependency
kinds must preserve that contract; published bindings are immutable.

Data::Asset retains the read-only AssetRuntimeBindings interface. Content's compact
bundle stores child controls and exact publication metadata. It contains no back
reference to its parent. Shared parents and extracted children keep their dependencies
alive independently; clearing the cache does not dismantle a retained parent.
Contextual getters use these exact bindings before resolving unbound references.
There is no duplicate mutable dependency graph. Debug graph queries build snapshots
from current cached parents; evicted parents retain their data but leave that snapshot.

A shared decode result carries a temporary internal residency hold through waiter
delivery. Each surviving waiter acquires its own control against the original entry
incarnation. A callback can clear/reload the cache without redirecting later waiters
onto the new entry. Warm acquisitions create one control directly. Failed cache
admission can return valid uncached CPU data; revoked source results are rejected.

Stop closes the request's captured release epoch. Queued requests and suspended
work cannot publish or begin further child loads in a subsequent Run epoch. Run
creates a fresh epoch; old controls never join it. Allocation failure while constructing
a request control returns its ticket, and failed admission still reports committed
evictions.

## Release processing

Final control destruction publishes a preallocated record to a Content-owned atomic
queue. The normal return path allocates nothing, takes no throwing lock, invokes no
loader callbacks and performs no GPU work. Records retain CPU storage until the
owner thread processes them. Weak queue references avoid cycles through queued
parents and their child controls.

At engine frame start, including frames without views, ProcessPendingReleases handles
up to 128 records. Detaching a batch is constant-time; processing neither reverses nor
scans the backlog. An unfinished batch completes before newer arrivals, preventing
starvation. A record may destroy a large payload, so the count limit bounds bookkeeping,
not elapsed time. Measure the largest payload destruction separately.

Explicit trim and memory-pressure recovery drain pending returns fully. Trim removes
policy-eligible entries, drains child returns, and repeats until no further entries
become eligible. Active callers, bound children and pins retain their usages. The
RefCountedEviction cache keeps one baseline residency reference until trim, budget
policy or explicit invalidation removes the entry. Its current default cost is one
unit per entry; byte-weighted accounting remains
[CNTT-BUDGET-01](implementation_plan.md#cpu-budget-accounting).

Shutdown closes enqueueing before invalidating the cache. Remaining queued records
then dispose CPU storage without touching cache accounting. Late pointer destruction
also releases storage without accessing the former loader. Drains retain their queue
through reentrant payload destruction; closure prevents further access to a retired
cache. No callback from an old epoch can resume work in a replacement epoch.

Eviction notifications carry the retired incarnation, payload and type. Callbacks run
outside the cache lock. Their registration remains alive during dispatch; callbacks
may reenter cache APIs. Committed events are delivered before a notification exception
propagates, unless the cache itself has been destroyed.

## Validation

Native tests cover exact tickets, collision-safe IDs, source refresh/retirement, lazy
reload, coalesced request controls, retained parents, extracted children, cancellation,
allocation failure, reentrant callbacks and enqueue/close races. Release measurements
track acquisition/return cost, bounded batches and largest-payload destruction.
Renderer checks preserve allocation-free IBL inspection and existing GPU retirement.

The reference model is UE5.7.4 FStreamableHandle request ownership
(`Engine/Classes/Engine/StreamableManager.h` and `Private/StreamableManager.cpp`).
Oxygen additionally supports any-thread final CPU-pointer destruction through its
owner-thread release queue.
