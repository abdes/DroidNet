# Conventional local-shadow sharing

## 1. Result and scope

Compatible views share one physical depth map and one successful depth producer
for each local light. Each view retains its own light selection, shadow-reference
mapping, resolution decision, filtering, fade, and strength. Directional cascades
and contact-caster depth remain view-owned.

Use `ConventionalShadowTargetAllocator` for chunk placement, `ShadowDepthPass` for
depth production, `ShadowCasterDependencies` for producer inputs, and the existing
frame-binding publishers for GPU records. Extend these owners in place. There is
one local-shadow allocation/content path after migration.

The supporting Nexus contract is
[Slot retirement and completion-controlled reuse](../../../src/Oxygen/Nexus/Docs/slot-retirement.md).

The required outcomes are:

- Partial overlap and different light-list orders share matching individual maps.
- Changing one light or caster invalidates only affected content variants.
- Removing a view does not unregister storage used by another view or recording.
- Delayed managed uses retain their resources until execution completes.
- Memory exhaustion reports the existing lighting-preparation failure. It does
  not drop requested shadows, reduce resolution, or wait for GPU space.
- At most one backend incarnation is active or retiring in a process.

## 2. Module and ownership boundaries

| Owner                     | Responsibility                                                                                                                                   |
| ------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------ |
| Graphics Common           | Managed registration, immutable registered views, recording use batches, submission outcomes, completion receipts, and final resource retirement |
| D3D12 / Headless          | Execute, signal, query completion, wait on another receipt, report device loss, and implement native lifetime retention                          |
| Backend loader            | One backend incarnation, close/reload exclusion, module ownership, and destruction ordering                                                      |
| Nexus                     | Slot generation, immediate logical retirement, and completion-authorized finalization                                                            |
| Vortex shadow service     | Canonical content identity, view aliases, map versions, publication, and whole-backing access ordering                                           |
| Existing shadow allocator | Resolution/projection chunks, physical layer placement, and budget admission                                                                     |

Graphics does not include or link Nexus. Vortex connects Graphics completion to a
Nexus retirement ticket held by its physical-slot owner. Opaque internal use pins
let Graphics retain Vortex state without knowing shadow types.

### 2.1 Required object graph

| Object                               | Owns                                                                             | Does not own                                                                     |
| ------------------------------------ | -------------------------------------------------------------------------------- | -------------------------------------------------------------------------------- |
| External registration/content lease  | Backend owner token and registration/map state                                   | Renderer or pass                                                                 |
| Registry managed entry               | Registration core and underlying resource                                        | External lease or strong Graphics owner                                          |
| Registration core                    | Native resource, registered views, descriptor allocations, registration identity | Graphics facade                                                                  |
| Shadow pool state                    | Chunk owners, slot/free-list metadata, Nexus core, weak version/slot lookups     | Strong map-version references, Graphics facade, Renderer, ShadowService, or pass |
| Map version                          | Canonical content record and physical-slot state                                 | Pool, view, or pass                                                              |
| Physical-slot state                  | Chunk owner, use counts, retirement ticket, weak pool return target              | Strong pool reference                                                            |
| Recording                            | Backend owner token, command list, recorder, internal use batch                  | View/pass through raw callbacks                                                  |
| Submitted retirement batch           | Command list, native allocator, internal registration/map pins, receipt          | External lease or strong Graphics owner                                          |
| Command-pool return state            | Existing free-list buckets/mutex, closed flag, native lifetime                   | Graphics facade or active checked-out lists                                      |
| Diagnostic native-resource reference | Native allocator/device/module lifetime through the native backing               | Permission to record another GPU use                                             |

This graph prevents `Graphics -> in-flight batch -> Graphics` and
`Graphics -> registry -> client wrapper -> Graphics` cycles. When a public lease
is attached to a recording, copy its internal pin into the batch; do not copy the
public lease into backend-owned retirement storage.

Vortex allocation/cache facades hold a backend owner token while open. Their
shared pool state contains only internal references, so a completion callback can
outlive those facades without retaining them or forming a cycle.

View aliases and retained content leases own versions; recorded/submitted uses
pin those versions internally. A version owns its slot, and a slot owns its chunk.
The pool's version/slot lookups are weak. A slot's free-list return target is weak;
if that target has closed or expired, finalization consumes the ticket without
returning an index. This prevents a `pool -> version -> pool` cycle.

Registration ownership and use retention have separate counters. The chunk holds
an internal allocation-owner token that keeps registration acquisition open. A
public RegistrationLease combines such an owner token with the backend owner
token. A use pin retains the core but cannot reopen acquisition. Dropping the last
allocation-owner token closes acquisition even if GPU use pins still exist.

## 3. Managed registrations

### 3.1 Identity and operations

Use a registration identity containing `BackendIncarnationId` and a monotonically
allocated `RegistrationId`. IDs are never reused within an incarnation. Reject
allocation on counter exhaustion before creating a native object. A resource
address is not a registration identity.

Add a copyable `RegistrationLease` to the existing registry API. Its operations
are:

