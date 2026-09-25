# EX07E — Scalable optimization

Status: `validated`

| Field     | Summary                                                                         |
| --------- | ------------------------------------------------------------------------------- |
| Outcome   | Measured local-shadow and submission/resource optimizations, including sharing. |
| Remaining | None in the recorded scope.                                                     |
| Evidence  | [Validation record](validation.md)                                              |

[Roadmap](../../../../PLAN.md) · [Design index](../../../../lld/README.md)

## Optimization tasks and outcome

**E06 closed — S1–S9 complete. Manual visual acceptance and numeric review approved
(2026-09-25); implementation and evidence committed in dependency order.** The final non-Tracy Release qualification passes **494/494 tests**.
Both existing Ninja Release trees pass **50/50 shadow-service tests and 26/26
native image tests**. Debug additionally qualifies the failure/retirement paths,
retained native captures and tight-budget fallback. The final diagnostics delta passes 81 tests in each Release tree and 82 in Debug;
two retained-memory cases additionally pass in all three configurations.
Accepted evidence is committed in `b8f1376e1`; the [report](validation.md#commit-sequence) records the implementation sequence.

| Step                                                     | Completed so far                                                                                                                                                                                                                                                                                                | What remains                                                                                  |
| -------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------- |
| **S1/S2 — Safe slot reuse and deferred cleanup**         | Implemented generation-safe slot retirement, allocation-free cleanup, and migrated existing callers. **201/201 tests pass in both Debug and Release.**                                                                                                                                                          | None for S1/S2. Subsequent integrated lifecycle tests pass.                                   |
| **S3 — Backend and native-resource lifetime**            | Implemented ownership that keeps the backend, device and descriptors alive through outstanding references; blocks premature reload. **27/27 ordinary lifetime tests pass in each configuration.**                                                                                                               | None for S3. S5 also closes submission-failure/device-loss integration.                       |
| **S4 — Shared resource ownership and immutable views**   | Implemented managed registrations, independent owner/use lifetimes, mutation guards and transactional descriptor creation. Registry tests: **73/73 Release, 76/76 Debug**; descriptor, queue and native lifetime suites also pass.                                                                              | None for S4. Worker-thread release and final integrated registry/lifetime regressions pass.   |
| **S5 — GPU submission and actual-completion retirement** | **Complete: 93/93 Debug and 90/90 Release owning tests pass.** Both backends use prepared submission, private completion receipts, cross-queue dependencies, quarantine/recovery and fault-aware cleanup. Includes 26 native cases per configuration and Debug allocation denial after issue/during retirement. | None for S5. Shared writer/reader integration and delayed capture tests pass.                 |
| **S6 — Canonical shadow preparation**                    | **Complete.** SceneRenderer supplies the full preparation family while rendering only the current view. CPU identity/invalidation and native same-frame sharing tests pass.                                                                                                                                     | None for S6. Performance reporting belongs to S9.                                             |
| **S7/S8 — Cross-view maps and every consumer**           | **Complete: 50/50 service and 26/26 native image tests in each Release tree.** Includes deferred/forward/translucent readers, typed retained leases, cross-queue ordering, expired frame rejection, five-frame delayed capture, six-layer budget fallback and actual retained-byte accounting.                  | None for S7/S8. S9 measurements and manual visual approval are complete.                      |
| **S9 / E08 — Measure and qualify**                       | **Automated work complete:** 13 native + 9 Tracy final benchmark rows and four final application captures; ownership, CPU/GPU, memory, image and noise results are in the [report](validation.md). Initial captures and the bounded endpoint repeat are retained.                                               | None for E. Visual acceptance and numeric review approved; evidence committed in `b8f1376e1`. |

**Current:** EX07 and EX08–EX10, including EX08.1/EX08.2, are validated and closed. No implementation or acceptance gate remains in this package; see [EX10 closeout](../../EX10/validation.md). The three reviewer documents were committed in `fa94bbae3` after the user clarified that their earlier exclusion was temporary. Detailed suite
results are in the [E06 implementation plan](README.md).

The two approved design amendments are recorded: allocation-free descriptor
cleanup, and trustworthy recovery of **all** affected resource states, including
manual registrations, after uncertain submission. The accepted Debug test limit
excludes MSVC iterator-proxy-sized allocations only during registration-construction
fault injection; view rollback and retirement tests still reject every allocation.

The pre-E06 implementation is committed as `e824c97a7`: both Ninja Release
builds, 23/23 native image tests and 34/34 service tests per tree, and 12 interaction
rows / 20 matching images. E06, memory/lifecycle accounting and final integrated
Sponza/Instancing comparisons are complete; the final E06/S9 evidence is above.
The reviewer documents are committed in `fa94bbae3`.

This is the authoritative E work ledger. The eight items below preserve the scope presented to the user;
they refine the existing EX07 deliverable IDs rather than replace them.
`Open` means the investigation or implementation and its acceptance remain
unfinished. A suspected cause is not a confirmed defect, and a prototype is not
an accepted improvement. Earlier repairs and D baselines remain credited.

| E item                                                   | State                                                               | Required investigation / delivery                                                                                                                                                                                                                                                                                                | Closure evidence                                                                                                                                                                                                                                | Existing EX07 IDs |
| -------------------------------------------------------- | ------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------- |
| **E01 — Deferred local-light GPU cost**                  | Complete; visually accepted 2026-09-25                              | Isolate shadow filtering, GBuffer/bindless fetches, BRDF work, register pressure/spills, divergence and overdraw. Evaluate feature specialization and uniform-data reuse before larger architecture changes. Current traced point totals: Sponza 45.288 ms / 23 draws; Instancing 26.433 ms / 39 draws.                          | Attributed bottleneck and compiled-shader evidence; measured candidate improvement beyond noise on matched synthetic and application controls; preserved image/physical response.                                                               | 02–04, 06, 09, 13 |
| **E02 — Translucent lighting GPU cost**                  | Complete; visually accepted 2026-09-25                              | Explain Sponza's 7.726 ms traced translucency cost: light overlap, forward evaluation, shadow sampling and material work. Repair demonstrated redundant work in the shared consumers.                                                                                                                                            | Matched stage and whole-frame improvement; transparent-material, forward/deferred and shadow reference checks.                                                                                                                                  | 02–04, 06, 09, 13 |
| **E03 — Grid/list scaling**                              | Complete; final integration and acceptance closed in F              | Explain B09 grid means of 10.959 ms deferred / 10.800 ms forward without Tracy. Inspect candidate tests, assignment/list construction, memory traffic and synchronization; improve measured scaling.                                                                                                                             | Count/boundary, sparse/dense, irrelevant-light, moving, 4K and multi-view comparisons; complete lists/fallback, capacity diagnostics and conservative contributor coverage preserved.                                                           | 08–10, 13         |
| **E04 — Shadow depth, bias and filtering**               | Complete; visually accepted 2026-09-25                              | Audit producer/consumer depth encoding, units, nonzero authored bias, receiver offsets and filter quality against the corresponding UE5.7 source path. Investigate comparison sampling if filtering is a demonstrated cost. The receiver-footprint repair is already delivered; complete quantitative parity is not established. | Coherent documented depth/bias/filter contract; contact, grazing/self-shadow, cube-seam, point/spot, short/long-range and quality-tier checks in both families; measured quality/time tradeoffs.                                                | 02–04, 06, 09, 11 |
| **E05 — CPU, upload, memory and resource scaling**       | Complete; visually accepted 2026-09-25                              | Find remaining redundant gather/transform, upload, binding and allocation work under scaled/mutating workloads. Measure live, queued, retired and cached bytes, slack and peaks. Retain existing CBV/state/lifetime repairs and zero steady benchmark allocation churn.                                                          | Attributed active CPU costs separated from GPU waits; matched upload/allocation and whole-frame results; fence-safe reuse/invalidation and bounded growth. Application device-wide samples alone cannot prove renderer allocation savings.      | 09–10, 13         |
| **E06 — Shadow updates and compatible cross-view reuse** | Complete; visually accepted 2026-09-25                              | Share identical local-map content across compatible views using existing ownership/cache mechanisms. Identify unnecessary updates during camera/light/caster changes; reject sharing for incompatible content or generations.                                                                                                    | Matching local-map content rendered/allocated once where compatible; incompatible views remain isolated; mutation and queued-reader checks; memory/time benefit. Directional cascades remain view-dependent.                                    | 10–11, 13         |
| **E07 — Correctness defects discovered during E**        | Complete; repairs regression-tested and final F acceptance approved | Fix discovered missing/duplicate contributions, stale light-shadow mapping, invalid caches, overflow/recovery, view contamination and lifetime failures. Preserve off-screen contributing lights and casters, authored ranges and requested shadows.                                                                             | Every discovered defect gets a linked reproduction, owning E item, repair and regression evidence. No unresolved discovered defect is silently waived or moved out of scope. Final integrated acceptance passed in F.                           | 06, 08, 10–12     |
| **E08 — Candidate qualification and operating limits**   | Complete; visually accepted 2026-09-25                              | Select matched baseline IDs and freeze improvement/regression/noise criteria before timing. Address missing CPU preflight or noisy B05-D only when needed for a comparison. Keep Tracy attribution separate from native throughput and obtain actual allocation snapshots for memory claims.                                     | Durable Markdown results with identities, percentiles, stage/whole-frame costs, memory, image/physical checks, accepted/rejected decisions and supported limits. Unaffected D evidence is reused; F receives explicit residual acceptance work. | 07, 13–14/GATE    |

**Acceptance criteria:** compare each candidate with the frozen D baseline,
retain its numerical and resource checks, and record the measured tradeoff and
visual review before adopting a new baseline. The final S9 results record the
accepted candidates and operating limits.

The historical checkpoints below retain their original capture-time decisions;
the closed E06/S9 checkpoint above supersedes their pending measurement gates.

**Historical matrix-access checkpoint:** the E01 matrix-access improvement is committed in
`8b65c42f3`, accepted with
manual Sponza and Instancing visual approval (2026-09-24), one rebuilt GPU ABI
probe test, 17 native image tests and 10 selected synthetic comparison rows.
All 12 synthetic images match the original D hashes; steady buffer/texture
creations are zero. Sponza native is 23.735 ms versus the matched original-shader
control's 65.277 ms (initial D: 62.229 ms); Instancing native is 13.677 ms
(initial D: 27.416 ms). Native variability is retained; stable Tracy attribution
and full identity/visual limits are in the
[optimization report](validation.md) and
[durable evidence](evidence/baselines/ex07e-20260924/matrix-access/register.json).
The interrupted Instancing timing attempt is excluded and replaced. Full
RenderScene builds now align with `oxyrun`; use full target builds before freezing
future capture identities. Later changed baselines still need manual approval.

**E03 result (commit `3a7742eb0`):** cooperative light-bound preparation passes all 18 native tests,
including a 65-light/partial-group/mutation regression. Four timed endpoints and
26 interaction rows preserve all 44 initial D images exactly. The matched
matrix-access-only control differs only in shaders.bin and confirms about
15–17x faster grid construction at 4,096 lights (0.717/0.676 ms deferred/forward).
The recorded one-light overhead is approximately 1/7 microseconds. The user
visually validated both application scenes and approved these comparison records
for commit on 2026-09-24; [durable proof](evidence/baselines/ex07e-20260924/cooperative-grid/register.json).

**E04/E07.1 repair (commit `31303c316`):** the nonuniform caster-normal defect is reproduced
natively (0.15 depth mismatch for identical world geometry) and repaired by
publishing/using the existing inverse-transpose normal stream. All 19 native
image tests pass in each existing Ninja Release tree (Tracy OFF/ON), and both
complete RenderScene targets are rebuilt. The
[before/after evidence](evidence/baselines/ex07e-20260924/caster-normal/register.json)
is versioned with this correctness repair. Private pass constants remain 128 bytes, with the descriptor
at offset 124; zero slope bias avoids unnecessary normal work. The retained bias
calibration is documented, and misleading UE constant names are corrected.
Its proof is committed. The uncommitted raw-gather PCF candidate passes all
20 native tests (3,888 exact comparison cases), but its Sponza timing is mixed:
Tracy point work 11.561 -> 11.711 ms, translucency 1.928 -> 1.841 ms, whole frame
24.125 -> 24.360 ms. Native 23.835 -> 23.122 ms is inconclusive because control
variability is higher. **E04 direction approved in review on 2026-09-24:**
implement UE-aligned point-light cube hardware PCF with coherent depth/bias handling.
This supersedes both earlier choice questions. Preserve the mixed raw-gather
experiment as evidence; it is not an accepted implementation or new baseline.
The review also accepted Low/Medium/High/Ultra comparison counts of
**1/5/29/29**, matching UE's 29-comparison High/Epic setting. The raw-gather
experiment has been removed from the product path; its source and measurements
remain under `out/analysis/ex07e/e04-pcf`. The
[cube PCF contract](../../../../lld/point-shadow-filtering.md) now owns the implementation:
cube-array SRVs, native cube face addressing, unbiased raster depth, receiver
comparison bias, and a 448-byte record with explicit sample count. The shared
archive contains the tested hardware-PCF implementation (223 modules after the
punctual-point variant was added). Both
existing Ninja Release RenderScene targets are rebuilt. Qualification currently
proves 31/31 setup tests (non-Tracy), 21/21 native image tests in each tree, and
3/3 shadow ABI tests in each tree. The report records the 108 bilinear comparison
cases and 594 rendered cube samples per material state, including masked cache
invalidation. Scene timing and visual acceptance were still open at this E04
checkpoint; the final accepted operating point is recorded in the
[E closeout report](validation.md).
E07.1 is repaired; its native regression now uses a projected spot, where caster
slope bias remains active.

**E04 candidate regression:** the first Sponza capture is excluded for CPU
contention under the recorded protocol. A subsequent clean rerun confirms 24.125 ->
31.606 ms GPU frame time (41.451 -> 31.639 FPS), point work 11.561 -> 17.430 ms,
and translucency 1.928 -> 3.380 ms. Measured-window CPU mean/peak is 3.435/12%,
with the same 23 point draws and unchanged scene/settings hashes. This is a GPU
regression, not CPU overload. A new baseline is **not accepted**. The user then
explicitly directed retaining justified correctness/quality work and moving to
other performance opportunities. **Stop the PCF tuning loop.** Keep the approved
1/5/29/29 quality mapping and coherent hardware-PCF depth/bias contract; do not
start a shadow-mask architecture experiment or reopen filter quality on this
instruction. Manual visual approval remains necessary for new baselines.
The report retains the numerical results. Local candidate evidence at
`plan/baselines/ex07e-20260924/point-hardware-pcf/register.json` includes CPU-load
proof. Final quality/performance acceptance and committed evidence are recorded
in the [E closeout report](validation.md).

**E06 complete:** allocation/use ownership, actual completion receipts,
per-light content identities and the combined sharing/reader migration are
implemented and qualified. E08 comparisons and manual visual acceptance are
complete; see the [final E checkpoint](#optimization-tasks-and-outcome).
The reviewer documents are committed in `fa94bbae3`.

**E07.2 repaired:** `Core/Version.cpp::Patch()` now returns
`cVersionPatch`. Both Release targets rebuild. Current major and patch both
happen to be zero; this fixes the latent API defect without claiming it explains
any measured rendering cost. The repair was made after the PCF capture pair.

**E05 CPU implementation record:** cache the four owning per-light GPU profile
descriptors (the old label constructor allocated even with Tracy disabled), reuse
CPU constant/index-order scratch, borrow already-owned publication indices instead
of copying a vector, and use stable counting-sort buckets to reduce local-light
pipeline switches. Directional remains first, sky last, and each draw retains its
original constant/selection/shadow index. Gathering now reuses already-updated
world positions and selection-vector capacity, and avoids computing point-light
orientation that the evaluation publisher discards. Spot quaternion-chain
direction semantics are preserved under scaled parents. CPU borrowing changes no
GPU registration, retirement or Nexus ownership. Both Release trees pass 34
lighting-service tests and all 23 native image tests. The new test covers parent
rotation/scale, movement and IgnoreParentTransform. The 1,024-source
native preview renders 704 local draws with **2 pipeline binds** in each tree.
The original added counter assertion read cleared end-of-frame state; its rerun
records counters inside the existing publication probe. Non-Tracy 4,096-light
lighting CPU union is 2.295 ms versus accepted post-grid 4.708 ms; recording is
0.492 versus 2.620 ms, gathering 0.894 versus 1.163 ms. Whole frame is 9.245
versus 10.669 ms, but historical noise prevents an isolated FPS claim. Reference
and frozen-D image comparisons pass; steady buffer/texture creation remains zero.
The report and local candidate evidence at
`plan/baselines/ex07e-20260924/cpu-submission/register.json` record the one-light
result, noise, CPU storage tradeoff and remaining
dynamic/multi-view/final-scene gates at that checkpoint. Final baseline
acceptance is recorded in the [E closeout report](validation.md).

The moving, two-view, orthographic and three shadowed interaction recipes now
pass in forward and deferred: **12 rows / 20 images**, all exactly matching their
current complete-list references. E05's inventory/slack/staging figures are in
the report, but a trustworthy queued/retired/cache-owned byte split is not exposed
by current instrumentation. That evidence depends on the reviewed E06 ownership
work and final E08 lifecycle checks; do not invent zero values or implement E06
infrastructure early to satisfy a counter. Final integrated Sponza/Instancing
captures and visual baseline acceptance follow E06.

Committed [automated validation records](evidence/validation/ex07e-pre-e06/README.md)
retain the passing test results separately from unapproved baseline artifacts.

**Pre-E06 evidence check (2026-09-24):** all 30 files indexed by the two candidate
evidence registers match their recorded hashes. The tracked source patch and
captured untracked shader match the replay recipe, and, at that checkpoint, the non-Tracy
benchmark executable/DLL/archive identities matched its frozen checkpoint. Saved
results confirm 23/23 image and 34/34 service tests in each Release tree. This is
a verified candidate checkpoint, not milestone closure or visual acceptance.
That was the pre-E06 checkpoint. The later independent review and implementation
approval supersede its pause; see the current checkpoint above. E06 now implements
the reviewed per-light ownership model, including overlapping view lists. Final
CPU/upload/memory assessment and application comparisons remain in E08/S9.
Point/punctual specialization is now test-validated source with an
exact-zero-radius PSO variant and mixed-source native coverage. It lowers measured
point-draw GPU time, but the Sponza capture pair does not establish a whole-frame
gain; retain that limitation in the report and stop further PCF-side iterations.
**Final E06/S9 checkpoint (2026-09-25):** the [durable report](validation.md) supersedes the pre-E06 measurement gaps above. Final native Sponza is **29.451 ms / 33.95 FPS** versus D **62.229 ms / 16.07 FPS**; Instancing is **14.606 ms / 68.46 FPS** versus D **27.416 ms / 36.48 FPS**. Four final scene runs close normally. Compatible views share **40 MiB**, with **12 moving writers for 24 map uses**; retained-reader copy-on-write is measured at **128 -> 64 MiB** after release. The review approved visual acceptance and the numeric comparison on 2026-09-25; accepted evidence is committed in `b8f1376e1`. E implementation and qualification are complete; C caller-target validation is subsequently closed in the [C report](../EX07C/validation.md); F acceptance subsequently closed with user editor approval.

**Continuation discipline:** update the owning row and this checkpoint after
each substantive investigation or accepted change, before switching work items.
Record confirmed causes separately from hypotheses, exact code/evidence paths,
tests and their results, rejected approaches, remaining gaps and the next action.
Attach new findings to E01–E08 or add a numbered subitem; do not leave obligations
only in conversation or transient analysis output. On context recovery, read this
ledger and its linked evidence before resuming. A row closes only when its
implementation or supported disposition, documentation and required validation
are recorded; no unrun check is reported as passed.

Use only existing `out/build-ninja` and `out/build-tracy-ninja`, both Release,
for the authorized E build/capture work. Allow ordinary desktop use, check for
sustained heavy CPU/GPU contention, and avoid a blanket baseline rerun. Preserve
the conservative light/caster contract. A larger rendering architecture or
quality-policy change requires a concrete design and user decision before
implementation; no shortened ranges or dropped shadows count as an optimization.

## Conventional shadow sharing — implementation plan

## 1. Scope, authority, and execution order

This plan replaces the cross-view shadow-sharing proposal. It delivers the
Nexus retirement contract, the supporting Graphics/backend lifetime changes, and
shared conventional local-shadow storage in Vortex.

The implementation contracts are:

1. [Nexus slot retirement](../../../../../../src/Oxygen/Nexus/Docs/slot-retirement.md).
2. [Conventional local-shadow sharing](../../../../lld/conventional-shadow-sharing.md).

The first LLD owns slot identity, ticket finalization, and frame-adapter behavior.
The second owns Graphics/loader integration and the rendering scenario. This plan
owns sequencing, file boundaries, tests, and acceptance evidence. Change the
owning LLD before implementing a change to a contract.

**Implementation status: S1–S8 complete; S9 automated qualification complete (2026-09-25). Visual and numeric acceptance are complete (2026-09-25); implementation/evidence commits are listed in the final report.** Independent review approved the implementation, including allocation-free
descriptor cleanup and complete resource-state recovery after uncertain issue.
**S1–S8 implementation gates and S9 automated qualification are complete.** Final
non-Tracy Release qualification: **494/494 tests**. Both existing Ninja Release
trees pass **50/50 shadow-service tests and 26/26 native image tests**. Debug
qualifies allocation-failure and lifetime paths, including retained capture across
frame-slot rollovers. Final diagnostics pass 81 tests in each Release tree and 82
in Debug. Two supplemental retained-memory cases pass in all three configurations:
forced copy-on-write measures 128 MiB and returns to 64 MiB after release; the
separate 6 MiB closing native charge also releases exactly. The [durable S9 report](validation.md)
contains 13 native + 9 Tracy final rows and four final scene captures. New baselines
have visual/numeric approval (2026-09-25) and are committed in `b8f1376e1`.
The [tracker checkpoint](#optimization-tasks-and-outcome)
provides the concise completed/remaining table.
The approved backend rule is one active or
retiring incarnation per process. Backend reload returns BackendRetiring until the
previous incarnation reaches Released.

This remains EX07E06 and is the **last EX07E implementation item**. Complete the
other optimizations and non-sharing correctness repairs, including the approved
E04 cube hardware-PCF producer/consumer contract, before starting any step below.
The Graphics and Nexus work in this plan must not be started under another item.
Final integrated E08 qualification follows this implementation. New baselines
require manual visual validation before commit.

Existing unrelated worktree changes are not part of these steps. The execution
starting point is the final source after the preceding work has completed; its
source/shader identities become the comparison control.

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

## Supporting records

- [shadow memory](shadow-memory.md)
- [validation](validation.md)
- [evidence](evidence/README.md)
- [Captured evidence](evidence/README.md)
