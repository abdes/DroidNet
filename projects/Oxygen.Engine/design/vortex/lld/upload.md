# Renderer upload

Upload owns CPU-to-GPU data preparation and transfer. Existing planning and
coordinator APIs remain the foundation; immutable texture initialization adds
a bounded path for renderer-owned lookup products.

[VX-IBL-01.S7](../milestones/VX-IBL-01/README.md#s7--reusable-infrastructure)
owns implementation state, adopter migration and integration qualification.

Read: [immutable initialization](#immutable-texture-initialization),
[async ownership](#async-request-ownership), [result ownership](#result-ownership),
[consumer maintenance](#consumer-maintenance), [frame retirement](#frame-retirement),
[C++ guidance](../../../../../design/oxygen/RULES.md#c).

## Immutable texture initialization

**Implementation home:** `UploadCoordinator::PrepareImmutableTexture2D` prepares
the move-only `Vortex/Upload/ImmutableTextureUpload` recording object. Shared row
packing belongs to `TextureUploadPlan::Pack2D`. Its production users are
[`IblBrdfResources::Prepare`](../../../src/Oxygen/Vortex/Environment/Internal/IblBrdfResources.cpp)
and [`BrdfEnergyResources::Prepare`](../../../src/Oxygen/Vortex/Lighting/Internal/BrdfEnergyResources.cpp).
Both LUT generators, formats, dimensions and service publication fields are unchanged.

- Accept a newly created managed 2D texture, explicit source subresources/row
  pitches and allocation-budget/debug context. Preparation returns a move-only
  upload object owning the packed staging and destination registrations;
  `Record(CommandRecorder&)` records it. The caller obtains the producer
  `CompletionReceipt` from `SubmitWithReceipt`. S7 supports the
  two existing 2D LUT consumers; 3D/cube upload remains with `UploadCoordinator`.
  Initialize full selected subresources using the canonical destination
  footprint. Boxed updates are rejected; source rows may have their own pitch.
- `TextureUploadPlan::Pack2D` replaces the local `PackTexture2DToStaging` in
  [`UploadCoordinator.cpp`](../../../src/Oxygen/Vortex/Upload/UploadCoordinator.cpp)
  and uses `UploadPlanner::PlanTexture2D` layouts. The coordinator calls the same packer;
  keep its existing tickets, queues, cancellation and 3D behavior unchanged.
- Separate preparation/recording from submission: the caller owns the explicit
  graphics-queue recording, diagnostic scope and timing-failure handling. The
  helper retains managed source/destination registrations in that recording,
  records copies and final SRV transitions, and keeps CPU source bytes only
  through packing. Final transitions remain inside the measured upload scope.
- The initialization owner publishes a candidate only for `kSubmitted` with a
  valid receipt. Discard/rejection leaves it unpublished and retryable; uncertain
  submission follows existing Graphics fault handling. Consumers attach the
  registration and record the producer dependency without a CPU wait.
- `ForwardLightPublisher` retains the BRDF-energy product beside its existing
  `LightingFrameBindings`. Deferred, base and translucent recordings attach its
  registration and producer receipt. CPU ownership does not enlarge the shader
  ABI. Lighting's allocation budget and failure classification remain intact.
  Complete products are cached; stable frames allocate no upload staging and
  issue no LUT upload. There is no new global cache.

**Checks:** pitched/short/overflowing source rows, padding, setup/view/allocation
failure, discard, rejected/uncertain submission, retry and backend close. Read
back both real LUTs exactly and execute both lighting consumers after releasing
initialization CPU temporaries. Exercise existing 2D/3D/coordinator regressions.

## Async request ownership

`SubmitAsync` owns its request and staging-provider handle; `SubmitManyAsync`
owns the request vector and provider. Both remain lazy: packing and submission
start when the coroutine executes. Source byte views remain borrowed through
packing; an owning producer capture can carry source storage with the request.
Producers are const-callable and receive a writable staging span.
Each copy recording retains its staging backing and destination through Graphics
completion, independently of coroutine/provider lifetime. Cancellation remains
prompt; partial batch failure cannot release resources still used by the GPU.
Ticket results are published before waking coroutine consumers outside the
tracker lock.

`Shutdown` drains ticketed submissions. Prepared immutable uploads belong to
their caller's recording and retire through Graphics submission receipts.

## Result ownership

`UploadTicket` owns a retained CPU result record and exposes identity, fence,
nonblocking result lookup, cancellation and waiting. Copies share that record;
there is no frame expiry or manual release call. Dropping the last copy releases
only CPU metadata. The record owns no Graphics object, destination or staging
allocation: existing recording holds and Graphics/Nexus retirement protect GPU
resources independently.

One `make_shared` allocation creates the record, replacing the tracker's former
map-node allocation. Copying, polling, cancellation and frame progress allocate
nothing. All records refer to one shared coordinator timeline; success is derived
from its monotonic completed fence, without a registry or per-frame ticket scan.
Numeric ticket IDs are diagnostic identifiers, never authority to query another
coordinator. Tracker and coordinator ownership is non-movable. `UploadTicket`
declarations remain C++20-compatible.

Terminal precedence is explicit failure/cancellation, observed GPU success, then
tracker shutdown for unfinished work. Closing the timeline never advances the
GPU fence. Completed results remain readable after the coordinator is destroyed;
unfinished results report shutdown. Cancellation does not undo submitted copies
or shorten GPU-resource retention.

Record allocation occurs after the submitted-fence highwater has advanced, so
allocation failure cannot hide issued work from shutdown. Highwater updates use
max, including out-of-order registrations. Remove whole-tracker pending scans and
frame-slot cleanup; shutdown waits on physical submission highwater.

Blocking result waits may return cancellation. Asynchronous GPU-completion waits
retain tickets before lazy execution and wait for the real fence or timeline
close; logical cancellation alone does not satisfy a physical wait. Batch
coroutines own their ticket vector. Progress, close and asynchronous waits run on
the upload-owner thread; notifications resume coroutines inline and retain the
timeline across reentrant owner destruction. Unchanged progress sends no
notification. Polling, cancellation and blocking result waits are thread-safe.
Resumed coroutine work is not represented as constant-time polling.

## Consumer maintenance

Result retention removes the need to move binder `OnFrameStart` routines into
the global renderer tick. Scene collection, new uploads, descriptor publication,
atlas reset and bulk eviction stay with their owning resource services.

Geometry and texture completion publication use pending-only round-robin work
queues. A per-frame visit budget bounds checks as well as successful publications;
unready work yields its turn. Every work item verifies its handle/content revision
and ticket identity before applying a result. Delayed consumers retain results,
so budget pressure cannot turn a valid upload into an expired-ticket retry.
Ticket snapshots are built on demand from pending work, not every resident entry.

Geometry eviction replaces the composite asset/LOD identity map with one
asset-indexed collection of LOD handles; it does not add a second reverse registry.
Logical invalidation occurs when the render-thread owner accepts and detaches a
queued eviction, before reclamation. Detached old asset generations cannot
admit new reads or publish late completions, and their cleanup cannot erase a
reload. Reclamation budgets count resident LOD entries, not merely asset events.
Existing Nexus handle generations and Graphics deferred release remain the
physical lifetime authorities. Texture accepted-revision leases retain their
existing pinning semantics; no new residency contract is introduced for symmetry.

Measure large-unload CPU tails with Tracy and bound owner cleanup admission;
enqueueing an entire unload into one Graphics retirement bucket only postpones
a spike. An operation budget does not promise a hard wall-time ceiling for an
individual driver release. No queue flush, blocking upload wait or larger expiry
window is an acceptable substitute for ownership.

**Checks:** delayed polling across arbitrary frame cycles; ticket copies/moves and
coordinator close; independent timelines with equal numeric IDs; cancellation
versus physical completion; allocation failure after submission; highwater after
all tickets are dropped; pending-only budget fairness; paused views; stale
completion after reload; large multi-LOD eviction and existing texture leases.
Replay the editor's automatic publication and scene replacement workflow.

## Frame retirement

`UploadCoordinator::OnFrameStart` runs after `Graphics::BeginFrame`, which waits
for the recycled slot across every queue. This protects staging partition reuse.
The coordinator polls the upload queue's completed fence; it never flushes newer
work merely to retire uploads. Device-loss fence values close unfinished results
as device-lost; they never publish successful completion.

`Shutdown` polls until the last ticketed submission completes, including work
whose result tickets were dropped or canceled. Its timeout bounds that wait;
ordinary frame retirement does not wait for unrelated queued work.