| Operation                                | Contract                                                                                                                        |
| ---------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------- |
| `RegisterManaged(resource)`              | Create one managed entry or acquire its existing open managed identity; fail for a closing entry or a manual ownership conflict |
| `AcquireManaged(identity)`               | Return a lease only for the same open managed entry                                                                             |
| `AcquireManagedView(lease, description)` | Return an existing equal view or transactionally append a new view                                                              |
| `RetainUse(lease, recording)`            | Retain the internal registration core before the first GPU command references it                                                |
| Last external owner release              | Close acquisition when no cache/allocation owner remains; retain existing recording/submitted pins                              |
| Final retirement                         | Unregister views/state/resource once all use pins are released                                                                  |

The managed acquire/create/use operations return `std::expected` with
`RegistrationError::kClosed`, `kWrongBackend`, `kStaleRegistration`,
`kOwnershipConflict`, or `kAllocationFailed`.
Raw mutation of a managed entry throws `std::logic_error` before changing native
descriptors or registry maps, including boolean-returning UpdateView. Manual
entries keep their existing failure behavior. Closing/finalization is idempotent
and nonthrowing.

The registry stores the underlying resource, as `RetainedTexturePool` already
does. Registering a client wrapper with a backend-owning deleter is prohibited.

A live managed entry rejects raw `UnRegisterResource`, `UnRegisterViews`,
`UnRegisterView`, `UpdateView`, and `Replace`. `UpdateView(destination, index)`
checks both the index's current owner and the destination. Raw view registration
cannot append to a managed entry; callers use `AcquireManagedView`.

Managed view creation validates the registration identity before native view
creation and again before publication. The operation holds a registration pin
through both steps. A closing or replaced identity cannot receive a late view.

Existing manual registrations keep their current API. A native resource cannot
have manual and managed registration ownership at the same time.
Index native ownership as well as wrapper identity, so constructing another
wrapper for the same native backing cannot bypass this exclusion.

A new registration is rolled back completely if its construction fails. On an
existing registration, successfully appended immutable views remain cached when a
later operation fails; do not remove a view another recording may have acquired.
Each description can allocate at most one published descriptor.

Descriptor return must be allocation-free, including transaction rollback and
last-owner destruction. Reserve raw-segment and bindless free-list return capacity
before publishing allocations; descriptor handle cleanup is noexcept and cannot
depend on a later vector growth succeeding. Inject allocation failure during
rollback and final descriptor release to verify this contract.
Resource-state retirement must also avoid allocation: erase the resource from
queue state tables without constructing temporary queue vectors or deduplication
sets. Otherwise allocation failure could leave a stale native-address state entry
after registration cleanup.

### 3.2 Descriptors and heaps

Published descriptor contents are immutable. A changed backing receives fresh
descriptors. Additional DSVs are appended lazily, once per layer description.
Hash lookup must compare complete view descriptions, including dimension, format,
range, mip/layer selection, and read-only flags, before returning a cached view.
Use the existing registry view cache; extend its equality handling rather than
adding a second cache.

Resource and sampler shader-visible heaps are fixed for the backend incarnation.
Keep the current no-growth D3D12 configuration. Reject replacement of a bound heap
or participating queue while managed registrations/recordings remain. Heap
exhaustion is an allocation failure. This work adds no heap migration system.

The recording's backend/native lifetime token retains the bound heap incarnation.
DSVs remain registration-owned for simple reuse; no descriptor ownership is
copied per draw. Native test consumers must retain a managed use before using a
raw SRV index or texture reference.

## 4. Recording and completion

### 4.1 Use batch

`CommandRecording` owns one `RecordingUseBatch`. It deduplicates registration IDs
and physical map/version IDs. The first attachment reserves space and retains
internal pins before commands use those bindings. Later stages in the same view
recording reuse the batch.

Batch storage, retirement storage, and submission bookkeeping are allocated
before native execution. Allocation failure discards the recording before issue.
Submission transfers the prepared batch; it does not construct another vector of
pins after execution.
Keep the batch buffers and retirement node with the existing pooled command-list
entry. Clear contents on completion and retain capacities for the next recording.
Grow capacities before issue. This avoids a new steady allocation for every
managed submission after the command-list pool has warmed.

### 4.2 Typed result and receipt

Keep `CommandRecording::Submit() -> bool` for current callers. Add
`SubmitWithReceipt() -> SubmissionResult`; both call the same finalization path
and return the stored terminal result on repeated calls.

```cpp
struct CompletionReceipt {
  BackendIncarnationId backend;
  QueueId queue;
  uint64_t value;
};

enum class SubmissionOutcome {
  kDiscarded,          // no native execution was issued
  kSubmitted,          // execution issued; receipt established
  kExecutionUncertain // execution issued; receipt not established
};

struct SubmissionResult {
  SubmissionOutcome outcome;
  std::optional<CompletionReceipt> completion;
};
```

For managed batches, `kSubmitted` always includes a receipt. The existing boolean
API returns true only for `kSubmitted`. Extend `OnSubmission` handling to the
third outcome: both failure outcomes invalidate publication; only discard permits
immediate release of unsubmitted pins. Graphics retains uncertain pins itself.
Audit every existing submission callback for this enum change.

