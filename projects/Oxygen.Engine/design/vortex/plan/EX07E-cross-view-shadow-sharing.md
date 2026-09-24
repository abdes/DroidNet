# Conventional shadow sharing — implementation plan

## 1. Scope, authority, and execution order

This plan replaces the cross-view shadow-sharing proposal. It delivers the
Nexus retirement contract, the supporting Graphics/backend lifetime changes, and
shared conventional local-shadow storage in Vortex.

The implementation contracts are:

1. [Nexus slot retirement](../../../src/Oxygen/Nexus/Docs/slot-retirement.md).
2. [Conventional local-shadow sharing](../lld/conventional-shadow-sharing.md).

The first LLD owns slot identity, ticket finalization, and frame-adapter behavior.
The second owns Graphics/loader integration and the rendering scenario. This plan
owns sequencing, file boundaries, tests, and acceptance evidence. Change the
owning LLD before implementing a change to a contract.

**Implementation status: S1–S8 complete; S9 automated qualification complete (2026-09-25). User visual and numeric acceptance are complete (2026-09-25); implementation/evidence commits are listed in the final report.** The user
approved implementation after independent review, including allocation-free
descriptor cleanup and complete resource-state recovery after uncertain issue.
**S1–S8 implementation gates and S9 automated qualification are complete.** Final
non-Tracy Release qualification: **494/494 tests**. Both existing Ninja Release
trees pass **50/50 shadow-service tests and 26/26 native image tests**. Debug
qualifies allocation-failure and lifetime paths, including retained capture across
frame-slot rollovers. Final diagnostics pass 81 tests in each Release tree and 82
in Debug. Two supplemental retained-memory cases pass in all three configurations:
forced copy-on-write measures 128 MiB and returns to 64 MiB after release; the
separate 6 MiB closing native charge also releases exactly. The [durable S9 report](EX07E-shadow-sharing-results.md)
contains 13 native + 9 Tracy final rows and four final scene captures. New baselines
have user visual/numeric approval (2026-09-25) and are committed in `b8f1376e1`.
The [tracker checkpoint](../IMPLEMENTATION_STATUS.md#ex07e--work-items-and-resume-checkpoint)
provides the concise completed/remaining table.
The approved backend rule is one active or
retiring incarnation per process. Backend reload returns BackendRetiring until the
previous incarnation reaches Released.

This remains EX07E06 and is the **last EX07E implementation item**. Complete the
other optimizations and non-sharing correctness repairs, including the approved
E04 cube hardware-PCF producer/consumer contract, before starting any step below.
The Graphics and Nexus work in this plan must not be started under another item.
Final integrated E08 qualification follows this implementation. New baselines
require the user's manual visual validation before commit.

Existing unrelated worktree changes are not part of these steps. The execution
starting point is the final source after the preceding work has completed; its
source/shader identities become the comparison control.

### Execution record — 2026-09-24

The comparison control is committed pre-E06 source `e824c97a7`; existing E04/E05
candidate records remain provisional, not newly accepted baselines. S1/S2 changed
no shaders, rendering policy, or shadow allocation partitioning.

| Owning suite (existing `out/build-ninja`)                | Release     | Debug       |
| -------------------------------------------------------- | ----------- | ----------- |
| Nexus reuse (including core and frame/timeline adapters) | 45/45       | 45/45       |
| Nexus allocation failure                                 | 4/4         | 4/4         |
| Graphics deferred reclaimer                              | 18/18       | 18/18       |
| Transform uploader                                       | 18/18       | 18/18       |
| Draw metadata emitter                                    | 15/15       | 15/15       |
| Geometry uploader                                        | 30/30       | 30/30       |
| Material binder                                          | 34/34       | 34/34       |
| Atlas buffer                                             | 10/10       | 10/10       |
| Texture binder regression control                        | 27/27       | 27/27       |
| **Total**                                                | **201/201** | **201/201** |

The allocation-failure executable intercepts allocations in its instantiated
Nexus templates; it does not intercept allocation inside the Graphics DLL.
Reclaimer mixed-order, reentrant, exception-continuation and uncommitted-action
tests pass. The tests exposed and fixed the MSVC checked-iterator temporary-vector
failure in GenerationTracker. Material atlas return capacity is now secured during
growth, and material/geometry/shadow activation restores unexposed indices on
failure. Existing multi-view publisher and repeated-frame-slot tests pass.

Local JSON/log evidence is under `out/analysis/ex07e/e06-s12/{Release,Debug}`.
This table is the durable S1/S2 result summary. Later-step checkpoint results
follow below; native integrated qualification, Tracy integration, and final
visual baseline acceptance remain open.

S3 ordinary lifetime qualification: **27/27 in Release and 27/27 in Debug**, in
the same existing Ninja tree: loader 17, Common ownership/admission 2, Common
lifecycle 6, and native loader integration 2 (Headless and D3D12 with debug layer).
Native tests retain a manually registered texture/SRV, buffer and unfinished
recording across close without starting a nursery. They verify discard on the
recording thread, descriptor cleanup, canonical-owner retirement, D3D12 budget
retention through native destruction, reload exclusion and eventual reload while
dead weak observers remain. The ownership helper preserves texture
`shared_from_this()`. Nested factory admission is tested against concurrent close.
Evidence: `out/analysis/ex07e/e06-s3/{Release,Debug}`. S5's remaining lifecycle
failure cases and final integrated qualification are not covered by these counts.

S4 qualification so far (existing non-Tracy Ninja tree):

| Owning suite                                   | Release | Debug |
| ---------------------------------------------- | ------- | ----- |
| Resource registry, including managed ownership | 73/73   | 76/76 |
| Descriptor allocator                           | 26/26   | 28/28 |
| Descriptor segments                            | 27/27   | 27/27 |
| Queue ownership/state retirement               | 19/19   | 20/20 |
| Native lifetime integration                    | 4/4     | 4/4   |

Managed coverage includes independent allocation owners/use pins, immediate
acquisition closure, monotonic/exhausted IDs, stale identity rejection, all raw
mutation routes (including descriptor source and destination), native backing
aliases, forced view-hash collisions, concurrent equal-view acquisition,
transactional view rollback and late cleanup without a facade/frame. Native
Headless/D3D12 cases create a cube SRV and six immutable DSVs and retain them
through close with only an internal use pin. These are correctness tests, not
timing baselines.

All-allocation denial passes for raw/bindless descriptor return, prepared-action
commit, managed owner/use/view retirement, and queue-state retirement. View
construction rollback also passes fault injection; its fixture uses concrete
allocator access so mock-framework allocations are not confused with product work.

**Accepted S4 qualification limit (user approval, 2026-09-24):** exhaustive Debug registration-construction fault
injection reaches MSVC's `_Hash_vec` noexcept constructor allocating a 16-byte
checked-iterator proxy. The debugger confirms termination inside the STL before
Oxygen can handle `bad_alloc`. The user accepted exclusion of allocations the size
of `std::_Container_proxy` in that construction test. It also excludes unrelated
allocations of the same size; this is a test-coverage limit, not a product policy
change. The exclusion is opt-in and used only by registration construction.
View rollback and all retirement tests continue rejecting every allocation.
The complete Debug registry suite now passes 76/76. Do not count the earlier
aborted run as a pass. Current proof is
`out/analysis/ex07e/e06-s4/Debug/Oxygen.Graphics.Common.ResourceRegistry.Tests.json`;
the debugger evidence remains `out/analysis/ex07e/e06-s4/managed-oom-debugger.log`.

### S5 closure — actual submission and retirement

**Complete: 93/93 Debug, 90/90 Release**, both using `out/build-ninja`.

| Owning suite                            | Release   | Debug     |
| --------------------------------------- | --------- | --------- |
| Prepared submission/use-batch contracts | 4/4       | 5/5       |
| Queue strategy and frame retirement     | 19/19     | 20/20     |
| Graphics lifecycle                      | 6/6       | 6/6       |
| Recording/publication callbacks         | 20/20     | 20/20     |
| Command-list pool and late returns      | 15/15     | 16/16     |
| Headless/D3D12 native integration       | 26/26     | 26/26     |
| **Total**                               | **90/90** | **93/93** |

Both production backends now use Common's prepared submission transaction.
Managed uses retire from private completion receipts. Ordinary frame-scoped
submissions retain frame reclamation. Cross-queue dependencies reference the
producer timeline; same-queue dependencies add no native self-wait. Resolved
recordings release their recorder/backend owner and preserve only their result.
Queue enumeration retains an immutable snapshot so completion polling allocates
no temporary queue collections.

Native qualification covers stale reserved signals, impossible same-list waits,
pre-issue discard, partial array issue, post-issue/pre-marker failure, recovery
of mixed manual/managed states, out-of-frame retirement, late submission after a
frame marker, two consuming queues, actual cross-queue copied buffer contents,
device loss, queue replacement guards, close/reload and worker-thread late release.
The Debug submission test rejects every allocation after native issue and during
completion retirement; no proxy-size exemption is used for that test.

Recovery drains all queues, reconciles prepared states, and checks cross-queue
agreement before finalizing quarantined ownership or clearing the fault. Conflicting
queue state records or failed drain/reconciliation close the backend. A device-loss
sentinel never counts as successful completion. Unresolved pre-fault recordings
are rejected through their preparation epoch. Existing publication callbacks
invalidate products for both discard and uncertain execution.

The regression suite exposed a missing null-result check during queue creation;
that defect is fixed and the full queue suite passes. Evidence logs/JSON:
`out/analysis/ex07e/e06-s5/<suite>-{Debug,Release}.{json,log}`. The counts and
behavior above are the durable summary. S6–S9 remain; no E06 timing baseline or
shared-shadow acceptance is claimed by this gate.

### Native-integration repairs discovered after the S5 core gate

- Legacy readback producers allocate their own increasing signal values. Common
  submission now accepts these without requiring a prior `Signal()` reservation;
  stale/non-monotonic values remain rejected before issue.
- Headless reserves accepted legacy signal values on the CPU before enqueuing its
  async task, preventing a following frame/drain marker from duplicating a value.
- Readback tickets require submission acceptance before fence completion can
  publish data. Unissued shutdown work is cancelled, and a device-loss sentinel
  reports failure. Staging/resolve allocations use managed owners and actual-use
  pins; pending Reset no longer depends on indefinite manual-registration retention.
- Readback facades and mapping guards use Common-owned deleters and retain their
  canonical backend until backend cleanup returns.

These are E07 correctness/lifetime repairs within S3/S5 and the S8 native-capture
path. The original S5 table remains its completed checkpoint, not a claim that
these later changes have finished validation. Added native, tracker and readback
regression suites are being run before integrated closure.

### S6 checkpoint — canonical local-shadow requests

**38/38 tests pass in Debug and Release; production integration remains open** in the existing non-Tracy Ninja
tree (`Oxygen.Vortex.ShadowService.Tests`). Exact immutable caster records retain
geometry/LOD/generation/revision, bindings/ranges, world transform, raster and
masked coverage inputs. Interning compares values after hash lookup. Unknown
sampled-texture continuity disables reuse; texture-free masked materials need no
texture revision.

`ShadowService::PrepareLocalRequests` prepares the family before placement.
Caster/request caches honor frame and CPU preparation revisions, including a
same-frame offscreen snapshot rebuild. Point/spot math is separated from physical
binding; existing projection and per-view reference tests pass. Content identity
includes the explicit depth contract and relevant caster set, while excluding
view/frame/placement identity and cube receiver-only bias/filter settings.

Seven added request tests cover collisions, unchanged-record interning, sampled
texture continuity, reordered/partial membership, entering/unknown-bound casters,
producer versus consumer bias, and geometry/LOD/raster invalidation. The existing
cache regression now uses an explicitly sampled texture with a known revision,
and also checks a same-frame preparation rebuild. S7/S8 will replace the old
physical-map cache and attach all readers; S6 alone does not enable shared maps.

The S7/S8 call-site audit found that SceneRenderer still supplies only the current
view, so the earlier S6 completion label was premature. The CPU/service gate
stands; production must pass the full preparation family while rendering only
the current view. This integration and its regression check are being completed
with S7/S8 before S6 is marked complete again.

Durable result: **38/38 per configuration**. Supporting JSON/logs are
`out/analysis/ex07e/e06-s5/shadow-preparation-{Debug,Release}.{json,log}`.

### S6–S8 integrated closure

The previously reopened SceneRenderer family-input gap is fixed: the full CPU
preparation family is separate from the current view selected for rendering.
The old per-surface local content cache and its reuse-only submissions/self-waits
are removed. Physical maps and immutable versions are shared per compatible light.
Unsubmitted backing users prevent writes to any of its layers; submitted users on
other queues contribute actual completion dependencies.

Frame publications close at EndFrame, snapshot replacement and service shutdown.
Already attached frame-ring recordings are rejected if submitted after closure.
Explicit content leases support delayed recordings with independently owned inputs.
Native tests preserve old depth across five frame advances while a new caster
version renders elsewhere. A 32 MiB budget forces exact six-layer cube fallback;
a retained diagnostic texture keeps **6,291,456 native bytes** charged until release.

| Final owning qualification                    | Result                      |
| --------------------------------------------- | --------------------------- |
| Non-Tracy Release integrated selection        | 494/494                     |
| Shadow service, non-Tracy / Tracy Release     | 50/50 in each tree          |
| Native image suite, non-Tracy / Tracy Release | 26/26 in each tree          |
| Debug shadow service                          | 50/50                       |
| Debug native shadow/capture/budget selection  | 10/10                       |
| Native backend lifetime, Debug / Release      | 36/36 in each configuration |
| Readback tracker, Debug                       | 17/17                       |
| Headless readback manager, Debug              | 27/27                       |
| D3D12 buffer / texture readback, Debug        | 29/29 and 25/25             |

The same-frame native family renders five cube and nine projected maps for two
views, including reversed publication order, deferred/forward opaque and
translucency. Resident views share the same placements; warm frames produce no
shadow writers. Different exposure settings are compared in scene-radiance space.
The 14 map-use pins deduplicate to two backing-registration pins per view.

Readback integration is also qualified: independently allocated monotonic legacy
signals work; unsubmitted tickets cannot complete from another queue marker;
shutdown cancels unissued copies; staging/resolve owners and mapping guards retire
safely from actual use. Fourteen later lifecycle cases expanded the original
native set to 36, including canonical facade/mapping lifetime across close.

Proof: `out/analysis/ex07e/e06-final/tests-Release.json`, the two owning test JSONs
per Release tree in that directory, and the named Debug JSON/logs under
`out/analysis/ex07e/e06-s5`. Counts and essential outcomes above remain durable if
those transient logs are removed. S9 now has the separate timing/ownership report
linked above; automated test success is not manual visual acceptance.

## 2. Fixed design decisions

| Decision                  | Implementation rule                                                                                                                                      |
| ------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------- |
| GPU lifetime owner        | Graphics manages registration, descriptors, submission outcomes, and actual completion                                                                   |
| Reusable-slot owner       | Nexus invalidates handles and issues/finalizes retirement tickets                                                                                        |
| Shadow owner              | Vortex owns canonical content, versions, aliases, and placement in the existing allocator                                                                |
| Dependency direction      | Vortex connects completion to Nexus; Graphics Common never depends on Nexus                                                                              |
| Backend lifecycle         | One Active/Closing/Retiring incarnation; replacement creation starts only after Released                                                                 |
| Rendering lifetime        | Existing Vortex writers and view recordings remain scoped to their rendering calls                                                                       |
| Retained shadow contents  | Retain a content lease across frames; publish fresh frame bindings for later view use                                                                    |
| Delayed managed recording | All its referenced inputs must have retained lifetimes; frame-ring publications cannot be reused after closure                                           |
| Completion markers        | Private queue-owned values assigned in submission order; one marker per managed submission; shadow producers use that receipt instead of a legacy signal |
| Physical storage          | Extend ConventionalShadowTargetAllocator; retain bounded chunks, D32, quality history, and allocation budget                                             |
| View compatibility        | Compare canonical per-light producer inputs; preserve view-local references and consumer settings                                                        |
| Hazards                   | Whole-backing ordering; copy-on-write when an old reader can still attach or submit                                                                      |
| Heap behavior             | Keep current fixed shader-visible heaps; fail exhaustion/replacement instead of adding heap migration                                                    |
| Failure                   | Preserve published owners; explicit preparation failure; uncertain issue retains ownership and faults submission until recovery/close                    |

## 3. Delivery boundaries

The only new general Nexus type is the lifecycle core and its move-only ticket.
The existing frame adapters use that core. Dense draw/transform publishers stop
pretending to allocate reusable slots; their emitted data and handle semantics
stay the same. TextureBinder and TimelineGatedSlotReuse are regression controls,
not migration targets.

The only DeferredReclaimer extension is preparation of an action node followed by
allocation-free commit to its existing frame buckets. Existing callers keep
RegisterDeferredAction and its vector storage. Both action forms retain enqueue
order and use the same frame-completion event. No second reclamation service is
introduced.

Graphics gains managed registrations and managed use retention in its existing
registry/recording/queue pipeline. Manual registrations retain their explicit
ownership. One registration cannot mix both modes. This is not a general
conversion of every renderer resource to leases.

Vortex uses the existing shadow allocator, pass, dependency builder, frame
publishers, diagnostics, and native lighting fixtures. Family preparation is CPU
work inside ShadowService. There is no new render graph, shadow allocator,
subresource barrier framework, retained-frame renderer, or benchmark runner.
Virtual-shadow-map migration is outside this implementation.

## 4. Step S1 — Extract the Nexus lifecycle core

**Dependencies:** execution gate in section 1.

S1 and S2 form one production compatibility change. Test the extracted core first,
then switch the adapters and dense callers together; do not publish strict
activation while those callers still depend on repeated live-index stamping.

**Files:** Nexus/IndexReuse.h; FrameDrivenIndexReuse.h; GenerationTracker.h
(test-access friendship and exception-safe temporary storage construction);
Nexus/CMakeLists.txt; Nexus/Test.

Implement the Nexus LLD's Reusable/Live/Retiring/Unavailable phases, one transition
mutex, generation validation, retirement tickets, close, exhaustion handling, and
telemetry. Reuse GenerationTracker storage. Keep its standalone increment/load
semantics unchanged.

Make retirement/finalization/ticket destruction allocation-free. Complete all
state and counter writes before returning a reusable index. A dropped unresolved
ticket makes its index unavailable and increments abandoned_retirements.

**Gate:** N01-N06 and N10 pass. Use controlled thread barriers for the stale-release
and resize interleavings. Initialize test generations near the maximum rather
than iterating billions of releases. This step does not change a production
caller's activation behavior yet.

## 5. Step S2 — Make the frame adapter safe and migrate its callers

**Dependencies:** S1.

**Files:** Graphics/Common/Detail/DeferredReclaimer.h/.cpp and tests;
Nexus/FrameDrivenIndexReuse.h; Nexus/FrameDrivenSlotReuse.cpp and tests;
Vortex/Resources/TransformUploader, DrawMetadataEmitter, GeometryUploader,
MaterialBinder; ConventionalShadowTargetAllocator's callback storage.
Material rollback also requires AtlasBuffer return storage to be reserved before
capacity publication; include its existing allocation/release tests in S2.

1. Add PrepareDeferredAction and allocation-free CommitDeferredAction to the
   existing reclaimer. Keep the existing frame buckets and callback thread.
   Preserve ordinary action vectors, add a prepared-node list to each bucket,
   and merge both in enqueue order at drain. Ordinary actions gain no per-action
   node allocation.
2. Prepare the action/payload before each frame-adapter activation. Release only
   claims that payload, retires the core handle, fills nothrow-movable context,
   and commits the prepared action.
3. Use shared recycle-target state. Remove both inner and wrapper captures of
   adapter/owner this. Finish Nexus finalization before invoking the target.
4. Reserve free-list return capacity when growing physical entries. Roll back an
   unexposed index if preparation or activation fails. Make completion callbacks
   nonthrowing; one callback must not prevent execution of later detached actions.
5. Replace DrawMetadataEmitter's fake allocation with its dense counter. Replace
   TransformUploader's fake allocation with its dense counter and existing
   generation stamping. Preserve bounds/generation validity checks.
6. Retain geometry/material frame-start eviction and destructor ordering. Move
   the shadow allocator's callback storage into stable pool state in preparation
   for shared physical slots. Do not change shadow allocation partitioning yet.

**Gate:** N01-N12 pass, including allocation-failure injection with several pending
slots, close with queued actions, and reentrant recycle. Existing geometry/material
and texture upload/eviction tests pass. Dense publisher output tests cover two
views within a frame and repeated physical frame slots. No new draw-loop locks,
per-record retirement actions, or queue waits are introduced.

## 6. Step S3 — Establish backend incarnation and native lifetime

**Dependencies:** S2.

**Files:** Loader/GraphicsBackendLoader and loader tests;
Graphics/Common/Graphics, CommandRecording, Internal/Commander, and the Common
backend-object ownership helper; existing GraphicsModuleApi factory integration;
Graphics/Direct3D12/Graphics, Devices/DeviceManager, GraphicResource;
Graphics/Headless/Graphics; native lifetime test fixtures.

1. Introduce one process-wide incarnation gate and a canonical BackendOwner.
   Return public Graphics pointers as aliases of that owner. Implement
   Active/Closing/Retiring/Released. Load while active returns the same owner;
   load during closing/retirement fails before calling CreateBackend.
2. Change UnloadBackend to request close and release the active loader owner.
   Its retirement record observes remaining ownership without strongly retaining
   Graphics. Remove unconditional CloseModule after resetting one shared pointer.
3. Keep device/allocator/module lifetime through native allocation destruction.
   Every owned GraphicResource retains native lifetime, including manual
   registrations. Retain its allocation-budget reservation through native
   destruction. Escaping Graphics/buffer/texture objects use the Common ownership
   helper; its outer deleter returns from backend cleanup before releasing the DLL.
   Clear its stored strong lifetime capture at deletion so surviving weak
   observers do not prolong retirement.
4. Implement the lifecycle gate and close ordering from the scenario LLD.
   Acquisitions/submissions stop independently of IsRunning. Existing recording
   threads resolve their own recording objects; close does not race native End.
5. Prevent a second ownership block from adopting the singleton during restart.
   Keep the existing singleton factory/API; do not introduce concurrent backends.
6. Retain the canonical owner token in acquired recordings. Until S5 adds early
   terminal release, destroy recorder/list references before that token. Install
   weak canonical-owner access in Graphics without changing factory signatures.

**Gate:** The G05 lifecycle cases with ordinary successful submissions, live
Graphics owners, diagnostic resources, and recordings;
no-nursery close; repeated close; reload exclusion; final release permits exactly
one new incarnation; loader facade destruction/recreation; expired weak observers
retained through reload. Verify budget retention through the last diagnostic native
reference. Existing loader tests continue to pass when no external owner exists.
S5 completes G05's uncertain-submission and device-loss cases.

## 7. Step S4 — Add managed registration and immutable views

**Dependencies:** S3.

**Files:** Graphics/Common/ResourceRegistry.h/.cpp; descriptor/native lifetime
integration and registry tests; backend resource tests.
Managed ownership is implemented in Registration.h/.cpp and
ResourceRegistryManaged.cpp. The Graphics/QueueManager resource-state forget path
must be allocation-free during final retirement.

Implement registration IDs, RegistrationLease, internal registration cores,
managed/manual mode checks, append-only view acquisition, and final unregistration.
An external lease retains the backend owner; an internal core does not.

Guard every mutation route, including UpdateView's source descriptor owner and
its destination. Validate identity across native view creation and publication.
Extend the existing view cache to compare complete descriptions after hash lookup.
No parallel managed view cache is created.

Treat native resource/registration/view construction as one transaction. On
failure, release descriptors, registration, backing, and native budget reservation
without replacing an existing published owner. Keep shader-visible heaps fixed.
Reserve raw descriptor-segment and bindless free-list return capacity before
publication. Descriptor rollback and final release must not allocate or throw,
including when heap allocation is failing during noexcept handle destruction.

**Gate:** G01, descriptor exhaustion/rollback, concurrent equal-view acquisition,
forced hash collision, unregister/re-register of the same object, stale finalizer,
managed/manual conflict, and last-owner release. Diagnostic native references keep
bytes alive without authorizing another GPU acquisition. Inject allocation failure
during descriptor rollback and final release, for both raw and bindless returns.

## 8. Step S5 — Add actual-use submission and retirement

**Dependencies:** S4.

**Files:** Graphics/Common/Graphics, CommandRecording, CommandRecorder, CommandList,
CommandQueue, Internal/CommandListPool, Internal/QueueManager; D3D12 and Headless
queues/executor; ReadbackTracker and both backend readback managers; existing
submission callback consumers and tests.

1. Add one deduplicating RecordingUseBatch and prepare its in-flight node before
   native issue. Retain command lists and native allocators with the same proof.
   Store batch buffers/node with the pooled list and reuse their capacities.
2. Add SubmitWithReceipt and the stored typed result. Preserve Submit's boolean
   API through the same finalization function. Add kExecutionUncertain to callback
   handling and audit all OnSubmission consumers.
3. Add private queue completion timelines, stable queue identities, and typed
   RecordDependency. Serialize issue/markers/frame signals per queue. No lock is
   held while a pass records draws.
4. Validate legacy queued actions before native execution. Prepare final-state
   adoption before issue. Terminal bookkeeping after issue must not allocate or
   throw; publication callbacks run afterward outside locks.
5. Retain uncertain ownership in the prepared quarantine node. Faulted backends
   stop new issue and frame retirement; a recovery drain closes affected chunks
   or selects device-loss teardown. Before clearing the fault, reconcile states
   for every affected issued resource, including manual registrations, using
   prepared state records and actual issue order. If trustworthy reconciliation
   is impossible, close the backend even if the drain succeeded.
6. Release the resolved recorder/backend token after transferring internal pins.
   Reserve command-pool return capacity when creating lists so pool return does
   not allocate. Move the existing pool's return state behind a shared control
   object; close clears its factory and idle lists, and late returns destroy
   checked-out lists. Clear deleter captures at last strong release. A resolved
   CommandRecording shell retains only its result.
7. Preserve existing frame reclamation for established frame-scoped work. Managed
   batches retire from their actual receipts, independent of the current frame
   bucket.
8. Add owner-thread PollCompletedUses at the specified frame/flush entry points
   and for explicit out-of-frame collection. Use preallocated ready-finalization
   nodes for worker releases. After close/drain and target closure, late CPU-only
   cleanup completes without a renderer thread or another frame.

**Gate:** G02-G05 on both backends. Required controlled sequences:

| Sequence                                                                                   | Expected result                                                                                                           |
| ------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------- |
| Record A with legacy signal 1, submit B with signal 2, then submit A                       | Reject A before native execution; no successful publication                                                               |
| Inject failure after native execution and before its private marker                        | ExecutionUncertain; list/allocator/pins quarantined; no reuse                                                             |
| Issue mixed managed/manual resources, fail the marker, then drain                          | All affected resource states reconciled before resuming, or backend closed; successful drain alone cannot clear the fault |
| Submit after a frame-end signal and roll the frame slot                                    | Frame completion does not retire that managed submission                                                                  |
| Complete one of two consuming queues                                                       | Ownership remains until the other completes                                                                               |
| Submit outside the frame loop, complete the queue, then poll                               | Eligible managed ownership retires without frame rollover                                                                 |
| Wait for a receipt from another queue                                                      | Native wait uses the producer fence, not the consumer's fence                                                             |
| Report device loss as native UINT64_MAX                                                    | DeviceLost; no successful reuse publication                                                                               |
| Discard or fail End before issue                                                           | Release unsubmitted pins; no receipt or content publication                                                               |
| End throws during explicit discard or recording destruction                                | No exception escapes; unusable list/allocator is destroyed rather than pooled                                             |
| Retain a resolved recording shell beyond close                                             | No callback/deleter into a destroyed command pool                                                                         |
| Retain a copied command-list reference beyond pool close, then retain only a weak observer | Strong reference delays native destruction; late return destroys the list; weak observer does not delay reload            |

Existing raw submission tests retain their ordinary successful behavior. The
current QueueFailureDiscardsPublication test is split by whether issue occurred.
Failure callbacks must not manufacture GPU completion.

## 9. Step S6 — Prepare canonical local-shadow requests

**Dependencies:** S5; preceding E04 producer contract fixed by section 1.

**Files:** Vortex/Shadows/Internal/ShadowCasterDependencies,
PointShadowSetup, SpotShadowSetup; Shadows/Types/FrameShadowInputs;
ShadowService; SceneRenderer/SceneRenderer and prepared-view revision publication;
owning CPU shadow tests.

1. Preserve the fields currently used to fingerprint caster content as immutable
   canonical records. Share equal records across prepared views; preserve LOD,
   source generations, revisions, transforms, raster flags, and masked inputs.
2. Build conservative per-light membership from all current caster records.
   Unknown geometry or sampled-texture continuity disables reuse for that variant.
3. Split projection preparation from placement binding. Compute matrices, producer
   identity, quality, and consumer settings without a descriptor/layer.
4. Prepare active-family local requests after existing scene preparation and
   light selection. Cache this CPU preparation by frame/preparation revision;
   invalidate it when a same-frame offscreen preparation rebuilds snapshots.
5. Keep frame/preparation identity out of the semantic content key. Encode the
   depth producer contract explicitly; consumer-only filtering/fade/strength do
   not invalidate stored depth.

**Gate:** V01-V03 at the CPU/key level. Force hash collisions; reorder and partially
intersect view lists; vary view LOD; mutate geometry/material/texture revisions;
exercise unknown bounds and off-screen casters. Projection tests confirm the
established cube layer/orientation/depth contract and per-view reference indices.
No GPU ABI or quality policy changes are made for sharing.

## 10. Step S7 — Share physical slots and map versions

**Dependencies:** S6.

S7 and S8 form one production integration change. Component tests can exercise
S7 first, but local-shadow allocation must not switch to managed ownership in a
delivered change until every reader in S8 is attached. There is no runtime flag
or second allocator used to bridge the two steps.

**Files:** ConventionalShadowTargetAllocator.h/.cpp;
ShadowDepthPass.h/.cpp; CascadeShadowPass.h/.cpp; ShadowService.h/.cpp;
existing shadow service and allocator tests.

1. Remove view identity from physical chunk partitioning. Keep per-view aliases
   and quality history; retain chunk sizing, spare capacity and budget fallback.
2. Use managed registrations for all local-shadow chunks. Use Nexus retirement
   tickets for their physical slots; directional ownership remains as before.
3. Replace the existing local content cache with canonical map versions. A hit
   produces no shadow recording. A miss records one light and publishes only
   after SubmitWithReceipt returns Submitted.
4. Remove the local producer's legacy reserved signal/self-wait path. Retain its
   receipt as the producer dependency and retirement proof.
5. Reconcile current family requests before choosing in-place update. Retained
   content readers or unsubmitted recordings force copy-on-write. Submitted
   cross-queue uses require whole-backing ordering or another backing.
   Treat native readback state transitions as exclusive backing intervals and
   preserve their receipt dependency for later shader readers.
6. Make slot/view/alias installation transactional. Invalid/failed versions do not
   replace an existing published version. Quarantine affects every map sharing
   the backing's resource state.
7. Finalize slots after the last logical/recording/GPU use. Retire a chunk only
   when all its slots have retired. Keep diagnostic/native bytes charged.

**Gate:** V01-V05. Include common dynamic changes in two compatible views without
new native allocations when ordered reuse is possible; incompatible variants;
retained old content; first/last-owner removal; chunk spare-layer exhaustion;
budget-tight exact-layer fallback; interrupted creation; failure of the first
writer followed by another view's successful retry after discard.

## 11. Step S8 — Attach every local-shadow consumer

**Dependencies:** S7.

**Files:** ShadowService publication API; SceneRenderer view recording;
Lighting/Passes/DeferredLightPass; base-pass/translucency attachment validation;
Vortex/Test/Lighting/ShadowAdmissionGpu_test.cpp; native capture/probe helpers;
owning native test fixtures. ToneBoundsProbe.hlsl remains a validation input; the
lifetime migration changes host-side ownership, not its shader interface.

Add ShadowFrameReadSet and ShadowContentLease as specified in the scenario LLD.
Attach one read set before deferred/forward/translucent local-shadow reads in the
view recording. Reuse that batch across stages. Do not attach per draw.

Keep transient GPU frame publications frame-scoped. Later-frame content reuse
publishes fresh bindings. A native delayed recording uses a content lease and
retained capture inputs; inspection pointers alone do not authorize GPU use.

Replace blanket different-view texture inequality assertions with content-aware
sharing/isolation checks. Preserve the existing fifth-cube/ninth-projected-map
capacity case and add true same-frame multiple-view rendering.

**Gate:** V06 and native V01-V05, covering deferred, forward opaque, translucency,
and direct probes/readbacks. Check actual output and reference identity, not just
pointer equality. Test expired frame publication rejection and fully retained
managed captures across multiple frame-slot cycles. Directional/contact outputs
remain distinct where their view inputs differ.

## 12. Step S9 — Qualification and final integration

**Dependencies:** S8.

Use the existing lighting workloads, native image fixtures, allocation counters,
and capture/report tooling. Freeze source/shader identities and numerical/image
criteria before comparing candidate results.

| Control                                              | Required observations                                                                      |
| ---------------------------------------------------- | ------------------------------------------------------------------------------------------ |
| Two compatible views, cold                           | One producer/allocation per unique local content variant; correct view-local mappings      |
| Two compatible views, warm                           | Zero shadow writer recordings and shadow self-waits; stable descriptors/native allocations |
| Partially overlapping/reordered views                | Cost and storage follow the union of required variants; no bucket-order restriction        |
| Incompatible LOD/resolution/content                  | Separate versions; report added lease/lookup/signal overhead                               |
| Moving light/caster and masked revisions             | Affected content updates; ordered reuse when allowed; bounded copy-on-write peaks          |
| Small-light case                                     | Report receipt/lookup overhead without hiding it in a large-scene average                  |
| Removal, retained capture, budget pressure, shutdown | Charges follow actual native lifetime; recovery releases all eligible storage              |

Report CPU allocation counts, preparation/membership time, pin deduplication,
registry/queue lock time, writer/read batches, signals/waits, native live/spare/
pending bytes, retained CPU records, GPU raster cost, and whole-frame results.
A drop in aliases is not a native-memory saving. Warm reuse does not imply raster
savings when both old per-view maps were already cached.

CPU allocation diagnostics use the shared MSVC Debug CRT hook on the rendering
thread, with iterator-support allocations included. They are diagnostic counts,
not Release heap totals or timing baselines. Throughput and application baselines
remain Release-only. Registry/use-pin and queue submission lock-wait/critical-section
zones are enabled only in Tracy builds; native throughput is reported separately.
The existing benchmark gains optional matched/offset/partial secondary layouts
and per-frame writer/map-pin/backing-pin counts; original recipes keep their defaults.

Final ownership instrumentation uses render-thread, untimed snapshots. Weak
observations of backings and versions are pruned at frame maintenance and never
delay retirement. Snapshots expose aliases, live versions, cache decisions and
copy-on-write reasons. Native placement sizes come from the backend; closing
chunks (no allocation owners, possibly still held by GPU work or diagnostic
references) are counted as a subset of unique bytes. Spare texel capacity excludes
retiring slots and closing chunks. The retained-resource native test checks a
6 MiB closing charge and its removal after the final diagnostic reference drops.
Canonical record, immutable version and prepared request payload capacities are
reported separately; these exclude allocator/control-block/container-node overhead
and do not pretend to be total process CPU heap usage. Queue counters measure
successfully accepted batches/lists, private completion signals and cross-queue
dependency waits; frame-end legacy signals and uncertain submissions are not
included in those success counters. Preparation/membership has its own CPU zone.

Run relevant Debug/Release owning suites and native debug-layer checks using the
repository's existing build/run workflow. Compare performance with the matched
post-optimization control from section 1. Reuse unaffected earlier evidence.
Record manual visual approval before accepting/committing a new baseline. Final
integrated E08 qualification follows; do not mark implementation validated from
source changes or pointer-sharing tests alone.

## 13. Contract-to-step coverage

| Required contract                                                       | Owning step | Acceptance IDs |
| ----------------------------------------------------------------------- | ----------- | -------------- |
| Atomic generation/state, reentrancy, exhaustion                         | S1-S2       | N01-N06        |
| Allocation-free retirement, stable callbacks, close                     | S1-S2       | N07-N10        |
| Dense publisher compatibility and existing consumers                    | S2          | N11-N12        |
| Single backend incarnation, native/module lifetime                      | S3, S5      | G05            |
| Registration identity, raw mutation guards, immutable views             | S4          | G01            |
| Recording/list/allocator lifetime, late submit, multi-queue completion  | S5          | G02-G03        |
| Partial execution, uncertain issue, device loss                         | S5          | G04-G05        |
| Canonical per-light content, partial overlap, LOD and revision handling | S6-S7       | V01-V03        |
| Producer/consumer depth separation and physical placement               | S6-S7       | V01-V02        |
| Versions, copy-on-write, backing-wide hazards                           | S7          | V04            |
| Capacity, rollback, actual native charges, owner removal                | S3-S4, S7   | G01, G05, V05  |
| Every surface/native reader and frame publication lifetime              | S8          | V06            |
| CPU/GPU/memory cost and final acceptance                                | S9          | P01-P02        |

## 14. Design integration review

The three-document review resolves the following implementation conflicts:

| Review finding                                                    | Resolution in the LLDs and plan                                                                                                                           |
| ----------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Strict activation conflicts with dense publishers                 | S1/S2 migrate the core, adapters, and two dense callers together                                                                                          |
| Extra per-action allocation in the initial reclaimer draft        | Prepared nodes supplement the existing action vectors within the same buckets and preserve enqueue order                                                  |
| A version retaining a pool that owns versions creates a cycle     | Pool lookups are weak; versions own slots; slots own chunks and a weak return target                                                                      |
| Internal use pins retaining Graphics create a cycle               | Public leases split backend ownership from internal pins; submitted storage receives internal pins only                                                   |
| Buffer ownership does not retain frame-ring contents              | Frame read sets remain frame-scoped; retained content publishes new bindings; delayed native recordings use retained inputs                               |
| Module pinning alone does not protect restart or cleanup code     | One retiring incarnation excludes reload; Common owns outer destruction calls and native lifetime outlasts allocations                                    |
| Weak observers retaining a deleter capture delay backend release  | Outer deleters move strong lifetime captures into their deletion call and leave dead control blocks empty                                                 |
| Checked-out lists outliving the command-pool component            | The existing pool uses shared return state; closing clears its factory and late returns destroy lists                                                     |
| A failed signal skips resource-state adoption                     | Fault recovery closes affected managed registrations and reconciles every affected issued resource, including manual registrations, or closes the backend |
| Descriptor cleanup can allocate during noexcept destruction       | S4 reserves descriptor return capacity before publication and tests rollback/final release under allocation failure                                       |
| Writer migration before reader migration permits early retirement | S7/S8 are one production integration change                                                                                                               |
| Counting submission markers as automatic signal savings           | Keep per-light dirty writers initially and measure reader-marker overhead in cold/warm/incompatible controls                                              |

Section 13 maps the original proposal's ownership, descriptor, submission, Nexus,
content, hazard, budget, shutdown, and efficiency requirements to owning steps and
acceptance IDs. The two LLDs contain no milestone identifiers; this plan carries
the execution order and baseline approval requirements.