Discard and destruction are nonthrowing and close a native list at most once. If
End fails before issue, resolve Discarded, release unsubmitted pins, and destroy
the unusable list/allocator instead of returning it to the pool. Discard cannot
change an ExecutionUncertain terminal result or release its quarantined pins.

Calling `SubmitWithReceipt` on an unresolved recording requests a receipt even
when its use batch is empty. Calling it after a legacy submission returns that
submission's stored result; it does not append a retrospective marker. Callers
requiring a receipt select `SubmitWithReceipt` before the first terminal action.

A receipt is a value, not a resource-use lease. It does not permit a new use or
keep a closed backend open. Receipt operations reject a different backend ID or
an unknown queue ID. Participating queue identities are stable until backend
closure; queue role alone is not identity.

Both backend submission overloads execute through one internal prepared-submission
routine. `CommandRecording` supplies the lists, prepared retirement nodes, use
batches, and final-state updates. Raw queue submissions use that same routine with
empty managed-use batches. They cannot submit a list owned by an unresolved
managed recording outside its recording owner. The internal routine reports which
native groups were issued and resolves each prepared node from that result;
classification does not depend on the exception's C++ type.

### 4.3 Queue timeline and issue order

Each queue has a private completion timeline. Callers cannot reserve its values.
Managed submission assigns its value under the queue submission mutex in actual
issue order. Existing caller-reserved signal APIs remain separate.
Valid emitted values are 1 through `UINT64_MAX - 1`. Reject exhausted private
timelines and legacy requests for zero/`UINT64_MAX` before issue; never wrap a
counter or emit the device-loss sentinel as a normal completion value.

One queue submission mutex covers validation, native waits, execution, signals,
and completion-marker emission. Frame-end signals and explicit drains use the
same submission mutex. Recording draw commands takes no queue submission lock.
Legacy value reservation is synchronized with that mutex; locked internal helpers
do not reacquire it. The lifecycle gate is acquired before the queue mutex.

Submission steps are:

1. Check that the backend accepts submission and that all attached uses are
   valid. The backend lifecycle gate excludes close from an in-progress submit.
2. Prepare the in-flight node, final recorded states, and storage needed to adopt
   those states. Validate all legacy queued signals against the last emitted
   value and against earlier actions in this submission.
3. Accept typed dependencies only for private markers already emitted by their
   producer queue. Reject a legacy self-wait above that queue's last emitted
   legacy signal. Enqueue cross-queue waits and omit waits on earlier markers of
   the same queue. Dependencies therefore point only to earlier issued work; no
   dependency-graph component is required.
4. Mark the prepared node `Issued` immediately before `ExecuteCommandLists`.
5. Emit legacy signals and one private completion marker for the managed batch.
6. Commit the preallocated state bookkeeping, link the node to in-flight storage,
   and return the receipt. No allocation occurs in steps 4-6.
7. Resolve publication callbacks after releasing queue/lifecycle locks.

Validate command-list state before issue. The Submitted state transition and
terminal-result storage after issue are nonthrowing. An already established
receipt is never discarded by logging or a publication callback exception.

Remove the shadow producer's current reserved `Signal()/RecordQueueSignal` pair.
Its completion receipt serves both publication and retirement. Keep one writer
recording per unique dirty light initially; therefore a writer batch can contain
one light. Do not claim that this removes all per-light producer signals.

Add `RecordDependency(CompletionReceipt)` to the recording API. D3D12 resolves the
producer's fence and calls the consumer queue's native `Wait(producer_fence,
value)`. The existing `RecordQueueWait(uint64_t)` targets its own legacy fence and
must not receive private receipt values. Coalesce receipt dependencies by taking
the maximum value for each backend/queue identity.

Completion polling returns `Pending`, `Complete`, or `DeviceLost`. Treat native
`UINT64_MAX` as `DeviceLost`. A registration used on several queues is retained
until each of its submitted uses completes. No numeric comparison crosses queues.

For the span submission overload, prepare all bookkeeping before the first
execution call and record which native groups have been issued. A failure before
any group is issued is discard. A later failure quarantines the issued groups;
unissued groups are discarded. Do not relabel the complete span as unissued.

### 4.4 Failure after issue

If execution was issued and no receipt was established, retain the command list,
allocator, registrations, and map pins in the prepared quarantine node. Invalidate
publication and mark the backend faulted. A faulted backend rejects new recording
acquisition/submission and does not advance frame retirement.
The fault flag is separate from the loader lifecycle state: it applies while an
incarnation is Active and is cleared only by successful recovery.

The owner thread attempts a drain using fresh ordered queue markers. This is a
failure-recovery operation, not a normal sharing path. If the drain succeeds,
close the affected managed registrations and invalidate every map version in
affected shadow chunks before releasing quarantined uses. Reconcile the final
state of every resource touched by issued work, including manually registered
buffers and render targets, from the prepared state records and known submission
order. A successful drain proves completion, not the correctness of stale CPU
state tracking. If any affected state cannot be reconstructed reliably, close the
backend instead of resuming it. Only then process finalizers and clear the fault
flag. Fail the current view; a subsequent preparation recreates
the affected chunks on demand;
do not continue using resource states skipped by the failed submission. Unaffected
completed chunks retain their cache contents. If the device is lost or the drain
fails, close the backend and destroy its resources without recycling into it.

Headless keeps its asynchronous executor. Add test controls to pause completion
and inject rejection before issue, failure after issue, and device loss. Do not
substitute immediate completion for these cases.

### 4.5 Retirement progress

Add nonblocking owner-thread `Graphics::PollCompletedUses`. Call it from BeginFrame
after frame-slot waits and before frame callbacks, from EndFrame after markers,
and from Graphics::Flush after the queue drain. Out-of-frame submission owners
call it while collecting their completion receipts. It does not depend on the
async nursery or start a background service.

Polling queries each queue's private timeline, consumes completed batches in
submission order, and runs eligible registration/slot finalizers. A worker's last
owner release queues its already allocated registration finalization node in the
managed retirement state; it never invokes a renderer callback directly. Polling
processes these ready nodes without a second frame-bucket delay. Queue::Flush by
itself waits for execution; Graphics polling performs the managed cleanup.

After close has drained submitted work and closed renderer callback targets,
late CPU releases can finish registration/native cleanup under the lifetime
state's lock on their releasing thread. They cannot publish a reusable shadow
slot or call a closed renderer target. Thus Retiring does not require another
frame or a surviving renderer thread to release its last CPU-only owner.

### Readback submission and retained mapping ownership

Native capture readbacks participate in the same managed-use protocol. Staging
buffers and MSAA resolve textures hold internal allocation owners; each recorded
copy acquires use pins before recording native commands. Reset releases the
allocation owner and invalidates its ticket, while recording/submitted pins keep
the backing and registry state alive until discard or actual completion. Normal
`ResetForReuse` keeps a completed allocation for subsequent copies.

A readback ticket starts unsubmitted. Only a successful submission callback arms
fence completion; discard/uncertain issue cancels it. A frame marker passing an
unsubmitted ticket's number cannot publish copied bytes. Shutdown cancels unissued
copies instead of waiting for unsignaled values. Device loss produces a backend
failure result, never successful copied bytes.

Readback facades and mapping guards use the existing Common-compiled ownership
helper. Facades retain the canonical Graphics owner; mapping guards retain their
facade. Backend cleanup returns to Common before the final owner/module token is
released. Dead weak facade observers do not keep the incarnation retiring.

## 5. Backend close and restart

A process-wide gate in Loader maintains one incarnation with states `Active`,
`Closing`, `Retiring`, and `Released`. `LoadBackend` during `Closing` or `Retiring`
throws the existing `loader::InvalidOperationError` with `BackendRetiring` in its
diagnostic message. It does not call the backend factory. Repeated load while
`Active` returns the same canonical owner. A new ID is allocated only after
`Released`.

The gate contains one retirement record and an ID counter, not a collection of
backends. It survives a loader facade's destruction. Serialize factory creation
under the gate so two concurrent load calls cannot create separate owners. A
failed creation releases the reservation; its consumed ID is not reused.

The canonical owner is a Loader-owned `BackendOwner` containing the factory
pointer and destruction hook. Return public `shared_ptr<Graphics>` values as
aliases of that owner. The backend's singleton keeps its internal object owner;
the Loader owner invokes its destruction hook exactly once. This preserves the
factory ABI without constructing independent public control blocks on reload.

`UnloadBackend` requests close; it does not unconditionally unload the DLL. The
loader drops its active owner after requesting close and retains a retirement
record that observes completion without strongly owning Graphics. Existing
external owners keep the canonical backend owner alive. Native references can
keep the native lifetime state alive after the Graphics facade is destroyed.

### 5.1 Native lifetime state

Add backend-native lifetime state owning the device, native memory allocator,
descriptor heaps required by outstanding managed uses, and module-retention
token. It does not own Graphics, its registry, or its in-flight queues. Every
owned GraphicResource retains this state through actual native destruction,
including manually registered resources. Registration mode does not change. The
budget reservation remains in `GraphicResource` and is released after its native
resource/allocation references.
Queue/fence cleanup uses the retained native device state directly. It does not
query a Graphics facade whose component teardown has already started.

Module retention must span the entire call into a backend destruction function.
Place the outer module-retaining deleter in Loader/Common code: invoke the backend
cleanup function, return from it, then release the module token. Releasing the DLL
from a member destructor while backend code is still on the stack is prohibited.

Use one type-erased ownership helper implemented in Graphics Common for backend
objects that escape the backend: it accepts an object pointer, a nonthrowing
backend destroy function, and a lifetime token, and creates the shared ownership
control block in Common code. Graphics and buffer/texture factory results use
that helper. Its deleter retains the token across the destroy call. Do not define
the outer deleter as a backend-instantiated template or leave these escaping
objects in backend-owned `make_shared` control blocks.
Typed Common entry points preserve `Texture::shared_from_this()` initialization;
aliasing a texture from an untyped owner alone would break existing consumers.

At deletion, move the strong lifetime/module capture into a local variable before
invoking the destroy function. The stored deleter is then empty. Weak observers
that retain a dead shared-pointer control block must not keep the module,
allocator, or backend incarnation retiring.

The loader's retirement record observes both canonical-owner destruction and
native-lifetime destruction. Only then does it become `Released`. Keep the
existing singleton backend factory; the loader's exclusion prevents a second
control block from adopting that singleton while it is closing.

Install the lifetime tokens through Graphics after the existing backend factory
returns. Keep GraphicsModuleApi's factory/destruction signatures unchanged. All
public backend owner tokens refer to the loader's canonical owner; do not create
another owning shared pointer from the raw factory pointer. Backend-internal
`shared_from_this` references are not substitutes for that external owner token.
Graphics stores weak access to the canonical owner; external leases and recording
acquisition obtain their strong token through that access. Registry cores and
native lifetime state never retain that strong token.

Serialize loader lifecycle transitions. Invoke close, backend destruction, and
module release outside the loader mutex. Completion updates a stable retirement
record rather than calling a raw loader pointer that may have been destroyed.
Admission is scoped to a CPU operation. Nested factories on that thread share the
outer lifecycle lock, avoiding recursive shared-mutex acquisition when a queued
close is waiting. The scoped guard cannot move across threads or survive a
coroutine suspension; it performs no allocation.

### 5.2 Close sequence

1. Acquire the lifecycle gate exclusively, transition to `Closing`, and stop new
   managed acquisitions, recordings, resource creation, and public submissions.
   Release the gate after in-progress submission calls have finished.
2. Mark unresolved recordings for discard. Their owning threads close/discard
   their native lists; close never calls into a recorder concurrently with its
   recording thread. They retain their pins until this happens.
3. Shut down readback acceptance and drain already submitted work with internal
   ordered markers. Drain operations remain available during close.
4. Process completed/quarantined batches and finalizers on the owner thread.
   Device loss selects destruction without reuse.
5. Close managed registrations and Vortex pool targets. External leases become
   closed for new GPU use; native inspection references can still retain bytes.
6. Transition to `Retiring` while external owners, unresolved recordings, native
   references, or final destruction remain. Do not block waiting for an arbitrary
   caller to release a CPU reference.
7. Destroy registry views before their allocators, allocations before the native
   memory allocator, and backend objects before releasing the DLL. Mark the
   incarnation `Released` after the last destruction completes.

Close/drain and renderer finalizers execute on the owner thread. After that drain,
late CPU releases may destroy retained native objects on their releasing thread;
they do not run renderer callbacks or start another drain. Backend destruction
recognizes an already closed incarnation and performs native teardown only.

Run this sequence even when the async nursery was never activated. `IsRunning`
does not decide whether resources need closing. Destructors do not treat
`ProcessAllDeferredReleases` as a GPU completion proof. Queue destruction waits
for submitted markers established by close, not for unsignaled reserved values.

`CommandRecording` releases its recorder and public backend token after transferring
the list/use batch to submitted or quarantine storage. A resolved recording shell
contains only its terminal result. Move the existing command-list pool's return
bookkeeping into shared `CommandPoolState`. Checked-out list deleters retain that
state rather than raw pool `this`. The state owns only idle lists; active list
control blocks are held by their callers/batches, so there is no ownership cycle.

Closing the pool marks that state closed, clears the Graphics-dependent factory,
and destroys idle lists. A late return to a closed state destroys its list instead
of placing it back in the pool. The deleter moves its state capture into a local
before cleanup, so weak list observers cannot retain the native lifetime after
the last strong list reference. The state/native allocator remain alive until
that cleanup returns. Preserve the existing pool and its queue-role buckets.

## 6. Shadow identities and records

### 6.1 Physical storage

`StorageId` contains backend/registration identity, Nexus physical-slot index and
generation, first layer, layer count, resolution, format, and projection layout.
An ordinary projected map uses one layer; a cube map uses six consecutive layers.

Retain current chunk sizing: target 64 MiB, at most 64 maps, at least one complete
projection. Retain the existing retry with exact requested layers when spare
capacity does not fit the lighting allocation budget.

Remove view ID from physical chunk partitioning. Keep view ID in aliases and
quality history. A view alias maps `(scene lifetime, ViewId, light NodeHandle)` to
its selected content version and consumer settings. Selection-list positions are
stored only in that view's publication.

### 6.2 Canonical producer identity

`LocalShadowContentKey` contains:

- Scene lifetime and light NodeHandle, including its source generation.
- Projection kind, resolution, depth format, and depth-producer contract version.
- Light-space matrices, origin/range, and producer-side bias values.
- Canonical relevant caster records, compared by value after hash lookup.

Extend `ShadowCasterDependencies` to retain its existing input fields instead of
only their hash: node/submesh identity, geometry asset/LOD/generation/content
revision, geometry binding identity, topology/ranges, transform generation and
matrix, raster flags, and masked material/texture inputs. Preserve material
generation, alpha/cutoff, UV fields, sampled texture identity/revision, and flags
that change masked coverage. A sampled texture with unknown continuity prevents
reuse; a material that performs no texture sampling needs no texture revision.

Canonical records describe actual prepared inputs. Two views selecting different
LODs have different records. Intern equal records across prepared views; do not
intern only by node or scene generation. Canonicalize once per prepared snapshot,
then derive each light's membership. Reuse scratch vectors and record storage.

Light membership is computed from all prepared shadow casters, including newly
entering casters. Keep unknown/invalid bounds conservatively. Preserve the
existing off-screen shadow-only extraction and influence-sphere light admission.

Storage address, target layer, view ID, selection order, and frame number are not
semantic producer inputs. They validate placement/publication, not shared content.
Geometry binding generations and authoritative content revisions remain checks
even when target placement is removed from the old fingerprint.

### 6.3 Depth-producing and consumer settings

| Input                                                                     | Content key                                          |
| ------------------------------------------------------------------------- | ---------------------------------------------------- |
| Light transform/range, projection, resolution, caster inputs              | Include                                              |
| Projected-spot constant/slope depth bias                                  | Include                                              |
| Cube depth encoding, face orientation, depth-producing shader permutation | Include through contract version and matrices/layout |
| Cube receiver comparison bias and normal offset                           | Exclude; publish per view                            |
| PCF weights/count, fade, shadow strength                                  | Exclude; publish per view                            |

Use the established reversed-Z D32 cube producer: unbiased raster depth, physical
faces `-X,+X,-Y,+Y,-Z,+Z`, and receiver-to-light cube sampling. Cube SRVs are
`TextureCubeArray`; the cube index is `first_array_layer / 6`. Ordinary projected
spots retain their linear depth and producer-bias path. Changing those producer
contracts increments the explicit producer contract version and invalidates
affected cached content. Sharing does not change their filtering or bias formulas.

## 7. Preparation, allocation, and publication

### 7.1 Prepare requests before placement

Split point/spot setup into allocation-independent preparation and binding:

- Preparation computes quality, matrices, producer key, consumer parameters, and
  view-local light identity without an SRV or target layer.
- Binding writes the acquired canonical SRV/layer into the view's existing
  `ProjectedLocalShadowRecord` or `CubeLocalShadowRecord`.

There is no placeholder descriptor used to pass the old allocation-validity check.

After `PrimePreparedViews` and frame light selection, prepare the active family's
local requests once. Reuse the existing prepared-view collection and pass the
family to `ShadowService::PrepareLocalRequests`. This is CPU preparation in the
existing service, not a new render graph. Directional/contact rendering stays at
its existing per-view stage.

Key the preparation cache by frame sequence and a CPU preparation revision
advanced whenever prepared view snapshots are rebuilt. A same-frame offscreen
re-preparation gets a new revision. This revision controls CPU cache lifetime; it
does not enter semantic shadow content identity or the GPU ABI.

Preparing the family before placement establishes which aliases request each
content variant. It permits a common dynamic update to reuse one physical map
instead of allocating a new map merely because the second view has not rendered
yet. The single-view entry point prepares a family containing that view. Aliases
outside that preparation cannot supply new GPU readers through an expired frame
publication; independently retained content leases are counted separately.

### 7.2 Acquire and render

For each prepared request:

1. Look up an equal, readable content version using the canonical key.
2. On a hit, acquire its read capability and bind its canonical placement into
   the view's record. Create no writer recording and no same-queue self-wait.
3. On a miss, choose a safely reusable physical slot or acquire another slot from
   the existing allocator. Reserve the registration, required views, version
   record, alias updates, and retirement storage before publishing anything.
4. Record one light through `ShadowDepthPass`, pinning its target and using the
   current prepared writer inputs. Finish and submit that writer before proceeding
   to a second view that can consume it.
5. On `kSubmitted`, publish the content version with its receipt. On discard,
   leave it invalid and release unsubmitted ownership. On uncertain execution,
   quarantine the backing as specified in section 4.4.
6. Commit the view's alias/reference changes only after acquisition and writer
   publication succeed. Failure leaves previously published versions intact.

The existing per-light cache becomes the shared version cache; do not retain a
separate per-view content cache beside it. DSV lookup remains registration-owned.
Missing trustworthy dependencies select independent view-owned variants in the
same allocator and render path, with content reuse disabled for those variants.

Writer production is serialized on the renderer thread. A version in `Recording`
is not a hit. Native callers requesting it receive `NotReady`; they do not record
a speculative read. A failed first view does not install a valid shared result;
the next view can produce a fresh version after proven discard.

### 7.3 Frame publication and retained content

`ShadowFrameReadSet` contains the current frame/preparation identity, map-version
read capabilities, and the view's existing GPU binding/reference/record indices.
Attach it once to the owning view recording before local-shadow consumers. It is
valid for that frame publication and cannot be attached after its publication
closes. EndFrame, snapshot replacement and service close seal the publication. A
CPU admission guard also rejects an attached-but-unsubmitted recording after
sealing; the guard alone adds no private GPU fence. Existing transient publishers remain in use.

`ShadowContentLease` retains a map version independently of frame bindings. It can
be used in a later frame to publish fresh view-local records or to attach an
explicit native capture. It does not expose an old frame's transient SRVs as a
valid later-frame publication.

Current `SceneRenderer` view recordings and shadow writers remain scoped to their
render calls and submit before those calls return. A caller-owned managed recording
may span frames only when its other inputs also have retained lifetimes. Native
delay tests use owned buffers/root constants and registered targets, not expired
frame-ring allocations. No general retained-frame renderer is introduced.

GPU generation checks in the existing binding loaders remain ABI validation. They
are not a substitute for retaining the referenced descriptors and bytes.

## 8. Version changes and whole-backing hazards

A map version is `Recording`, `Submitted`, `Invalid`, or `Quarantined`. Submitted
versions can be read before CPU-observed completion through queue ordering. A
separate admission flag controls whether a version can acquire new readers.

Use these rules for changes:

| Condition                                                                                           | Action                                                                       |
| --------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------- |
| Equal producer key and readable version                                                             | Reuse version and storage                                                    |
| Changed key; no old read capability, no unsubmitted pin, all submitted uses precede the write       | Seal/remove old lookup; create new version in the same slot                  |
| An old content lease/publication can still attach a reader, or an old recording remains unsubmitted | Use another slot; preserve old content                                       |
| An unordered reader/writer exists on another queue                                                  | Order the whole backing with receipt dependencies, or choose another backing |
| Required storage exceeds budget/capacity                                                            | Report the existing preparation failure; preserve other published versions   |

Before an in-place update, reconcile current family requests and close superseded
frame-publication acquisition. An independently retained `ShadowContentLease`
prevents overwriting its version. Cache entries are not permission to acquire a
sealed version; remove their lookup routes before recording the replacement.

All shadow writers use the graphics queue. Track hazards at chunk/backing
granularity because barriers currently address all subresources. A free layer in
a backing with an unsubmitted reader is not an independent copy-on-write target.
Choose a different backing in that case. On one queue, record/submit accesses to
each backing in order; do not record two first-use transitions from the initial
depth-write state and then submit them in an arbitrary order.

For a submitted cross-queue reader, a later writer waits on its receipt before
the backing transition. A cross-queue reader waits on the published writer receipt
and adopts the producer's established shader-resource final state. Keep this
handoff in the shadow-use attachment path; add no global resource scheduler.

Read/read overlap is allowed only while both accesses leave the backing in the
same shader-resource state. Native shadow readback copies execute on the existing
graphics queue and restore shader-resource state before their receipt marker.
Treat the copy's state-changing interval as exclusive backing access: wait for
submitted uses on other queues and return `NotReady` if an unsubmitted use on
another queue prevents ordering. Record its receipt as the backing's latest
exclusive-access receipt. Later readers depend on that receipt as well as the
content producer, coalescing values per queue. This covers copy-versus-sampling
barriers without adding a copy-queue ownership-transfer protocol.

Physical-slot retirement starts once no alias/cache allocation owner remains.
Finalize its Nexus ticket after all recording and submitted pins resolve. An old
sealed version completing does not free a slot still owned by its replacement.
Retire the chunk registration only after every physical slot in it retires.

## 9. Consumers and diagnostics

| Consumer                                    | Attachment point                                                                                   |
| ------------------------------------------- | -------------------------------------------------------------------------------------------------- |
| `ShadowDepthPass`                           | Target write use before clear/draw; producer result before cache publication                       |
| `SceneRenderer` view recording              | One `ShadowFrameReadSet` after successful shadow publication and before local-shadow stages        |
| Deferred lighting                           | Reuse that view batch; retain existing whole-surface state transitions                             |
| Forward base pass                           | Reuse that view batch before the shared forward shader runs                                        |
| Translucency                                | Reuse the same batch; do not attach per draw                                                       |
| Native probes/readbacks and view extensions | Explicit content-use lease before native commands; retained capture inputs if execution is delayed |

Inspection APIs return CPU metadata or a diagnostic native reference. Neither is
accepted as a GPU-use lease. A diagnostic native reference keeps its native
lifetime and allocation charge, but does not prevent map contents from changing.
An image capture requires a content read capability and recording pin.

The shared forward shader covers both forward opaque and translucency. The
current volumetric-fog path reads directional shadows; its local-map use set is
empty. Directional/contact lifetimes are not migrated to shared physical slots.

## 10. Capacity, CPU work, and accounting

Use the existing lighting `AllocationBudget` and native `AllocationReservation`.
Charge each physical backing once, including spare layers. Retain charges through
pending GPU use and diagnostic native references. Destroying an alias or removing
a registry entry is not a memory saving while the native allocation survives.

Account for descriptor exhaustion as well as bytes. The current DSV heap is
bounded; allocating a new map must not leave a half-registered chunk when SRV/DSV
creation fails. Creation/alias installation is transactional; acquired but
unpublished slots are rolled back through the no-use path.

Bound engine-owned CPU caches by live aliases, live versions, and the current
prepared snapshots. Shared-key lookup entries are weak. Erase expired entries on
retirement and frame maintenance. Do not keep a history keyed by frame sequence.
Retained external content leases keep their versions explicitly alive; their
copy-on-write allocations remain subject to the native budget.

Keep canonical caster records shared across equal prepared inputs. Per-light
membership lists contain record identities, not copies of full caster records.
Deduplicate registration pins per chunk and version pins per recording. Reuse
scratch capacities after warm-up. Keep registry/queue locks out of draw loops.

Required counters are unique native bytes, spare bytes, pending-retirement bytes,
alias count, live version count, cache hits/misses, copy-on-write reason, writer
recordings, reader batches, completion signals, and dependency waits. Report CPU
allocation counts and retained canonical-record bytes separately from GPU bytes.

Unique native bytes count each backing's actual allocation charge once, including
backings retained only by diagnostics. Pending-retirement bytes are the subset
belonging to closing chunks; do not add that subset to the total again. Spare
bytes measure unused texel capacity in live chunks and exclude layers still held
by retiring slots. Report allocator committed/slack bytes and process device-memory
usage separately from these resource charges.

For two compatible views with L dirty local lights, retaining current writer
granularity gives L writers instead of 2L. With warm content it gives zero shadow
writer recordings instead of 2L cache-hit recordings. Reader receipts can add two
signals when the view lists have no existing receipt. Measure this tradeoff; do
not label every cache hit a signal reduction or frame-rate improvement.

## 11. Source integration anchors

- [Registry](../../../src/Oxygen/Graphics/Common/ResourceRegistry.h): managed mode,
  identity checks, mutation guards, and complete view-description equality.
- [Recording](../../../src/Oxygen/Graphics/Common/CommandRecording.cpp): one
  terminal result and preallocated submitted/quarantine retention.
- [D3D12 queue](../../../src/Oxygen/Graphics/Direct3D12/CommandQueue.cpp): ordered
  issue, private receipt marker, dependency waits, and failure classification.
- [Loader](../../../src/Oxygen/Loader/GraphicsBackendLoader.cpp): canonical owner,
  close/reload exclusion, and module release after final destruction.
- [Native backing](../../../src/Oxygen/Graphics/Direct3D12/GraphicResource.h):
  native allocator lifetime and existing budget reservation.
- [Allocator](../../../src/Oxygen/Vortex/Shadows/Internal/ConventionalShadowTargetAllocator.cpp):
  shared physical slots/chunks and view aliases.
- [Depth pass](../../../src/Oxygen/Vortex/Shadows/Passes/ShadowDepthPass.cpp):
  per-light versions, write pins, receipt publication, and zero-work hits.
- [Dependencies](../../../src/Oxygen/Vortex/Shadows/Internal/ShadowCasterDependencies.cpp):
  canonical records and conservative per-light membership.
- [Service](../../../src/Oxygen/Vortex/Shadows/ShadowService.cpp): preparation,
  frame read sets, content leases, and per-view reference publication.
- [View execution](../../../src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp):
  family preparation and one reader batch per view recording.

## 12. Acceptance

| ID  | Required result                                                                                                                                                                                                                                          |
| --- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| G01 | Managed mutation/re-registration/stale callback rejection; exact view equality under forced hash collision; allocation-free descriptor rollback/final release under allocation failure                                                                   |
| G02 | CPU owner released before submit; discard; delayed/out-of-frame submit; frame-slot rollover; list/allocator retained until actual completion; explicit polling releases completed work without another frame                                             |
| G03 | Two queues with different completion progress; typed dependency on the producer fence; no same-queue self-wait                                                                                                                                           |
| G04 | Reverse reservation/submission order rejected before issue; failures after issue quarantine ownership and invalidate backing state; mixed managed/manual issued resources regain trustworthy state after drain or force backend closure                  |
| G05 | Close without nursery activation; live external/diagnostic/recording references; reload rejected until release; loader facade replacement obeys the same gate; retained weak observers do not block reload; no module or allocator use after destruction |
| V01 | Compatible, reordered, partially overlapping view lists share per light and publish correct view-local indices                                                                                                                                           |
| V02 | LOD, resolution, scene, geometry, transform, masked-material/texture, bias, and depth-contract changes invalidate exactly the affected variants                                                                                                          |
| V03 | Off-screen contributing lights/casters, entering casters, unknown bounds, and empty selections preserve required contributions                                                                                                                           |
| V04 | Retained content lease forces copy-on-write; ordered unretained updates reuse storage; cross-queue hazards cover the entire backing                                                                                                                      |
| V05 | First/last view removal, failed writer, capacity exhaustion/recovery, and diagnostics preserve surviving owners and accurate native charges                                                                                                              |
| V06 | Deferred, forward opaque, translucency, and native captures all attach managed uses; expired frame publications are rejected                                                                                                                             |
| P01 | Warm compatible case has zero writer recordings and zero shadow self-waits; allocation/signal/memory counts match actual work                                                                                                                            |
| P02 | Compatible, incompatible, dynamic, and small-light controls report matched CPU/GPU/whole-frame results and bounded retained memory                                                                                                                       |

These gates use the existing Graphics, Nexus, Vortex and native lighting test
suites. No new benchmark runner, shadow allocator, resource graph, or GPU handle
validation framework is part of this design.
