# Vortex Implementation Status

Status: `active milestone ledger`

This file records milestone status and the item-level exposure delivery tracker
in section 3. It is not a commit log. Update each existing item in place with
its implementation evidence, validation evidence, and remaining work.

## 1. Ledger Rules

1. Do not add per-commit evidence rows.
2. Update the existing milestone row when work advances.
3. Keep evidence concise: implementation files/areas, validation commands or
   artifact names, and the remaining exit-gate gap.
4. Do not mark a milestone `validated` unless implementation exists, required
   docs/plans are current, and validation evidence is stated in the row.
5. If scope changes, update the design document and detailed milestone plan
   before claiming progress.
6. For exposure delivery, update **Current work** before starting the next item.
   At each implementation checkpoint, update the affected work-item statuses,
   evidence and remaining gate. A validated component does not close its slice.
7. Batch coherent implementation work and use focused Debug tests between
   checkpoints. Run the broader owning Debug gate for item closure; reserve
   Release validation for slice closure, as requested on 2026-09-18.
8. Keep a TODO at each deferred exposure code boundary, naming the owning item
   or plan and the concrete work required before activating that path.
9. For subsequent broad native gates, freeze the checkpoint and binary/shader/
   fixture hashes, then delegate the specified test run to one validation
   subagent. Its mandate is test execution and reporting only: no edits, builds
   or scope expansion. Continue source implementation, review and documentation
   while it runs; defer only builds, runtime-input changes and competing GPU work.
   Reconcile its result before committing.
   Keep short focused checks local and do not restart an already-running gate.
10. Performance measurements and conclusions use Release only. Debug is for
    correctness. Slice 5.1 uses Release measurements at each performance
    checkpoint; the general rule reserving Release for slice closure does not
    postpone those measurements. Reuse Oxygen's existing profiling facilities.
11. Execute the inserted slices in order: 5 -> 5.1 performance -> 5.2 code
    quality -> 6. Resume from **Current work**, which owns the active checkpoint
    and execution state. Follow the remaining order in section 3.2.1 rather than
    restarting at EX051-01. Slice 5.2 retains its diagnostic-inventory/design
    agreement gate before quality edits.
12. Slice 5.2 requires an agreed diagnostic inventory and restructuring design
    before quality edits. Fix warnings; justify every retained suppression.
    Keep mathematical, performance and structural changes in distinct coherent
    checkpoints, each independently buildable and validated before its commit.

## 2. Status Vocabulary

| Status | Meaning |
| --- | --- |
| `validated` | Implementation exists, docs/status are current, and fresh validation evidence is recorded here. |
| `landed_needs_validation` | Substantial code exists, but this ledger does not contain fresh closure proof. |
| `in_progress` | Implementation exists but known gaps remain, or scope is actively being corrected. |
| `planned` | Scope is defined; implementation has not started for this milestone. |
| `blocked` | Required design, dependency, or proof surface is missing. |
| `future` | Explicitly deferred beyond the production-complete desktop deferred baseline. |
| `superseded` | The item was replaced or merged; its row names the current owner. |

## 3. Exposure delivery status

The [implementation plan](plan/exposure-and-lightbench-correction.md) owns the
requirements and delivery order for the original ten slices. The inserted
Slices 5.1 and 5.2 below own their detailed tasks and acceptance gates. The summary
table and item-level tracker below are the current progress record. Normative equations, layouts and lifetime rules
remain in their owning LLDs; detailed commands, results and historical checkpoints
remain in the linked local manifests and Git history.

**Resume entry point:** read [Current work](#31-current-work) first. That section
owns the active checkpoint and any execution hold. The next performance task is
EX051-03's documentation-only inventory, followed by EX051-10A; use the exact
[remaining order](#delivery-order-and-ownership). Existing manifests describe
historical checkpoints; their old `remaining` lists do not reopen closed items.

FP32 SceneColor accumulation in both modes is approved and required by the
[allocation contract](lld/scene-textures.md#per-view-fp16-suitability).
Production per-view FP16 admission and the Slice 5 numerical/scene/MultiView gate
are qualified in Debug and Release. The collected replay costs do not establish
native 60 fps performance acceptance. Slices 5.1 and 5.2 must close before
Slices 6-10, including the complete LightBench delivery; the package is not complete.

| Slice | Status | Current boundary / remaining gate | Evidence |
| --- | --- | --- | --- |
| 1 — Contracts | validated | Numeric domain, error budgets, layouts and HDR inventory have designated owners. | [Contract checkpoint](plan/exposure-contract-checkpoint.md) |
| 2 — Settings and fixed exposure | validated | Canonical authored input, immutable pass snapshots, fixed/camera gain, per-view settings and public mask acceptance are qualified. | [Fixed gain](../../out/build-ninja/analysis/vortex/exposure-lightbench/fixed-gain/evidence-manifest.json), [frame bindings](../../out/build-ninja/analysis/vortex/exposure-lightbench/frame-binding/evidence-manifest.json), [configuration and mask acceptance](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/review-r028-manifest.json) |
| 3 — Metering and adaptation | validated | Controlled-input histogram, curve, masks and hybrid adaptation; scene acceptance remains slice 5. | [Metering](../../out/build-ninja/analysis/vortex/exposure-lightbench/metering/evidence-manifest.json) |
| 4 — GPU lifecycle and sharing | validated | Controlled-input/public-event gate, including offscreen routing. Real-scene resource lifetime is tracked in slice 5. | [Lifecycle](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/discontinuity-manifest.json), [offscreen sharing](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/offscreen-sharing-manifest.json) |
| 5 — HDR migration and recovery | validated | Numerical, lifecycle and native layout/interaction correctness qualified in Debug and Release. Collected Release costs motivate the separate, still-open performance gate in Slice 5.1. | [Detailed items](#32-slice-5-work-items) |
| 5.1 — Exposure performance | in_progress | H1/H2/H4/H5 and R091/accounting are complete; H3 is rejected. Remaining order: independent SceneColor ownership, FP32-only baseline, four-pair format decision, precision policy, CPU attribution and final integration/acceptance. | [Current work](#31-current-work), [revised tasks and benchmark matrix](#321-slice-51-performance-qualification-and-correction) |
| 5.2 — Exposure code quality | planned | Plan approved on 2026-09-19. Execution follows 5.1; EX052-03 covers the concrete diagnostic inventory and suite design. Preserve numerical coverage and performance. | [Tasks and gates](#322-slice-52-code-quality-and-test-structure) |
| 6 — Authoring and persistence | planned | Native physical-camera persistence and texture-resource-index mask contracts are approved; complete source/cook/load/script/editor/DemoShell round-trip remains. | [Detailed items](#33-slice-6-work-items) |
| 7 — Light units | planned | Directional, point and spot numerical/visual calibration across forward and deferred paths remains. | [Detailed items](#34-slice-7-work-items) |
| 8 — Measurements | planned | Implement instrumentation and qualify it against independent inputs. | [Detailed items](#35-slice-8-work-items) |
| 9 — LightBench and MultiView | planned | Complete all seven experiments and final integrated native operation. MultiView layout/interaction proofs are qualified in Slice 5; final package acceptance remains. | [Detailed items](#36-slice-9-work-items) |
| 10 — Automation and acceptance | planned | Run the same experiments through automation, close every acceptance gate and reconcile owner documents. | [Detailed items](#37-slice-10-work-items) |

### 3.1 Current work

**Performance resumed (2026-09-20): 09 and 11A validated; next is 11B publication.** The eight
existing workload entry points, fixed controls, public event operations and
bounded event script are mapped below. The SceneTextures owner inventory links
the existing descriptor and lifecycle reports; no build or native run was needed
or performed for this documentation-only item. Independent color ownership
passes 56 focused Debug checks, including queued consumers and the specified 4K
temporal-off lifecycle case. Final engine placement peak is 3747.207 MiB; all
three retained-resize cycles keep identical attachment populations, and all
retired family leases reach zero. The superseded double-retirement drafts and
their diagnosis remain in the [10A checkpoint](../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/color-ownership/checkpoint-manifest.json).
EX051-04 is validated: 109 focused Debug checks (latest unique outcomes), 13
Release checks and both 234-module shader builds pass. Its
[checkpoint](../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/fp32-only/checkpoint-manifest.json)
preserves the corrected temporal-test observation failure and exact frozen
inputs. EX051-05's four pairs now pass: eight timed runs, 60,000 sampled frames,
ten endpoint comparisons, zero steady allocation churn and no repeats. FP32-only
reduces graphics-queue frame p95 by 25–50%; admitted half saves memory only in C01.
See the [decision table](#ex051-05-results-and-decision) and
[checkpoint](../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/decision05/checkpoint-manifest.json).
The user approved the [concrete 09 policy](lld/post-process-service.md#agreed-ex051-09-precision-policy)
and C01 memory tradeoff on 2026-09-20. FP32 production, explicit `qualified`
diagnostics and stale-control acknowledgement rejection are implemented and
pass 122 focused Debug checks (97 CPU, 25 native). The
[policy checkpoint](../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/production-policy/checkpoint-manifest.json)
records exact commands and 648 unchanged frozen inputs. 11–14/GATE remain open.
Event-script execution remains
in 13; neither this inventory nor historical manifests close final acceptance.

**Complete: corrected quality review of every file under
`src/Oxygen/Vortex/Test` (2026-09-20).** Scope: 156 files, comprising 129 C++
translation units, 21 headers and six non-C++ files. The earlier result based on
broad warning suppressions is superseded by this validated correction.

The rejected optional-access, unchecked-indexing, constant-array-indexing,
arithmetic-precedence, pointer-arithmetic and missing-field suppressions are
removed. Code now uses checked access, explicit guards, spans/range algorithms,
explicit arithmetic grouping, and normally constructed input records. Retained
exceptions are local to the specific interface, framework or lifetime constraint
and document that constraint; generic suppression boilerplate is removed.

Every non-empty braced initializer in the **139 C++ files modified since review
baseline `34819517`** has a trailing comma. This includes earlier committed
changes. Clang syntax/token analysis verifies complete coverage and protects
typed temporaries inside GoogleTest macro arguments. Final reparse inventories
have zero missing commas and zero unresolved manual cases.

| Reviewed group | C++ units | Headers | Final clang-tidy |
| --- | --- | --- | --- |
| Exposure cases | 32 | 0 | 0 diagnostics |
| Rendering and scene preparation | 41 | 1 | 0 diagnostics |
| Resources, upload, shared fixtures and fakes | 37 | 12 | 0 diagnostics |
| Exposure fixtures and benchmarks | 19 | 8 | 0 diagnostics |

**Validation:** all 47 Vortex targets build in Debug; the Release exposure target
also builds, including guarded benchmark code. All 47 Debug executables pass:
**876 tests** (656 CPU and 220 native GPU), zero failures/errors/skips, plus the
link test. Eight disabled benchmarks remain unexecuted. Numerical tolerances,
test identities and benchmark workloads are preserved. Both reported emission
death tests pass with valid context and specific intended-failure matchers.
Clang-format, markdownlint and `git diff --check` pass.

**Evidence:** `out/clang-tidy/vortex-test-quality/quality-summary-corrected.json`,
`review-ledger.json`, `initializer-style-final.json`, per-lane current-source
reports, `build-final-all-debug.log`, `build-final-exposure-release.log`,
`correctness-final-cpu/result.json`, and
`correctness-final-exposure/result.json`. Failed intermediate and superseded
suppression-based results remain separate. This closes the requested test review;
the broader Slice 5.2 engine-quality scope remains pending EX051-GATE.

### 3.2 Slice 5 work items

**How to read the items:** `validated` closes only the named item's stated scope;
`in_progress` means partial work or active investigation, with the gap stated;
`planned` means that delivery item has not started. Existing prerequisites are
identified explicitly and do not close a later slice. A slice closes only when
its gate row is validated. Manifests describe their checkpoint; these rows own
current status, including decisions that supersede older manifest limitations.

[Requirements and gate](plan/exposure-and-lightbench-correction.md#slice-5---complete-pre-exposure-migration-and-numerical-recovery).
**Slice status: validated. Gate: passed in Debug and Release.**

| ID | Work item | Status | Completed scope / remaining work | Evidence |
| --- | --- | --- | --- | --- |
| EX05-01 | Early GPU P resolve and immutable frame P/1P binding | validated | GPU-owned P, pinned frame/state leases, submission failure and repeated-resolve behavior are implemented. | [Frame resolve](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/frame-resolve-manifest.json), [domain](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/domain-manifest.json) |
| EX05-02 | FP32 SceneColor accumulation and HDR format plumbing | validated | Approved FP32 accumulation contract and format-aware resource/PSO plumbing exist. This is not automatic mode switching. | [Formats](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/formats-manifest.json), [allocation contract](lld/scene-textures.md#per-view-fp16-suitability) |
| EX05-03 | Meter with 1/P; consume S/P in all exposure modes | validated | Unified gain consumption covers Manual, ManualCamera, Auto, disabled and zero-target behavior in the qualified fixtures. | [Domain](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/domain-manifest.json), [mode fixtures](../../out/build-ninja/analysis/vortex/exposure-lightbench/multiview/modes-manifest.json) |
| EX05-04 | Deferred emissive/direct/indirect and forward lit/unlit/masked/translucent P domains | validated | **Source corrections:** emissive RGB was omitted from material-cache identity; shared half decoding halved subnormals and encoding dropped the carry into minimum normal (R038). All three source-value regressions are corrected; Debug/Release each pass 327 affected tests. This closes the named corrections only. The original non-emissive sunlit materials now have a qualified Average/Spot comparison in both views/builds; identical HDR inputs separate meter-target washout from adaptation. The 324-case cooked-material domain matrix now passes in both builds, including HDR/sRGB sampling and forward resolve publication. Direct/indirect producer endpoints are now qualified by the lighting matrix. Mixed-content Debug acceptance now passes: four exact isolated/family image comparisons, actual opaque/masked/emissive/translucent contributions and the 284-test owning gate. Release is qualified in EX05-GATE. | [Lit-profile investigation](../../out/build-ninja/analysis/vortex/exposure-lightbench/multiview/lit-profiles-manifest.json), [Mixed acceptance](../../out/build-ninja/analysis/vortex/exposure-lightbench/multiview/mixed-interactions-manifest.json), [Cooked-material matrix](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/material-domain-increment-manifest.json), [Source-value evidence](../../out/build-ninja/analysis/vortex/exposure-lightbench/multiview/source-values-manifest.json), [source regressions](../../src/Oxygen/Data/Test/HalfFloat_test.cpp), [material regression](../../src/Oxygen/Vortex/Test/Resources/MaterialBinder_basic_test.cpp), [scene migration scope](../../out/build-ninja/analysis/vortex/exposure-lightbench/scene/scene-migration-manifest.json), [producer inventory](lld/scene-textures.md#exposure-hdr-domain-and-format-inventory) |
| EX05-05 | Sky/background and sky-view/AP producer-consumer P plumbing | validated | Paired P-domain/binding proofs and R036 low/zero-opacity correction are qualified. The isolated AP fixture adds actual deferred/alpha-one-forward whole-HDR-image agreement and readable mixed presentation in both views/configurations. R040/R041 additionally qualify exactly-once opaque-forward AP and explicit shading routing, with exact full-image agreement. This does not close broader material families or quantization/mode switching (EX05-04/15–19). | [Scene migration](../../out/build-ninja/analysis/vortex/exposure-lightbench/scene/scene-migration-manifest.json), [real products](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/required-products-manifest.json), [controlled AP correction](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/review-r036-manifest.json), [native scene AP fixture](../../out/build-ninja/analysis/vortex/exposure-lightbench/multiview/atmosphere-composition-manifest.json), [opaque forward](../../out/build-ninja/analysis/vortex/exposure-lightbench/multiview/opaque-ap-manifest.json) |
| EX05-06 | Height/volumetric fog and RGB history rebasing | validated | Current/stored P conversion and unchanged transmittance are implemented and exercised. Cumulative temporal quantization is not covered by this item. | [Scene migration](../../out/build-ninja/analysis/vortex/exposure-lightbench/scene/scene-migration-manifest.json), [history/resource lifetime](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/review-lifetime-manifest.json) |
| EX05-07 | Diagnostic colors, wireframe and display overlays | validated | Per-view unit-gain diagnostics, persistent exposure preservation and frame-retained overlay constants are qualified. Full feature-layout acceptance remains EX05-29. | [Diagnostics](../../out/build-ninja/analysis/vortex/exposure-lightbench/multiview/diagnostic-manifest.json) |
| EX05-08 | Canonical processed cubemap narrowing and upload packing | validated | Half/float resource choice, normalization and matching face/mip upload packing are qualified at the static-cubemap scope. | [Cubemap](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/cubemap-manifest.json) |
| EX05-09 | Bloom and remaining temporal/capture domain audit | validated | Active external bloom handoff qualified: disabled bloom supplies no SRV; matching-P radiance is added before S/P, including checked-half/fallback scene inputs. Debug gate: 218 passed; 24 domain/toggle cases and nine checked-color cases. Owned bloom shaders are inactive UV placeholders, threshold has no active filter, and temporal/captured/specular producers remain feature dependencies with code TODOs. | [Audit and native evidence](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/bloom-handoff-manifest.json), [consumer contract](lld/post-process-service.md#post-chain-and-qualification) |
| EX05-10 | Checked SceneColor conversion and conditional FP32 tonemap fallback | validated | Whole-image check precedes narrowing; rejected half contents are not sampled; current conversion verdict is separate from future-candidate qualification. Production admission and checked handoff are implemented in EX05-17; queued consumers are Debug-qualified in EX05-18. | [Conversion](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/conversion-manifest.json), [selection](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/selection-manifest.json), [separate reports](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/precision-status-manifest.json) |
| EX05-11 | GPU per-view suitability and two-result stability | validated | Required-product identity, candidate P, own precision history for borrowers, streak reset and bounded status publication are qualified as primitives. | [Eligibility](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/eligibility-manifest.json) |
| EX05-12 | Completed-status candidate selection and failure invalidation | validated | Lifetime/settings/layout/generation checks, bounded retry queue, stale-result rejection and failed-solve/fallback invalidation are qualified. Successful same-frame reuse retains acknowledgement. | [Status selection](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/precision-status-manifest.json), [R034 correction](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/review-r034-manifest.json) |
| EX05-13 | Collect real required scene-reference products | validated | Persistent scene views collect SceneColor, sky-view, AP and fog; missing producers remain missing requirements. Native capture checks 151,424 texels per view in the environment fixture. | [Required products](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/required-products-manifest.json) |
| EX05-14 | Finite/overflow checks before environment stores | validated | Sky-view/AP/fog report original finite/nonfinite and FP16 headroom failures through the existing status. Auto does not adapt from a producer-failed image. This is not a quantization certificate. | [Pre-store range](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/prestore-range-manifest.json) |
| EX05-15 | Quantization, cumulative image/meter error and temporal bounds | validated | Hardware-filtered current/candidate certificates cover producer stores, retained history, sky/AP/fog/translucent composition, coverage/depth and final image/meter tolerances. Preparation precedes checked conversion; eligibility follows it. GPU status384 / CPU prefix80; no additional HDR texture. Debug/Release each pass 330 owning tests and 308,016 exact arithmetic checks. Final Release Fog/Local audits pass 8,192 transfers; the four-phase visual/ROI cycle and 12 negative controls pass. User confirmed the local-fog lifetime correction. Controlled 1080p/4K and actual two-view scene costs are recorded. Production format switching and queued consumers are Debug-qualified in EX05-17–18. | [Completion evidence and commands](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/consumer-completion-manifest.json), [Runtime contract](lld/post-process-service.md#quantization-error-propagation), [Per-view budgets](lld/scene-textures.md#per-view-fp16-suitability) |
| EX05-16 | Full supported-radiance envelope at upstream producers | validated | Active producer matrix and pre-composition source checks qualified in Debug. Final gate: 350 tests; after the no-prepass depth-publication correction, 65 affected tests pass (80 native cases). Lighting matrices cover 324 direct, 180 SH, 144 analytic sky and 10 distant-sky cases; compiled replay and targeted capture are inspected. Release is qualified in EX05-GATE. | [Lighting/source closure](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/lighting-producer-increment-manifest.json), [producer mapping](lld/scene-textures.md#producer-range-qualification), [environment increment](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/domain-producer-increment-manifest.json), [material increment](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/material-domain-increment-manifest.json), [injection increment](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/injection-increment-manifest.json) |
| EX05-17 | Production per-view FP16 admission and allocation switching | validated | Early current-layout/source-compatible candidate selection is enabled. FP32 accumulation, half-capable environment qualification, checked resolve/fallback and supporting ownership are implemented. All 382 owning Debug tests pass; native admission, eight resize/view cases, source changes, fallback retention/failures and inspected S/P capture pass. Vacuum-layout proof is distinct from remaining nonzero/full recovery acceptance. | [Completion evidence](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/admission-increment-manifest.json), [resource contract](lld/scene-textures.md#per-view-fp16-suitability) |
| EX05-18 | Checked resolve extraction and all conditional-consumer leases | validated | EX05-17 ownership is qualified through a three-view mixed-format auxiliary family (18 outputs) and delayed offscreen HDR consumers across slot reuse/view removal. Five queued tonemap consumers cover accepted half, deterministic rejected-half fallback/control and full formats, with extraction release before the fence. Frozen Debug gate: 190 passed; later sentinel-only focused test passed. Release is qualified in EX05-GATE. | [Queued-consumer evidence](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/queued-consumer-manifest.json), [ownership contract](lld/scene-textures.md#36-scenetextureextracts), [Admission/ownership increment](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/admission-increment-manifest.json) |
| EX05-19 | Automatic recovery, excessive-range retention and stable return | validated | Producer failures request one implicit Auto Remeter; conversion-only rejection preserves valid adaptation. Fixed/disabled/borrowed failures and newer explicit requests retain authority. Native scene tests cover five owner/control modes, six-frame unsupported retention, 46-stop FP32 adaptation, reduced-range return and contrasting shared formats. Debug gate: 238 passed; 37 frozen hashes unchanged. Release and the scene lifecycle combinations are qualified in EX05-GATE. | [Recovery evidence](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/recovery-increment-manifest.json), [runtime contract](lld/post-process-service.md#bootstrap-recovery-and-format-eligibility) |
| EX05-20 | Scene/environment descriptors, histories and queued constants retire safely | validated | Same-frame offscreen resources, fog/HZB removal, transient histories and queued HZB constants are qualified. Mode-transition-specific leases are Debug-qualified in EX05-18. | [Environment retirement](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/review-r027-manifest.json), [fog/HZB lifetime](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/review-lifetime-manifest.json) |
| EX05-21 | Actual memory and bandwidth accounting | validated | Four opt-in native 1080p/4K × temporal-off/on cases pass; creation-time peaks, retained outputs, cached families and shared canonical LUTs are measured from deduplicated D3D12 resources. Four capture replays verify format/P/S and final pixels, direct logical texture traffic including promoted producer writes, and Release-only performance measurements. Texture/report exclusions are explicit; Release is qualified in EX05-GATE. | [Full evidence and scope](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/accounting-manifest.json), [allocation contract and measured table](lld/scene-textures.md#lifetime-and-memory-accounting) |
| EX05-22 | Scene startup/cuts/seeds/modes/pause lifecycle matrix | validated | 160 real-scene frame checks cover forward/deferred HDR startup, invalid-meter startup and seeds, cuts/overrides/retries, pause/zero-speed adaptation, Manual/Auto/disabled/physical camera in both projections, compensation and zero/locked target precedence. All 196 native Debug tests pass; an inspected 2^32 startup capture verifies FP32/P1 and final S/P. EX05-19 supplies recovery/retention controls. Release and the lifecycle/layout gate are qualified in EX05-GATE. | [Scene lifecycle closure](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/scene-lifecycle-manifest.json), [recovery matrix](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/recovery-increment-manifest.json) |
| EX05-23 | Inactive, removed, recreated and replaced-world view lifecycle | validated | Public offscreen release, retained GPU readers, fresh reuse, registered-owner protection, camera-history invalidation, age-60/61 pruning and actual scene replacement pass on both shading paths. MultiView tears down its persistent offscreen pairs. 222 Debug tests and the owning MultiView Debug build pass; Release is qualified in EX05-GATE. | [Lifetime closure](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/scene-retirement-manifest.json) |
| EX05-24 | Shared exposure and source-loss scene combinations | validated | 102 native scene frames cover both shading paths and actual render orders, contrasting images/settings, initial fallback, zero/manual/physical/disabled source modes, diagnostics, inactive ownership and six source-loss policies. First-frame later-view transform publication is corrected and captured. Owning Debug gate: 294 tests passed. Full pane/layout combinations are qualified in EX05-29/30; Release is qualified in EX05-GATE. | [Shared-scene closure](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/scene-sharing-manifest.json) |
| EX05-25 | Delayed/stale status, stateless Auto and device recovery | validated | Real-scene tests cover stateless endpoints/invalid input/zero-target recovery, delayed actual GPU readback delivery while FP32 adapts, stale generation/settings/lifetime rejection and public device-event precedence/requalification. All 204 native Debug tests pass; the stateless 2^32 capture verifies FP32/P1 and S/P. Physical adapter removal is outside this event-contract proof; Release is qualified in EX05-GATE. | [Scene status closure](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/scene-stateless-recovery-manifest.json), [bounded status retries](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/status-retry-manifest.json) |
| EX05-26 | Main/lit-PiP isolation, reordering and alone-versus-family equivalence | validated | Paused/static proof inputs have exact gain/meter/image agreement and specified sharing latency. This does not qualify moving-camera adaptation or all layouts. | [Static matrix](../../out/build-ninja/analysis/vortex/exposure-lightbench/multiview/static-matrix-manifest.json) |
| EX05-27 | Viewport, scissor and whole-window resize | validated | The named scripted resize/scissor fixtures pass without cross-view contamination. | [Viewport/scissor](../../out/build-ninja/analysis/vortex/exposure-lightbench/multiview/viewport-manifest.json), [window resize](../../out/build-ninja/analysis/vortex/exposure-lightbench/multiview/window-resize-manifest.json) |
| EX05-28 | MultiView per-view mode, seed, cut and diagnostic events | validated | Paused scripted events and diagnostic restoration are qualified; runtime physical-camera values are covered, persistence is not. | [Modes](../../out/build-ninja/analysis/vortex/exposure-lightbench/multiview/modes-manifest.json), [diagnostics](../../out/build-ninja/analysis/vortex/exposure-lightbench/multiview/diagnostic-manifest.json) |
| EX05-29 | Complete standard/auxiliary/offscreen/feature layout matrix | validated | All five native layouts pass in Debug and Release: 17 selected views/19 comparisons within frozen tolerances, exact auxiliary copies, three expected-black cells and feature-stage checks. The offscreen proof preserves its original Average profile; corrected Debug/Release comparisons and image inspection pass. | [Debug layout evidence](../../out/build-ninja/analysis/vortex/exposure-lightbench/multiview/layout-matrix-manifest.json), [Final closure](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/slice5-closure-manifest.json) |
| EX05-30 | Native combined interactions and active adaptation | validated | The refreshed native sequence passes 227 view checks across 58 frames: modes, camera motion, seed/cut, ordering, viewport/scissor/window resize, retained/recreated lifetime, prior-owner sharing, source loss/recreation and pause. Independent histogram/target/response checks pass; 11 negative checker controls reject corruption. Both native debug-layer audits and complete client inspection pass. Owning Debug gate: 284 passed with 38 unchanged runtime inputs. Release is qualified in EX05-GATE. | [Section 7.5](plan/exposure-and-lightbench-correction.md#75-multiview-visual-acceptance), [Active evidence](../../out/build-ninja/analysis/vortex/exposure-lightbench/multiview/mixed-interactions-manifest.json), [Offscreen intent prerequisite](../../out/build-ninja/analysis/vortex/exposure-lightbench/multiview/offscreen-intent-validation-result.json) |
| EX05-GATE | Slice 5 numerical and integration acceptance gate | validated | Passed in Debug and Release. Release: 504 owning tests plus four opt-in accounting runs; 47 runtime hashes unchanged. Native layouts, material contributions, 227 active view checks/58 frames, debug-layer audits and presented output pass. This is not native 60 fps acceptance: EX051-GATE remains open. Approved inactive feature boundaries remain documented. | [Full requirement/evidence mapping, commands and results](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/slice5-closure-manifest.json) |

### 3.2.1 Slice 5.1 performance qualification and correction

**Slice status: in_progress. Scope revised 2026-09-20. Execution state: [Current work](#31-current-work).**

#### Delivery order and ownership

Remaining order: **03 inventory -> 10A SceneColor ownership -> 04 FP32-only
baseline -> 05 format-benefit decision -> 09 precision policy -> 11 CPU
attribution -> 12 correctness integration -> 13 final acceptance -> 14 closeout**.

EX051-01/02/06/07/10 are complete within their named scopes. EX051-08 is
superseded by the temporal precision work in 09. An implementation item closes
with its own correctness checks, measured result and accept/reject decision.
Only 13 owns the final repeated performance matrix. GATE evaluates that evidence.
A completed or rejected experiment does not wait for 13 to close.

**Next action:** execute approved EX051-11B constant/publication work after the
accepted 11A recorder-ownership correction. The
[bounded contract](lld/post-process-service.md#ex051-11-cpu-attribution-checkpoint)
was agreed by the user on 2026-09-20. Existing traces do not demonstrate the
active-CPU budget; each candidate requires its own accept/reject evidence.
03, 10A, 04 and the four 05 pairs are complete; they do not close 13.

Use the existing renderer, profiling APIs, native workloads and GPU status
reports. Keep FP32 lighting accumulation, exposure equations, current-frame range
protection, image/meter tolerances, immutable leases and GPU-fence retirement.
FP16 is an optional storage optimization for resolved color and per-view
sky/AP/fog products. The FP32-only baseline and precision policy are specified in
[PostProcessService](lld/post-process-service.md#slice-51-fp32-baseline-and-precision-policy).
SceneColor fallback ownership is specified in
[SceneTextures](lld/scene-textures.md#ex051-10a-independent-scenecolor-fallback-ownership).

#### Established results and remaining design problem

- H1 maximum reuse and H2 scan fusion are retained. H3 bound-publication reduction
  was rejected and reverted. These experiments are closed.
- H4 depth sharing and H5 resource reuse are committed. The H5 I02 checkpoint
  removes 12 texture and two buffer creations per frame, with unchanged placement.
- R091 and lifecycle accounting are committed as `8903305`. Seven focused Debug
  cases and eight Release cases on each side pass. Matching pre-H5/current
  texture, buffer, combined, engine and HDR placement peaks are identical.
- The H5 I02 recording stays FP32 for all 3,600 sampled frames. Mean explicit
  exposure time is 2.146 ms: candidate qualification 1.377, gradients 0.328,
  scene-range checks 0.220, metering/adaptation 0.180 and other work 0.041 ms.
- Before 10A, conditional FP16 output retained a whole SceneTextures family for its FP32
  fallback. Temporal-off retirement retained an extra 302.25 MiB at 1080p and
  1,128.75 MiB at 4K; 216 and 806.25 MiB respectively are unrelated attachments.
  EX051-10A now fixes that ownership with independent color leases.
- The existing `fp32` control keeps certification enabled. Temporal production
  and that control differ in P at 13 per-view observations across seven phases.
  EX051-04 supplies the qualified FP32-only baseline; 05's results are below.

#### Acceptance contract

Keep numerical baseline `6092dc0d6` and its manifests. Use RTX 3080 / Ryzen 9950X
as the reference system and record adapter/LUID, driver, clocks, power settings,
compiler/shader options and runtime identities. The primary target is 1080p/60.
4K qualifies scaling. A secondary view is half the main width and height.

| Metric on frozen workloads | One 1920x1080 view | 1920x1080 main + 960x540 secondary | 4K scaling requirement |
| --- | --- | --- | --- |
| Native GPU frame, steady p95 / p99 | <=14.0 / <=16.67 ms | <=14.0 / <=16.67 ms | Report both percentiles and maximum; no 60 fps claim required |
| Uncapped complete frame interval, steady p99 | <=16.67 ms | <=16.67 ms | Report CPU/GPU/presentation limits separately |
| Attributed exposure + precision GPU work, p95 / p99 | <=0.50 / <=0.75 ms | <=0.65 / <=1.00 ms | p95 <=2.00 / <=2.60 ms respectively, and <=4.5 times corresponding 1080p p95 + 0.05 ms |
| Exposure CPU preparation/submission, p95 / p99 | <=0.10 / <=0.20 ms | <=0.15 / <=0.30 ms | Active CPU p95 <=1.25 times 1080p + 0.02 ms; report waits separately |
| Warm exposure transitions: additional GPU work over matched steady state, p99 | <=1.00 ms | <=1.30 ms | Report scaling and worst transition; preserve event-frame semantics |

Attribute metering, qualification, reductions, producer error bookkeeping,
exposure-related copies and CPU preparation to exposure. Report ordinary fog,
base rendering and whole-frame time separately. Compute per-frame interval unions
before percentiles; never add nested scope times or independent percentiles.

Memory accounting includes live, in-flight, retained and cached allocations,
outputs and reduction scratch. Repeated fixed-descriptor lifecycle cycles must
stabilize, steady-state creation churn must be zero, and matching lifecycle peaks
must not grow by more than 5% without an approved tradeoff. Record placement bytes
and cached-family populations separately. Baseline/candidate comparisons use the
same executed workload and common correctness fixes, identified by source hashes.

The numeric targets and their published context were established in the
[approved baseline manifest](../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/baseline-manifest.json).
Keep workload quality and numeric targets fixed. An unrelated rendering
bottleneck requires a separate owner decision.

#### EX051-04 runtime controls

| Selector | Behavior | Use |
| --- | --- | --- |
| `production` | Validated 09 default: FP32/P1, normal exposure and current range protection; no automatic half admission. | Production operating mode; historical D01-D04 used the previous dynamic policy at `5f5aa9e85`. |
| `fp32` | Existing format-only diagnostic control; qualified P and certification remain enabled. | Existing numerical/format diagnostics. |
| `fp32-only` | Qualified all-FP32 path with P=1. Preserve metering, adaptation, events, source sharing, temporal rendering and current-frame range checks. Omit FP16 candidate scans, FP16-only error certificates and eligibility-only status jobs. | Performance and memory reference for 05. |
| `qualified` | Existing checked FP16/FP32 qualification selected explicitly for diagnostics. Two actual eligible frames and all current checks remain required. | Preserves certified producer/consumer coverage; not automatic production policy. |

Extend `OXYGEN_EXPOSURE_BASELINE_PRECISION` with `fp32-only`. Use runtime selection
in one Release binary. A mode change resets only precision qualification and
rebases or rebuilds affected radiance histories; it preserves exposure gain and
pending authored transitions. Transition acknowledgements remain active.

#### EX051-05 fixed decision benchmark

Run these four pairs after 10A and 04. Each pair compares `production` with
`fp32-only` on identical scene, quality, camera path and simulation delta time.

| Pair | Existing workload | Main / secondary | Decision |
| --- | --- | --- | --- |
| D01 | C01: controlled Manual, deferred, temporal off | 1920x1080 / 960x540 | Does admitted FP16 improve total frame cost and memory? |
| D02 | C01 | 3840x2160 / 1920x1080 | Does the benefit scale with resolution? |
| D03 | C02: controlled Manual, deferred, temporal on | 1920x1080 / 960x540 | Does dynamic precision help temporal rendering? |
| D04 | I02: moving indoor/outdoor Auto, forward, temporal on | 1920x1080 / 960x540 | What does admission cost when production remains FP32? |

**Four pairs, eight timed runs, one Release binary.** Keep the existing opt-in
entry points `ExposureProfilingOverheadTest.DISABLED_ReleaseControlledBaseline`
and `ExposureIndoorOutdoorBenchmarkTest.DISABLED_ReleaseIndoorOutdoorBaseline`.
Use `OXYGEN_EXPOSURE_BASELINE_CASE`, `OXYGEN_EXPOSURE_TIMING_WIDTH`,
`OXYGEN_EXPOSURE_BASELINE_PRECISION`, `OXYGEN_EXPOSURE_BASELINE_FRAMES` and a unique
`OXYGEN_EXPOSURE_BASELINE_RUN`. No source swapping or rebuild between controls.
`OXYGEN_EXPOSURE_BASELINE_FRAMES=warmup` runs the same warmup without a timed
population and writes its throughput to the normal unique run manifest. Use
both controls' warmup results to select one common frame count with duration
headroom; use an integer for each of the eight timed runs. This is an untimed
calibration of the existing entry points, not another performance decision run.

- Warm for at least 300 frames and 10 seconds. I02 warmup includes a complete
  1,200-frame path cycle. Both controls use the same simulation timebase.
- Choose one common measured frame count per pair from the faster control's
  warmup throughput. Collect at least 1,800 frames and 30 seconds per condition;
  I02 populations contain whole path cycles.
- Collect native optimized Release data with VSync, caps, dynamic resolution,
  frame generation, RenderDoc, debug layer and GPU validation disabled.
- Reuse `GpuTimelineProfiler` and `CpuProfileScope`. Reject incomplete,
  overflowed or cancelled recordings. Keep graphics-queue timing and CPU waits
  identifiable; do not treat the graphics timeline as a cross-queue critical path.
- Record complete-frame and GPU-frame distributions, qualification/gradient
  time, memory, format occupancy and output correctness. Capture existing
  rejection reports and P at fixed untimed checkpoints, outside the sample window.
- Report p50/p95/p99/max with nearest-rank percentiles and all sampled frames.
  Preserve slow frames. Record thermal/clock variation.
- Repeat a pair once only when timing variation prevents its decision. If the
  repeated result is still ambiguous, record no demonstrated benefit and stop.

D01/D02 decide whether to retain further FP16 optimization work. D03 selects the
next temporal precision design. D04 defines the cost to remove from the FP32
steady state. EX051-05 records those decisions before 09 changes production policy.
The existing format-only control does not substitute for `fp32-only` in these pairs.

#### EX051-05 results and decision

Completed in one qualified Release binary from implementation `5f5aa9e85` on
the reference RTX 3080 / Ryzen 9950X system. Each pair used one common count
chosen from both untimed warmups; every condition exceeded 30 seconds. All
60,000 frames, including slow frames, remain in the native records. No recording
was incomplete, overflowed or cancelled. All 648 frozen identities stayed equal.

| Pair | Frames / condition | GPU frame p95: production / FP32-only (ms) | FP32-only minus production steady placement (MiB) | Measured decision |
| --- | ---: | ---: | ---: | --- |
| D01: C01, 1080p | 9,600 | 8.385 / 5.322 | +106.000 | Admitted half saves memory but has no frame-time benefit. |
| D02: C01, 4K | 5,100 | 16.084 / 9.444 | +398.625 | Memory saving scales; the time penalty grows to 6.640 ms. |
| D03: C02, 1080p | 9,300 | 10.359 / 5.158 | -1.125 | Both stay FP32; reject recurring temporal admission work. |
| D04: I02, 1080p | 6,000 | 11.015 / 8.232 | -1.125 | Both stay FP32; remove recurring admission work from this operation. |

GPU metrics are graphics-queue frame spans, not a cross-queue critical path.
Placement is the complete traced steady snapshot, including outputs/cache/
in-flight resources, not heap commitment or a lifecycle peak. The C01 increases
are 13.1% and 13.6% of production's traced placement; this is an explicit policy
tradeoff requiring the 09 agreement, not an assumed memory-budget exemption.

All ten endpoint pairs meet the existing 0.5% + 2e-5 float and one-code-value
image budgets. C01/C02 are byte-identical; I02's largest float delta is
2.742e-6, its largest independent UNorm8 delta is one, and endpoint gain differs
by at most 3.830e-6 stops. All eight runs have zero sampled texture and buffer
creations. Within-run block p95 ranges are separated for every pair, so variation
does not prevent these decisions and no pair is repeated.

**Recommendation for 09:** make FP32/P1 the production default and keep checked
FP16 qualification available only as an explicit diagnostic. This ends further
production FP16 optimization in this slice, subject to user agreement on the
memory tradeoff. The complete state/validity table is in the PostProcess owner.
This is not final acceptance: for example, I02's FP32-only explicit-exposure
p95 is 0.683 ms before attributing resolve work, above the 0.650 ms target.
11 must separate active CPU/submission work from waits; 12/13 own integrated
correctness and final budgets. No target or image tolerance is changed.

[One derived decision report, raw paths/hashes and all distributions](../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/decision05/decision-table.json)
and [checkpoint manifest](../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/decision05/checkpoint-manifest.json).

#### EX051-13 final acceptance matrix

Use these frozen recipes in `production` at 1080p and 4K. Run each cell three
independent times after the final implementation: **16 cells, 48 steady runs**.
Each run meets the warmup/sample protocol above. FP32 diagnostic controls remain
in 05 and are not a second 48-run acceptance matrix.
Every acceptance run must meet its cell's targets. Report each run and the
aggregate; do not replace a failed run with a pooled percentile.

| Recipe / `OXYGEN_EXPOSURE_BASELINE_CASE` | Views | Exposure | Path | Temporal fog | Existing test entry point / scene |
| --- | ---: | --- | --- | --- | --- |
| C01 | 2 | Manual EV0 | Deferred | Off | `ExposureProfilingOverheadTest.DISABLED_ReleaseControlledBaseline`; emissive triangle 0.25, vacuum atmosphere, zero-extinction fog |
| C02 | 2 | Manual EV0 | Deferred | On | Same controlled entry point and scene |
| M01 | 1 | Auto | Deferred | Off | `ExposureProfilingOverheadTest.DISABLED_ReleaseMixedBaseline`; static MultiView mixed scene |
| M02 | 2 | Auto | Deferred | On | Same mixed entry point and scene |
| M03 | 2 | Manual EV14.5 | Forward | Off | Same mixed entry point and scene |
| M04 | 1 | Auto | Forward | On | Same mixed entry point and scene |
| I01 | 1 | Auto | Deferred | On | `ExposureIndoorOutdoorBenchmarkTest.DISABLED_ReleaseIndoorOutdoorBaseline`; mixed scene plus six-piece shadowed enclosure |
| I02 | 2 | Auto | Forward | On | Same indoor/outdoor entry point and scene |

**EX051-03 executable inventory.** All entries are in
[`ExposurePerformance_bench.cpp`](../../src/Oxygen/Vortex/Test/Exposure/Benchmarks/ExposurePerformance_bench.cpp)
and run from `out/build-ninja/bin/Release/Oxygen.Vortex.ExposureGpu.Tests.exe`
with `--gtest_also_run_disabled_tests --gtest_filter=<entry-point>`.
[`ExposureBaselineSetup.cpp`](../../src/Oxygen/Vortex/Test/Exposure/Benchmarks/ExposureBaselineSetup.cpp)
owns the selectors, view count, path and temporal settings. Set width to 1920 or
3840, precision to the item-required control, frames to the paired sample count,
and run ID to a unique name using the five environment variables listed in 05.
`fp32-only` is implemented and qualified in 04.

Common controls in
[`ExposureBaselineScenario.h`](../../src/Oxygen/Vortex/Test/Exposure/Benchmarks/ExposureBaselineScenario.h):
16,666,667 ns simulation dt, three frame slots, Average metering, key 12.5,
None tone mapper, gamma 1, jitter off, AP width 64/depth 32/range 96 km/two
samples per slice, fog history-miss supersampling 4 and directional shadows on.
Other exposure fields use the existing `scene::ExposureSettings` defaults;
the run manifest records their resolved values. Views/handles are 500 and 501;
secondary dimensions are half the main. Controlled cameras retain aspect 1/FOV
1 radian; mixed cameras use aspect 16:9/FOV 45 degrees. Caller outputs are FP32.
The indoor fixture enables the shadowing capability; the other two do not.

[`PopulateMixedExposureBenchmarkScene`](../../src/Oxygen/Vortex/Test/Fixtures/ExposureBenchmarkScene.h)
owns procedural asset identities, five opaque/emissive/masked/translucent/ground
surfaces, lights and nonzero atmosphere/fog. I01/I02 add the existing enclosure.
[`ExposureBaselineRendering.cpp`](../../src/Oxygen/Vortex/Test/Exposure/Benchmarks/ExposureBaselineRendering.cpp)
owns the 1,200-frame path: 300 exterior hold, 300 smoothstep entry, 300 interior
hold, 300 smoothstep exit; secondary phase is +600. Its warmup already enforces
300 frames/10 seconds (whole 1,200-frame cycles for I01/I02). Asset readiness,
draws, actual formats, shadows and temporal history are checked by that harness.
GPU JSON, CPU CSV and the run manifest are emitted once; untimed endpoint images
and placement snapshots are separate from sampled frames. Existing
[native checkpoint](../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/native-benchmark-checkpoint-manifest.json)
and [H5 checkpoint](../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/h5-closeout-manifest.json)
remain prior evidence, not new runs or final acceptance.

Append the same bounded event schedule to the three 1080p I02 runs: seed/cut,
Manual/Auto switch, brightness step, sharing/source loss, resize, remove/re-add
and delayed acknowledgement. Record startup and event windows separately from
steady samples. Qualify warm-transition cost against its matched steady window.
The fixed event script is owned by the
[PostProcessService event inventory](lld/post-process-service.md#slice-51-event-operation-inventory).
It maps public operations and the existing test-only acknowledgement-delay seam;
wiring that script into the existing I02 harness and executing it belong to 13.
Memory expectations are in the
[SceneTextures inventory](lld/scene-textures.md#slice-51-workload-memory-inventory).
Verify normal 60 Hz presentation once through an
existing presented-output path. Do not rerun this matrix at intermediate items.

#### Tasks and exit evidence

| ID | Work item / owner | Status | Depends on | Delivery and exit evidence |
| --- | --- | --- | --- | --- |
| EX051-01 | Acceptance contract / rendering owner | validated | User approval | Hardware, invariants, budgets and eight recipes are frozen in the [baseline manifest](../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/baseline-manifest.json). |
| EX051-02 | Native profiling / Graphics + Diagnostics | validated | 01 | Timestamp retention, coverage, bounded export and R085 are qualified in the [profiling closeout](../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/profiling-closeout-manifest.json). Instrumentation overhead is accepted and frozen. |
| EX051-03 | Recipe, event and memory inventory / existing workload owners | validated | 01 | Documentation-only source inventory complete: eight entry-point/settings mappings and prior workload evidence above; public event operations and fixed script in PostProcessService; live/cache/lease/scratch expectations and existing lifecycle evidence in SceneTextures. Verified against current source and existing reports; no build or native run required/performed. Event-script wiring/execution and final repeated timings belong to 13. |
| EX051-04 | FP32-only baseline / PostProcess + Environment | validated | 02, 10A | Runtime FP32/P1, admission/certificate bypass, omitted unused reports and precision-only mode invalidation implemented; normal exposure/range/events/sharing/history remain active. Debug: 109 latest unique checks pass; Release: four catalog plus nine native controls pass. Both 234-module shader archives and Release-only benchmark paths compile. Existing `fp32` remains format-only. [Commands, frozen hashes and preserved test-observation correction](../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/fp32-only/checkpoint-manifest.json). D01-D04 measurements belong to 05. |
| EX051-05 | Format-benefit decision / rendering owner | validated | 03, 04 | D01-D04 complete in one Release binary: eight timed runs/60,000 frames, ten passing endpoint pairs, zero steady creation churn, 648 unchanged frozen identities. No repeats needed. Every dynamic-production pair is slower; C01 alone saves memory. Reject a frame-time benefit, record the C01 memory tradeoff, and propose FP32 production for the required 09 agreement. [Decision table](#ex051-05-results-and-decision), [raw evidence/checkpoint](../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/decision05/checkpoint-manifest.json). Final budgets remain 13. |
| EX051-06 | Bound-publication reduction experiment / shader owners | validated | 05 checkpoint | H3 was tested, rejected and reverted. [Closeout](../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/h3-closeout-manifest.json) preserves the native comparison, DXIL, numerical checks and restored scalar regression. No active follow-up experiment. |
| EX051-07 | Maximum reuse and scan fusion / ExposurePass + producers | validated | 05 checkpoint | H1 (`73ced156`) and H2 (`efecd6185`) are retained with owning Debug, Release comparisons, DXIL and image checks: [H1](../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/h1-closeout-manifest.json), [H2](../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/h2-closeout-manifest.json). A new scan hypothesis requires a new 05 decision; final matrix coverage belongs to 13. |
| EX051-08 | Standalone fog optimization | superseded | — | Removed from active delivery. Exposure-related temporal precision and error propagation belong to 09. General fog optimization is outside this slice. |
| EX051-09 | Precision operating policy / PostProcess + Environment | validated | 05 | User approved the [state/decision and validity table](lld/post-process-service.md#agreed-ex051-09-precision-policy) and C01 memory tradeoff on 2026-09-20. FP32 production, explicit `qualified` diagnostics and control-revision acknowledgement rejection implemented. Numerical fixtures explicitly select certified behavior without renaming their identities. 122/122 focused Debug checks pass, including both control-switch acknowledgement directions, qualification failures, source/layout/stale-status cases and queued readers. [Checkpoint](../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/production-policy/checkpoint-manifest.json). No automatic production admission/retry or tolerance change; integrated owning gates and final acceptance remain 12/13. |
| EX051-10 | Copy/reuse/retirement correction / SceneTextures + Graphics | validated | Measured checkpoints | H4 (`20e8968f`), H5 (`28e0f1ee`) and R091/accounting (`8903305`) are committed. H5 removes measured steady churn; R091 passes seven focused Debug cases. The two Release lifecycle matrices pass eight cases each with identical same-control peaks, P/state trajectories and retired populations. Evidence: [H4](../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/h4-closeout-manifest.json), [H5](../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/h5-closeout-manifest.json), [lifecycle protocol](../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/lifecycle-memory/protocol-shared-idle-fix.json), [matched 1080 production](../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/lifecycle-memory/audit-matched-1920-production-Release.json), [1080 FP32](../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/lifecycle-memory/audit-matched-1920-fp32-Release.json), [4K production](../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/lifecycle-memory/audit-matched-3840-production-Release.json), [4K FP32](../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/lifecycle-memory/audit-matched-3840-fp32-Release.json). |
| EX051-10A | Independent SceneColor fallback ownership / SceneTextures | validated | 10 | Independent color allocation/consumer leases and attachment-frame retirement implemented. Framebuffer bindings retain resources separately from immutable reader leases; both pool reuse paths preserve readers/fences. Final Debug batch: 56/56, including existing queued color/depth cases and 4K temporal-off lifecycle. All three retained-resize cycles preserve warmed attachment identities/populations; retired leases are zero. Engine peak 3747.207 MiB (322.5 MiB below the superseded draft); no matched-Release benefit claim. [Checkpoint, exact commands, hashes and preserved diagnosis](../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/color-ownership/checkpoint-manifest.json). Item exit passes; Release format-benefit decisions belong to 05 and final acceptance to 13. |
| EX051-11 | Active CPU attribution / PostProcess + Graphics callers | in_progress | 05, 09 | Original and detailed frozen traces each pass Debug smoke 1/1 and Release I02 1/1, with 6,000 samples and 1,005 unchanged inputs. Detailed elapsed union is 0.762/0.902 ms p95/p99; eight submissions/frame, zero explicit fence waits inside exposure. Acquisition/finalization/binding account for 59% of aggregate elapsed time. Active-only budget remains unproven. User approved the specific 11A/11B candidates below; no general submission-system rewrite. [Owner contract](lld/post-process-service.md#ex051-11-cpu-attribution-checkpoint), [CPU checkpoint](../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/cpu-attribution/checkpoint-manifest.json). |
| EX051-11A | Adjacent exposure recorder ownership / PostProcess + ExposurePass | validated | 11 attribution | Final range and histogram/solve share one successful submission; reused same-frame solves retain an independent current range check. Owning Debug: 228 native + 88 CPU pass (latest unique outcomes); two diagnostic/lifecycle fixture failures diagnosed, corrected and preserved. Matched Release I02: all 6,000 frames use six submissions instead of eight; elapsed CPU p95/p99 0.762/0.902 -> 0.646/0.760 ms, all five cycle p95 ranges disjoint. GPU phase counts, memory and zero steady churn preserved; four endpoint pairs pass. All 1,011 frozen inputs unchanged. Accept candidate; active-only CPU/global budgets remain open. [Decision](../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/cpu-attribution/recorder/decision-table.json), [checkpoint](../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/cpu-attribution/checkpoint-manifest.json). |
| EX051-11B | Exposure constant and descriptor publication / ExposurePass + existing upload owner | planned | 11A decision | Attribute publication with existing profiling; evaluate publishing identical histogram constants once for clear and accumulation. Preserve immutable payloads and rebind the same record for each dispatch. Broader reuse requires measured cost and explicit reader/fence ownership; no assumed descriptor bottleneck, frame-slot-only overwrite or renderer-wide allocator redesign. Focused histogram/mask/transition/two-view/retained-reader checks, owning exposure gate and matched Release CPU comparison. Stop the candidate without a performance claim if no benefit is demonstrated. |
| EX051-12 | Integrated correctness / existing owners | in_progress | 04, 09, 10A, 11 | Map existing numerical, range, mask, history, two-view, rejection and delayed-lease cases to the changed contracts. Run the owning correctness gates at final integration. Add only a named missing case; retain accepted checkpoint evidence for unchanged behavior. |
| EX051-13 | Final performance acceptance / rendering owner | planned | 12 | Execute the 16 production cells/48 runs and attached event windows defined above, plus normal presentation. Verify GPU/CPU distributions, scaling, transition latency, memory and output. This is the sole final performance campaign. |
| EX051-14 | Owner and evidence closeout | planned | 13 | Reconcile owner docs, item decisions, implementation commits and the evidence index. Record the Slice 5.2 handoff. No new implementation or benchmark programme. |
| EX051-GATE | Slice acceptance | planned | 12, 13, 14 | Evaluate the existing acceptance contract and completed evidence. Then proceed to Slice 5.2; original Slices 6-10 remain required. |

#### Checkpoint and evidence rules

1. Use focused Debug checks for a changed contract and the broader owning Debug
   gate at item closure. The final slice receives the owning Release correctness
   gate. An unchanged accepted checkpoint does not trigger another run.
2. Each new production hypothesis names its change, expected observable result,
   smallest check and stopping condition in 05 before implementation. Keep coupled
   C++/HLSL layouts and callers in one independently buildable commit.
3. Commit an accepted correction or a rejected experiment's retained regression
   and decision before starting another correction. Global acceptance remains in
   13/GATE; it does not reopen completed implementation items.
4. Freeze source/runtime inputs during native collection. Use one runtime control
   selector for D01-D04 and no competing GPU work. Instrumentation stays frozen;
   no new overhead campaign is scheduled.
5. Store each raw result once. Analyses contain derived tables and references to
   input paths/hashes. One checkpoint manifest links commands, revisions, raw
   results and the decision. Do not embed raw native payloads again in audits or
   summaries. Preserve existing evidence and failed-run records.

### 3.2.2 Slice 5.2 code quality and test structure

**Slice status: planned; plan approved on 2026-09-19.** Execution follows
EX051-GATE. The later EX052-03 discussion covers the actual diagnostic inventory
and concrete restructuring design before quality edits.
IDs EX052-* belong to Slice 5.2. This slice changes code quality and ownership
structure while preserving the final Slice 5.1 behavior, ABI and measured costs.
No Slice 6 feature work is included.

#### Scope and established tools

Start from the exact Slice 5/5.1 change manifest: exposure/PostProcess,
SceneRenderer/SceneTextures and relevant Environment, Graphics, Data/cooker,
example, test and analysis-tool changes. Follow actual callers and shared
fixtures, but exclude unrelated engine-wide cleanup. HLSL and Python quality
need appropriate review; clang-tidy does not validate them.

Use tools/cli/oxytidy.ps1 (oxytidy.py), the real compile database and repository
.clangd adjustments. The wrapper is analysis-only; review and apply fixes in
bounded batches. Include test translation units explicitly. Preserve the parent
.clang-tidy and Test/.clang-tidy policies; inventory existing exclusions rather
than treating suppressed checks as a clean result. Record compiler/tool versions,
scope, effective checks, translation-unit count, diagnostic count and parse
failures. No compiled input, skipped unit or external parse failure may silently
become a passing analysis result.

Fix warnings at their cause. New or retained NOLINT/check exclusions need the
exact diagnostic, location, reason the code must remain, alternatives considered
and a narrow documented scope. Do not disable a warning family, broaden filters,
cast away a real problem or add blanket suppressions to obtain a zero count.
Existing justified test-value exceptions can remain with recorded rationale.
Clang-tidy suggestions that affect semantics require a correctness review.

The initial read-only inventory counted 14,095 lines/211 declared tests in
ExposureGpu_test.cpp, including four opt-in timing/allocation tests. These are
historical baseline counts. On 2026-09-20 the user explicitly advanced the
exposure-test decomposition: the frozen inventory is 228 tests (220 ordinary
and eight opt-in), preserved under `Test/Exposure/`. This does not
authorize unrelated Slice 5.2 cleanup.
PostProcessService, deferred-core and offscreen suites also need scoped review.
Use responsibility boundaries, not arbitrary file-size or file-count quotas.

Proposed native-suite structure to agree in EX052-03:

- Shared fixture/resource setup owns backend, renderer, upload/readback,
  frame/queue and capture lifetime once; explicit per-test reset remains.
- Independent numeric references remain distinct from production algorithms.
- Coherent groups cover metering/adaptation; lifecycle/sharing; producer domains;
  precision/conversion/recovery/queued consumers; and scene/offscreen/composition.
- Profiling/allocation workloads retain their explicit opt-in execution.
- Prefer multiple translation units under the existing test target initially.
  Introduce separate executables only for demonstrated ownership/runtime benefit.
  Preserve filter names, discovery, capture scripts and isolation, or provide
  an explicit reviewed old-to-new test identity map.
- Treat /bigobj as a compiler capacity option, not a hidden warning. Reassess it
  after splitting; retain or remove it based on actual supported-build evidence.

#### Tasks, dependencies and proof

Evidence belongs in slice52 under the existing analysis root. Preserve the
Slice 5.1 source/runtime/performance baseline for non-regression comparisons.

| ID | Work item / owner | Status | Depends on | Required delivery and exit evidence |
| --- | --- | --- | --- | --- |
| EX052-01 | Freeze quality scope and baseline / rendering + test owners | planned | EX051-GATE | Record exact changed/related files, API/ABI contracts, all discovered test identities/parameters, enabled/opt-in counts and native performance baseline. Identify unrelated files explicitly; no implementation edits yet. |
| EX052-02 | Actual diagnostics inventory / C++ owners | planned | 01 | Run scoped oxytidy with current compilation inputs and tests included. Classify actionable correctness/lifetime, performance, API/style and external-tool issues. Inventory existing suppressions and uncovered headers/TUs. Preserve raw logs and a unique diagnostic ledger; do not infer warning counts from file size. |
| EX052-03 | Agree restructuring design / user + rendering/test owners | planned | 01, 02 | Present fixture ownership, proposed file/target groups, independent oracle boundary, test identity mapping, intended runtime simplifications and suppression decisions. Obtain the user's agreement before any quality edit. Keep changes behavior-preserving and avoid a new generic test/exposure framework. |
| EX052-04 | Correctness and lifetime diagnostics / C++ owners | planned | 03 | Fix verified clang-tidy defects in coherent owner batches. Add a meaningful regression for real behavior defects; if a fix changes the accepted contract, reopen the relevant owner decision. Keep mechanical changes separate from algorithm fixes. Run focused Debug checks per batch. |
| EX052-05 | Extract shared native fixture ownership / native test owner | planned | 03, 04 | Separate backend/resource/frame/capture helpers with explicit ownership and reset. Keep one source of fixture behavior, deterministic cleanup and no mutable process-global state. Prove old tests still discover and pass before moving scenario groups. |
| EX052-06 | Split scenario translation units / native test owner | planned | 05 | Move the agreed coherent groups, preserving test/filter identity and data vectors. Wire every TU into CMake/discovery. Keep independent references independent, preserve both renderer paths and all opt-in tests. Verify no dropped, duplicated, disabled or newly order-dependent coverage. |
| EX052-07 | Remaining owner/test/tool structure / relevant owners | planned | 03, 06 | Apply agreed bounded simplifications to PostProcess/ExposurePass, deferred/offscreen fixtures and touched evidence tooling. Remove duplication only where ownership/contracts match; keep C++/HLSL layout and state transitions explicit. Audit oracle self-validation and malformed/empty report handling. |
| EX052-08 | Readability and remaining tidy fixes / C++ owners | planned | 04-07 | Resolve remaining actionable diagnostics; make names, helpers, constness and interfaces clear without burying domain values behind meaningless constants. Review every retained narrow suppression. Do not expand .clang-tidy exclusions to conceal unresolved diagnostics. |
| EX052-09 | Build/discovery/tooling integrity / build + test owners | planned | 06-08 | Regenerate actual build/discovery inputs and confirm existing scripts and test filters work. Reassess /bigobj with real MSVC builds. Inventory before/after identities including disabled/parameterized tests and shader probes; update wrappers only when needed. No arbitrary suite merges/splits or duplicated shader builds. |
| EX052-10 | Final diagnostic and owning correctness gates / test owners | planned | 09 | Rerun scoped tidy and relevant compiler checks on final code: zero unresolved actionable in-scope diagnostics, only explicitly justified narrow exceptions, no hidden parse failures. Run broader owning Debug suites, then owning Release at slice closure. Require explicit coverage parity and no newly suppressed/skipped cases. |
| EX052-11 | Performance and visual non-regression / rendering owner | planned | 10 | Reuse the frozen native Release workloads and valid profiling protocol. Every 5.1 absolute budget still passes; investigate p95 cost growth exceeding max(0.05 ms, 5% of baseline), or a material p99/memory regression beyond repeatability. Inspect representative native output and relevant lifecycle captures when affected. Do not redo unrelated captures without a reason. |
| EX052-12 | Final owner/status reconciliation and commit / rendering owner | planned | 11 | Record file/fixture ownership, test identity map, exact diagnostic disposition, suppression rationale, test/performance evidence and final hashes. Update all affected rows and operating commands. Commit the complete quality increment before any authorized Slice 6 work. |
| EX052-GATE | Code-quality acceptance | planned | 01-12 | Agreed restructuring delivered; actionable tidy/compiler issues fixed, exceptions justified, tests/callers/ABI and independent oracles preserved, Debug/Release gates pass, Slice 5.1 performance retained and documents current. No quality or scope gap may be relabeled complete to start Slice 6. |

Commit boundaries: actionable diagnostic fixes by owner; usable shared fixtures
with their migrated callers; each coherent scenario group plus CMake/discovery;
then final owner/tooling closure. Avoid scaffolding-only or arbitrary file-count
commits. Each commit must build and preserve test discovery. Focused Debug checks
cover intermediate moves; owning Debug closes each coherent item. Final Release
and performance non-regression close the slice. Do not combine these mechanical
commits with new performance algorithms or Slice 6 functionality.

### 3.3 Slice 6 work items

**Slice status: planned. Depends on EX051-GATE and EX052-GATE.
Persistence/authoring integration has not started.**
Approved contracts are requirements, not completed implementation.

| ID | Work item | Status | Exact remaining delivery |
| --- | --- | --- | --- |
| EX06-01 | Native physical-camera persistence | planned | Source/cook/load aperture, shutter and ISO; scene-v6 perspective/orthographic 32/40-byte records; v5 20/28-byte defaults f/11, 125/s, ISO100. Keep the existing editor UI. |
| EX06-02 | Exposure source schemas and component/config parity | planned | Carry all authored exposure fields through JSON schemas and native component/config boundaries; existing canonical runtime types are a prerequisite, not completion of this integration. |
| EX06-03 | Versioned packed exposure record | planned | Implement the approved 144-byte prefix, mask resource index at 116, reserved 120–131, curve count at 132 and keys at 144; preserve enum ordinals/defaults. |
| EX06-04 | Cooker, package remapping and loader | planned | Texture path to source-local index, PAK index remapping and runtime ResourceKey hydration; curve and scalar cook/load support. |
| EX06-05 | Scripting and existing editor/native adapters | planned | Round-trip the same canonical fields and physical-camera values through existing adapters; no new physical-camera editor controls. |
| EX06-06 | Old/new source-cook-load-save/reload tests | planned | Verify every field, mask, curve, black influence and D, resolved-setting equality, old-record defaults and malformed boundaries. |
| EX06-07 | Runtime asynchronous mask acceptance prerequisite | validated | Pending/resident/failed masks and atomic accepted revisions already exist from runtime work, including direct SetConfig loading. Persistence round-trip remains EX06-04/06. [Evidence](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/review-r028-manifest.json). |
| EX06-08 | DemoShell controls and status | planned | Correct controls/labels; expose requested versus effective settings and resource/metering failures. |
| EX06-09 | Experiment-owned activation policy | planned | Prevent saved camera/scene/post-process reapplication from overriding a LightBench recipe while preserving other demos' behavior and personal settings. |
| EX06-GATE | Authoring/persistence/isolation gate | planned | Identical resolved settings across the full round-trip, deterministic legacy loading and no personal-settings mutation during batch execution. |

### 3.4 Slice 7 work items

**Slice status: planned. Calibration implementation has not started.**

| ID | Work item | Status | Exact remaining delivery |
| --- | --- | --- | --- |
| EX07-01 | Directional reference calibration | planned | Verify the white directional reference against the actual production BRDF/material and exposure equations. |
| EX07-02 | Point units and distance behavior | planned | Flux/(4*pi), inverse-square attenuation, smooth range fade, 0.001 m numerical floor and zero-separation handling. |
| EX07-03 | Spot normalization | planned | Smooth-cone solid-angle normalization, hard-cone limit, zero-angle rejection and angular/range boundary tests. |
| EX07-04 | Shared forward/deferred consumers | planned | Apply the same light-unit helpers and receiver-cosine convention to both active rendering families. |
| EX07-05 | Production material/color-space oracle | planned | Freeze packed albedo, normal, dielectric specular and color-space interpretation; use an independent reference calculation rather than treating roughness as Lambertian. |
| EX07-06 | Numerical integration and calibration tests | planned | Independently verify flux integration, distance/cone boundaries, known fade and all three required light experiments. |
| EX07-GATE | Complete lighting unit chain | planned | Passing directional/point/spot expectations and unit/BRDF contracts; no deferred calibration dependency. |

### 3.5 Slice 8 work items

**Slice status: planned. Reusable benchmark instrumentation has not started.**
Existing test readbacks/RenderDoc analyzers are not the delivered measurement API.

| ID | Work item | Status | Exact remaining delivery |
| --- | --- | --- | --- |
| EX08-01 | Region statistics and identity | planned | Diagnostics/extraction results with frame, view-state, source product/domain, region, validity and sample counts. |
| EX08-02 | Actual consumed exposure/output probe | planned | Recover scene values with stored P and inspect the gain consumed by that frame; opt-in GPU probe at the production operation; label CPU-derived quantities explicitly. |
| EX08-03 | Invalid and contaminated regions | planned | Distinguish zero from late, occluded, insufficient, edge-contaminated, partial-coverage and nonfinite measurements. |
| EX08-04 | Async association and lifetime | planned | Bounded readback/resources, fence-safe ownership and rejection of stale frame/view/experiment revisions. |
| EX08-05 | Disabled-path cost | planned | Verify zero measurement dispatches/readback allocations when disabled and bounded enabled-path cost. |
| EX08-06 | Independent instrument qualification | planned | Known GPU signals, P conversion, actual gain, zero/coverage/invalid cases and delayed-readback changes. |
| EX08-GATE | Instrument qualification before bench verdicts | planned | Instruments must match independent inputs before LightBench uses them to judge rendering. |

### 3.6 Slice 9 work items

**Slice status: planned. LightBench repair and experiment implementation have not
started. Some MultiView prerequisites were delivered in Slice 5, as identified.**

| ID | Work item | Status | Exact remaining delivery |
| --- | --- | --- | --- |
| EX09-01 | One versioned experiment schema/controller | planned | Shared validated definitions and one execution path for interactive and batch use. |
| EX09-02 | Complete staged application and reset | planned | Own geometry, materials, lights, environment, camera, features, exposure/output, measurement regions and temporal transition; apply coherently at a frame boundary. |
| EX09-03 | Neutral Reference experiment | planned | Default startup; three readable framed cards, visible-side camera/key, real white directional light and independently selected material/exposure/output reference. |
| EX09-04 | Fixed Exposure experiment | planned | Known HDR input and EV/key/compensation sweeps through uploaded and consumed gain. |
| EX09-05 | Adaptation experiment | planned | Both luminance-step directions, controlled dt, masks and compensation curves. |
| EX09-06 | Lifecycle experiment | planned | Startup, seeds, cuts, mode changes, pause, sharing, stateless views and recovery. |
| EX09-07 | Point Falloff experiment | planned | Prescribed receiver/distances, visible geometry relationship and independent inverse-square expectations. |
| EX09-08 | Spot Distribution experiment | planned | Aimed receiver, cone overlay, angular response and integrated flux expectations. |
| EX09-09 | HDR Domain experiment | planned | Bright/dark endpoints and mixed opaque/forward/translucent/sky/fog content under varying P. |
| EX09-10 | Focused controls and overlays | planned | Relevant controls, expected/measured values, effective exposure, Pass/Fail/Modified/Invalid states and visible debug overrides; collapsed advanced controls. |
| EX09-11 | Saved experiment migration/loading | planned | Version the indoor preset, load modified experiments explicitly, separate window/panel preferences and preserve personal files. |
| EX09-12 | LightBench native visual acceptance | planned | Inspect all seven experiments, startup, transitions and reset at 1080p, 1440p and resized dimensions; aligned measurement regions and reproducible presented captures. |
| EX09-13 | MultiView operational integration | in_progress | Existing proof CLI, framing fixes and scenario fixtures are prerequisites from Slice 5. Complete controls/measurement integration, combined interactions and all layout/pane acceptance remain. [Current README](../../Examples/MultiView/README.md). |
| EX09-14 | LightBench operating README | planned | `Examples/LightBench/README.md` does not yet exist; write actual launch/run/reset/save instructions, supported tests and interpretation after implementation. |
| EX09-15 | MultiView operating README closure | in_progress | Current README documents proof modes and intentional black cells. Reconcile it with the completed matrix, shared latency, measurement results and final repeatable commands. |
| EX09-GATE | Interactive benchmark and MultiView visual gate | planned | Complete reproducible LightBench and all MultiView layouts/interactions; inspect native presented output of both applications. |

### 3.7 Slice 10 work items

**Slice status: planned. Package-level automation and final acceptance have not
started. Individual Slice 5 capture/assertion scripts are prerequisites only.**

| ID | Work item | Status | Exact remaining delivery |
| --- | --- | --- | --- |
| EX10-01 | Deterministic batch execution | planned | Select/run the same versioned experiments and controller as interactive mode using existing capture CLI. |
| EX10-02 | LightBench runner and report schema | planned | `tools/vortex/Run-LightBenchValidation.ps1` does not yet exist; implement it and a schema-validated result report. |
| EX10-03 | Native game-facing/lifecycle cases | planned | Integrate production callers without DemoShell, scene lifecycle, sharing and numerical recovery into the runner. |
| EX10-04 | Extend the existing MultiView runner | planned | Extend `Run-VortexMultiViewValidation.ps1`, existing analyzer/assertions and result schema; retain structural checks and add exposure/image/interaction acceptance. |
| EX10-05 | Reuse current per-view proof tools | in_progress | Slice 5 has isolated/reordered/shared, source-loss, resize, lifetime, mode and diagnostic capture/assertion tools. Their integration into the final runner/report and remaining cases are unfinished. |
| EX10-06 | Complete report provenance | planned | Record resolved parameters, experiment/version/revision, build/shader identity, device/backend, actual dt, frame/view validity, tolerances, measurements and captures. |
| EX10-07 | Entire acceptance matrix | planned | Run every case in section 9, including native presented-image inspection; required failed/unsupported cases block completion. |
| EX10-08 | Final owner-document reconciliation | planned | Reconcile equations/contracts, implementation/operating docs and this tracker against final evidence. |
| EX10-GATE | Full package completion | planned | Every required feature, experiment, command and acceptance gate passes. The overall goal remains active until this is proven. |

### 3.8 Requirement coverage and update discipline

The item IDs are stable work items, not commit-history entries. Update their rows
in place. When an item is reopened, state the defect and remaining validation
there; after correction, replace the stale limitation with the current result.
Record completed code and its proof together. If work stops before validation,
leave the item `in_progress` and name the missing check. Update section 3.1 before
moving to another item so the user can see what is being worked on without
reading tool logs or reconstructing Git history.

| Plan section 8 requirements | Tracking items |
| --- | --- |
| Slice 5: early P; full 4.4 migration; S/P; formats; range/recovery; scene lifecycle; MultiView; gate | EX05-01; EX05-04–09/18; EX05-03; EX05-02/17; EX05-10–16/19–21; EX05-22–25; EX05-26–30; EX05-GATE |
| Slice 5.1: native profiling; workloads/FP32-only baseline; format decision; precision policy; SceneColor ownership; final acceptance | EX051-01–07; EX051-09/10/10A/11; EX051-12–14; EX051-GATE; EX051-08 merged into 09 |
| Slice 5.2: diagnostic inventory/design; fixes; fixture/suite structure; coverage/tidy/build/performance parity; closure | EX052-01–03; EX052-04–09; EX052-10–12; EX052-GATE |
| Slice 6: camera persistence; authoring surfaces; full round-trip; mask runtime; DemoShell; activation policy; gate | EX06-01; EX06-02–05; EX06-06; EX06-07; EX06-08; EX06-09; EX06-GATE |
| Slice 7: point/spot; shared consumers; independent calibration; material mapping; gate | EX07-02–03; EX07-04; EX07-01/06; EX07-05; EX07-GATE |
| Slice 8: diagnostics; known inputs; identity; disabled/enabled cost; gate | EX08-01–03; EX08-06; EX08-04; EX08-05; EX08-GATE |
| Slice 9: definitions; ownership/reset; UI; seven visual experiments; preset/loading; MultiView; both READMEs; gate | EX09-01; EX09-02; EX09-10; EX09-03–09/12; EX09-11; EX09-13; EX09-14–15; EX09-GATE |
| Slice 10: batch; LightBench runner; MultiView extension; provenance; full acceptance/docs; gate | EX10-01; EX10-02–03; EX10-04–05; EX10-06; EX10-07–08; EX10-GATE |

### Related editor package

**Implementation package:** `ED-M08 — V0.1 canonical authoring and rendering`.
**Status:** `planned`; design package ready, implementation and rendered gates open.

The [cross-engine/editor plan](../../../../design/editor/plan/ED-M08-runtime-parity-and-standalone-validation.md)
defines native implementation and native visual validation before editor work.
The engine [rendering contract](plan/editor-v01-rendering-contract.md) and
[captured-sky IBL contract](plan/editor-v01-captured-sky-ibl.md) define this
extension. The editor ledger owns its slice-by-slice progress; this ledger
records the native capability boundary without duplicating its schedule.

VTX-M08 remains validated at its static diffuse-only scope. ED-M08 requires new
proof for its formats, directional/visibility/shadow behavior, captured sky and
specular IBL, camera framing, grading, primitives and material contract.

**Latest closed plan:** `design/vortex/plan/VTX-M08-skybox-static-skylight.md`.
Validated M08 LLD references are `design/vortex/lld/cubemap-processing.md` and
`design/vortex/lld/skybox-static-skylight.md`.

## 4. Milestone Ledger

| ID | Milestone | Status | Current Evidence | Missing To Close |
| --- | --- | --- | --- | --- |
| Exposure / LightBench / MultiView | Complete global exposure and benchmark package | `in_progress` | [Current work, slice status and item-level evidence](#3-exposure-delivery-status). | All open items and gates in section 3; no independent completion claim in this summary row. |
| ED-M08 native extension | V0.1 canonical authoring and rendering | `planned` | Final rendering/IBL contracts, source-local deferred annotations and editor execution plan. | Native implementation, focused tests and visual proof outside the editor, followed by integrated editor qualification. |
| VTX-M00 | Planning and status truth surface | `validated` | `PLAN.md` was rewritten as a milestone-first plan; this milestone/status ledger exists; restricted doc scans and `git diff --check` passed on 2026-04-25. | No open planning-status gap. |
| VTX-M01 | Renderer Core and SceneRenderer baseline | `validated` | Vortex module, Renderer Core, publication, upload/resource substrate, SceneRenderer shell, SceneTextures, non-runtime facades, resolve/cleanup, and related tests are present and freshly validated. Build proof passed `cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.LinkTest Oxygen.Vortex.RendererCapability.Tests Oxygen.Vortex.RenderContext.Tests Oxygen.Vortex.SceneRendererShell.Tests Oxygen.Vortex.SceneTextures.Tests Oxygen.Vortex.SceneRendererPublication.Tests Oxygen.Vortex.RenderContextMaterializer.Tests Oxygen.Vortex.RendererFacadePresets.Tests Oxygen.Vortex.RenderGraphHarnessFacade.Tests Oxygen.Vortex.SinglePassHarnessFacade.Tests Oxygen.Vortex.UploadCoordinator.Tests Oxygen.Vortex.ViewConstantsManager.Tests oxygen-examples-vortexbasic --parallel 4`. Focused CTest passed the corresponding Vortex substrate/facade suites plus `Oxygen.Vortex.LinkTest`. Runtime proof `tools\vortex\Run-VortexBasicRuntimeValidation.ps1 -Output out\build-ninja\analysis\vortex\m01-m03-closeout\vortexbasic-foundation -Frame 3 -RunFrames 6 -Fps 10 -BuildJobs 4` passed overall with runtime exit 0, final present nonzero, CDB/debug-layer `overall_verdict=pass`, no D3D12/DXGI errors, and no blocking warnings. | No open VTX-M01 closure gap. |
| VTX-M02 | Deferred core visual path | `validated` | InitViews, depth prepass, generic Screen HZB, base pass/GBuffer/velocity, Stage 10 publication, deferred lighting, shader families, debug views, and focused tests/tools are present and freshly validated. Build proof passed `Oxygen.Vortex.SceneRendererPublication.Tests`, `Oxygen.Vortex.SceneRendererDeferredCore.Tests`, `oxygen-graphics-direct3d12_shaders`, `Oxygen.Graphics.Direct3D12.ShaderBakeCatalog.Tests`, and `oxygen-examples-vortexbasic`. CTest passed `Oxygen.Vortex.SceneRendererPublication.Tests`, `Oxygen.Vortex.SceneRendererDeferredCore.Tests`, and `Oxygen.Graphics.Direct3D12.ShaderBakeCatalog.Tests`. Runtime proof `vortexbasic-foundation.validation.txt` passed with Stage 3 depth scope/draw/clear/copy counts, Stage 5 Screen HZB scope, Stage 9 base-pass scope/draw counts, Stage 9 GBuffer/base-color/velocity nonzero proof, Stage 12 scope/directional/point/spot draw counts, and phase-stage order checks. | No open VTX-M02 closure gap. |
| VTX-M03 | Migration-critical non-environment services | `validated` | LightingService, ShadowService baseline, PostProcessService, Stage 8/12/22 routing, and focused tests are present and freshly validated. Build proof passed `Oxygen.Vortex.LightingService.Tests`, `Oxygen.Vortex.ShadowService.Tests`, `Oxygen.Vortex.PostProcessService.Tests`, `Oxygen.Vortex.SceneRendererDeferredCore.Tests`, shader bake/catalog targets, and `oxygen-examples-vortexbasic`. CTest passed `Oxygen.Vortex.LightingService.Tests`, `Oxygen.Vortex.ShadowService.Tests`, `Oxygen.Vortex.PostProcessService.Tests`, `Oxygen.Vortex.SceneRendererDeferredCore.Tests`, and `Oxygen.Graphics.Direct3D12.ShaderBakeCatalog.Tests`. Runtime proof `vortexbasic-foundation.validation.txt` passed with Stage 12 deferred-lighting scope, directional/point/spot draw-count proof, nonzero Stage 12 SceneColor for directional/point/spot lighting, compositing present operation count, final present nonzero, CDB/debug-layer `overall_verdict=pass`, no D3D12/DXGI errors, and no blocking warnings. Later milestones validate expanded conventional shadows, diagnostics, translucency, environment, and composition behavior. | No open VTX-M03 closure gap. |
| VTX-M04D | Environment / fog parity closure | `validated` | VTX-M04D.1 through VTX-M04D.6 validated environment publication truth, height fog, local fog volumes, volumetric fog, consolidated environment runtime proof, and main-view aerial perspective against the UE5.7 source/shader families recorded in the M04D plans. Evidence includes focused Vortex builds/tests, ShaderBake/catalog validation where shaders changed, VortexBasic and RenderScene RenderDoc proof, CDB/D3D12 debug-layer audits where required, and user visual confirmation for projected shadows and city scene shadows. Static specified-cubemap SkyLight products are validated under VTX-M08. | Captured-scene SkyLight, real-time capture, and reflection/360 AP remain deferred to later resource paths. |
| VTX-M04D.1 | Environment publication and sky/fog contract truth | `validated` | EnvironmentLightingService sanitizes IBL probe bindings, distinguishes authored SkyLight from usable IBL resources, keeps invalid SkyLight/volumetric products explicit, exposes Stage 14 local-fog state through SceneRenderer, and focused EnvironmentLightingService/SceneRendererPublication tests passed on 2026-04-25. | Real SkyLight capture/filtering and fog parity are outside VTX-M04D.1 and tracked by later milestones. |
| VTX-M04D.2 | UE5.7 exponential height fog parity | `validated` | CPU/HLSL height-fog payload, authored layer translation, analytic `HeightFogCommon`-shaped line integral, sky-depth exclusion, DemoShell/RenderScene controls, focused build/tests, ShaderBake/catalog validation, focused VortexBasic enabled/disabled RenderDoc proof, and city-scale RenderScene capture proof are recorded. | Cubemap inscattering resource binding/sampling remains explicitly unavailable and deferred. |
| VTX-M04D.3 | UE5.7 local fog volume parity | `validated` | Analytical local-fog volume path is implemented and proven: UE5.7 source mapping, authoring sanitization, sorting/capping, HZB-backed tiled culling, single draw-indirect rendering for the Stage-15 local-fog compose path, analytical shader path, SceneColor contribution, far-depth no-op behavior, focused tests, ShaderBake/catalog validation, VortexBasic runtime/capture proof, and focused RenderDoc draw-args probe. | Local-fog participating-media injection into volumetric fog is validated under VTX-M04D.4, not VTX-M04D.3. |
| VTX-M04D.4 | UE5.7 volumetric fog parity | `validated` | Stage-14 compute volumetric fog allocates and publishes `IntegratedLightScattering`, Stage 15 composes it, and proof covers captured fog payload/SRV/grid state, volume min/max/slices, directional CSM shadowed-light sampling, local-fog participating-media injection, Oxygen distant-SkyLight volumetric ambient, temporal jitter/reset/history-miss reprojection, city-scale `CityEnvironmentValidation`, and D3D12 debug-layer audit. Directional CSM projected-shadow blocker was closed with RenderDoc proof and user visual confirmation. | Accepted Oxygen divergence: single integrated-scattering temporal product without UE conservative-depth history fixup or pre-exposure transfer. Real SkyLight cubemap capture/filtering remains outside this milestone. |
| VTX-M04D.5 | Environment runtime proof and Async preparation | `validated` | `Run-VortexBasicRuntimeValidation.ps1` builds VortexBasic, runs a CDB/D3D12 debug-layer audit, captures RenderDoc frame 5, and asserts one runtime path with atmosphere, main-view AP, height fog, local fog, volumetric fog, authored SkyLight unavailable state, and SkyLight volumetric injection. Focused EnvironmentLightingService tests passed. | Real SkyLight cubemap capture/filtering remains a later IBL/resource gap. Async proof is validated separately by VTX-M04E. |
| VTX-M04D.6 | UE5.7 aerial perspective parity | `validated` for main-view AP | Vortex AP sampling uses camera-volume lookup helpers traced to UE5.7 `SkyAtmosphere` shader behavior, preserves raw camera-volume generation with apply-time strength control, exposes effective DemoShell/VortexBasic AP controls, fixes city-scale reversed-Z far-depth AP composition, and has focused enabled/disabled plus city-scale RenderDoc proof. | Reflection/360-view AP is explicitly deferred to the future reflection-capture resource path. |
| VTX-M04E | Async migration parity gate | `validated` | Async authors a Vortex-ready scene environment, sun directional light, shadow receiver ground, lifted sphere/two-submesh geometry, and Vortex runtime view metadata. DemoShell no longer overrides the scene-authored environment. `Run-AsyncRuntimeValidation.ps1` passed with RenderDoc structural/product proof, final present proof, and overlay composition proof on 2026-04-26. | No open M04E closure gap. |
| VTX-M04F | Single-view composition and presentation closeout | `validated` | Runtime composition registration, queued single-view composition copy, Stage 22 post-process routing, and overlay blend path exist. `AnalyzeRenderDocAsyncProducts.py` proves exactly one post-Stage-22 composition copy from `Async.SceneColor`, exactly one overlay blend after scene copy, final present output, and focused `RendererCompositionQueue` tests passed. | No open M04F closure gap. |
| VTX-M05A | Diagnostics product service | `validated` | Diagnostics runtime service, authoritative shader-debug registry, frame ledger, capture manifest, GPU timeline facade, Diagnostics panel, proof script hardening, direct/masked debug corrections, unsupported IBL/light-culling mode handling, and solid/wireframe/wireframe-overlay render controls are implemented and documented in the LLD/plan. Validation evidence: focused Vortex/DemoShell builds and tests, release DiagnosticsService test, VortexBasic runtime RenderDoc/debug-layer proof, TexturedCube panel-registration smoke, RenderScene build/ShaderBake validation, and user visual confirmation for solid plus wireframe overlay. | No open M05A closure gap. Optional GPU debug primitives remain deferred until a concrete proof gap requires them. |
| VTX-M05B | Occlusion consumer closeout | `validated` | LLD/plan updated; result substrate, HZB tester, base-pass consumers, diagnostics facts, `vtx.occlusion.*` controls, and VortexBasic `--with-occlusion` proof scene/tooling exist. Validation: ShaderBake rebuilt 186 modules; focused `RendererCapability`, `OcclusionModule`, `SceneRendererPublication`, and `SceneRendererDeferredCore` tests passed 62/62; CDB/D3D12 audit report `out/build-ninja/analysis/vortex/occlusion/vortex-occlusion.debug-layer.report.txt` passed with 0 D3D12/DXGI errors; RenderDoc proof `vortex-occlusion.proof.report.txt` shows Stage 3 depth draws 3, Stage 5 occlusion dispatch 1, Stage 9 base-pass scene draws 2, and Stage 20 ground grid absent; user visual confirmation approved. | No open M05B closure gap. |
| VTX-M05C | Translucency stage | `validated` | Stage 18 `TranslucencyModule`/`TranslucencyMeshProcessor`, SceneRenderer wiring, forward unlit-material exposure contract fix, VortexBasic cyan sphere + magenta cylinder proof scene, and RenderDoc/CDB proof tooling are present. Senior-review remediation on 2026-04-26 fixed sparse-bounds sort fallback, invalid draw rejection, projection-kind detection, diagnostics skip reasons/logging, and Stage 18 PSO/root-binding descriptor caching; broader UE-class gaps for per-material sided culling, instanced draw merging, lightweight translucent shading, and material fog/AP controls are documented as deferred scope. UE5.7 re-check covered standard straight-alpha blending and read-only depth state. Validation: focused ShaderBake/catalog tests passed previously; remediation validation passed `cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.SceneRendererDeferredCore --parallel 4`, `ctest --preset test-debug -R "Oxygen\.Vortex\.SceneRendererDeferredCore" --output-on-failure` with 40/40 tests, and `git diff --check`. Fresh VortexBasic translucency proof after remediation passed `cmake --build out\build-ninja --config Debug --target oxygen-vortex oxygen-graphics-direct3d12 oxygen-examples-vortexbasic --parallel 4`, a CDB/debug-layer audit, runtime log inspection, RenderDoc capture, and `Verify-VortexTranslucencyProof.ps1`. Fresh CDB report `out/build-ninja/analysis/vortex/translucency/m05c-review-remediation/vortexbasic-translucency-review-remediation.debug-layer.report.txt` passed with runtime exit 0, no debugger break, 0 D3D12/DXGI errors, and 0 blocking warnings. Fresh RenderDoc report `out/build-ninja/analysis/vortex/translucency/m05c-review-remediation/vortexbasic-translucency-review-remediation_capture.rdc_vortex_translucency_report.txt` proves Stage 18 scope count 1, Stage 18 draw count 2, Stage 9 draw count 2, ground grid absent, Stage 18 after post-opaque and before resolve, cyan pixels 2130, magenta pixels 225, max RGB delta 2684, `stage18_scene_color_changed=true`, `runtime_log_translucency_enabled=true`, and `runtime_log_draw_metadata_count=4`. User visual confirmation approved the final scene. | No open M05C closure gap. |
| VTX-M05D | Conventional shadow parity and local-light expansion | `validated` | Directional CSM UE5.7 audit/remediation is recorded in `shadow-service.md` and `VTX-M05D-conventional-shadow-parity.md`: stable/no-AA frusta, sphere bounds, texel snapping, 5000-unit directional depth extent, UE-style non-last transition overlap coverage, optional CSM constant/slope depth bias, default zero bias, and resolution-hint wiring are present. Release `RenderScene --scene VsmTwoCubes --directional-shadows conventional` smoke/capture/probes validated the local-scale CSM descriptor/settings after the Serio scene-loader fix. Slice E spot-light conventional shadows are implemented and validated with focused tests, shader validation, CDB/debug-layer audit, RenderDoc probe `spot-shadow-validation.bias0.final.spot-shadow-probe.txt`, and user visual confirmation after authored spot bias `0.0`. Slice F point-light conventional shadows are implemented with cube-array storage, six explicit face depth slices, Stage 12 point-shadow consumption, point proxy sphere winding regression coverage, and focused `PointShadowValidation` proof. Validation passed `cmake --build out\build-ninja --target Oxygen.Vortex.SceneRendererDeferredCore.Tests Oxygen.Vortex.ShadowService.Tests oxygen-graphics-direct3d12_shaders --parallel 4`; tests passed SceneRendererDeferredCore `43/43` and ShadowService `9/9`; ShaderBake repacked `186` modules. CDB report `point-shadow-validation.final.debug-layer.report.txt` passed with runtime exit `0`, no debugger break, `0` D3D12/DXGI errors, and `0` blocking warnings. RenderDoc probe `point-shadow-validation.final.point-shadow-probe.txt` proves Stage 8 point draws `168,171,187,190,206,209,225,228,244,247,263,266`, non-clear `Vortex.PointShadowCubeSurface` slices `0`, `2`, and `5`, Stage 12 point draw `343`, Stage 12 point SRV binding, and `SceneColor` contribution. User visual confirmation on 2026-04-27 approved both point-light shell and point-shadow artifact fixes. | No open M05D closure gap. Stage 18 translucent local-light shadow consumption, layered one-pass point cubemap rendering, local-light shadow caching, and VSM remain deferred future work. |
| VTX-M06A | Multi-view proof closeout | `validated` | B1-H implementation exists. B1: per-view `ViewRenderSettings`, producer-owned `ViewStateHandle`, packet-level state-handle copy, packet-owned effective shader-debug mode, handle-keyed `PreviousViewHistoryCache`, ViewStateHandle-keyed exposure state, and removal of `FramePlanBuilder` frame-global render/debug fields. B2: runtime C++ payload types for `ViewKind`, `ViewFeatureMask`, `ViewSurfaceRoute`, `OverlayPolicy`, auxiliary IO descriptors, factory classification, packet copies, and `FramePlanBuilder` view-kind validation. C: `PerViewScope`, no-eager-cursor frame entry materialization, serialized `SceneRenderer::RenderViewFamily`, per-view scene-product reset while reusing the existing `SceneTextures` family, and pre/post per-view binding publication. D: descriptor-keyed `SceneTextureLeasePool`, exclusive per-view leases routed through `RenderViewFamily`, explicit exhaustion, queue-affinity keying, and focused allocation-churn coverage. E: route-aware layer planning, structural full-surface copy selection independent of `kZOrderScene`/primary id, deterministic surface/layer debug names, and filtered surface submissions. F: typed `AuxiliaryDependencyGraph`, runtime publication/materialization of auxiliary descriptors, required producer resolution, producer-before-consumer ordering, extracted color-product publication, and dependent view GPU consumption. G: typed overlay batches, `on_overlay` to `kViewScreen` compatibility conversion, reserved world-depth-aware/world-foreground view overlay placeholders, surface overlay execution before presentable handoff, and typed `IViewExtension` hooks including `OnPostComposition`. H: `Examples/MultiView --proof-layout true`, `--aux-proof-layout true`, CDB/debug-layer audit script, RenderDoc analyzer, assertion script, allow-list, validation schema, and scene-texture lease-pool runtime churn telemetry are implemented. Latest validation passed proof-script parser checks, `AnalyzeRenderDocVortexMultiView.py` py-compile, standard runtime proof `Run-VortexMultiViewValidation.ps1 -Output out\build-ninja\analysis\vortex\m06a-multiview\multiview-proof -Frame 5 -RunFrames 65 -Fps 30 -BuildJobs 4`, auxiliary runtime proof `Run-VortexMultiViewValidation.ps1 -AuxProofLayout -Output out\build-ninja\analysis\vortex\m06a-multiview\multiview-aux-proof -Frame 5 -RunFrames 65 -Fps 30 -BuildJobs 4`, CDB reports with `overall_verdict=pass`, runtime exit `0`, `d3d12_error_count=0`, `dxgi_error_count=0`, and `blocking_warning_count=0`, RenderDoc reports with `overall_verdict=true`, `stage9_scope_count=4`, `composition_view_ids=1,2,3,4`, `aux_consume_scope_count=1`, `aux_consume_copy_count=1`, and `aux_consume_before_composition=true`, allocation reports with `steady_state_frame_count=60` and `steady_state_allocations_after_warmup=0`, focused section 9 build/ctest with 10/10 matched tests passing, and user visual confirmation on 2026-04-28 that GroundGrid stability is corrected. ShaderBake/catalog validation was not run because no shader bytecode, catalog, HLSL ABI, or root-binding files changed. | No open VTX-M06A closure gap. Offscreen-only proof remains VTX-M06B; feature-gated runtime variants remain VTX-M06C. |
| VTX-M06B | Offscreen proof closeout | `validated` | Detailed plan `design/vortex/plan/VTX-M06B-offscreen-proof-closeout.md` exists. Slices A-D landed source/test proof for docs truth, offscreen scene execution, deferred/forward routing, and output product final state. Slice E source commits `f12fcefb9`, `dfc3dab3c`, `5299d6c1c`, `62b4b525f`, and `97ca7fb65` add runtime texture composition layers, embedded offscreen execution, a visually inspectable MultiView offscreen layout, forward-wireframe regression coverage, and solid forward base-pass SceneColor output without deferred GBuffer publication; the offscreen capture proof tile now requests forward solid. Proof tooling `Run-VortexOffscreenValidation.ps1`, `AnalyzeRenderDocVortexOffscreen.py`, and `Assert-VortexOffscreenProof.ps1` runs the closure gate. Latest validation passed `powershell -NoProfile -ExecutionPolicy Bypass -File tools\vortex\Run-VortexOffscreenValidation.ps1 -Output out\build-ninja\analysis\vortex\m06b-offscreen\offscreen-proof -Frame 5 -RunFrames 65 -Fps 30 -BuildJobs 4`: CDB report `overall_verdict=pass`, runtime exit `0`, `d3d12_error_count=0`, `dxgi_error_count=0`, and `blocking_warning_count=0`; RenderDoc report `overall_verdict=true`, `deferred_draw_count=10`, `forward_draw_count=5`, preview/capture composite work counts `1/1`, and non-empty preview/capture texture RGB proof; allocation report `run_frames_at_least_60=true`, `steady_state_frame_count=190`, `steady_state_allocations_after_warmup=0`, and `overall_verdict=pass`; assertion report `overall_verdict=pass`. Focused build/CTest also passed `OffscreenSceneFacade`, `RendererCompositionQueue`, and `SceneRendererDeferredCore` with 3/3 test executables. User visual confirmation on 2026-04-28 approved the solid-forward offscreen proof visual. ShaderBake/catalog validation was not run because no shader source, shader ABI, root-binding, or catalog files changed. | No open VTX-M06B closure gap. Feature-gated runtime variants remain VTX-M06C. |
| VTX-M06C | Feature-gated runtime variants | `validated` | Detailed plan `design/vortex/plan/VTX-M06C-feature-gated-runtime-variants.md` exists. Slices B-E implemented and validated feature-profile vocabulary/carry, depth-only and shadow-only gates, no-environment/no-shadowing/no-volumetrics gates, diagnostics-only product truth, and focused source tests. Slice F added the MultiView `--feature-variant-proof-layout true` runtime proof, RenderDoc analyzer, CDB/debug-layer wrapper, assertion script, and allocation-churn report. Closure validation passed the full focused build plus `oxygen-graphics-direct3d12_shaders`, full focused CTest with 8/8 Vortex test executables, CDB/debug-layer report with runtime exit `0`, no debugger break, `d3d12_error_count=0`, `dxgi_error_count=0`, and `blocking_warning_count=0`, RenderDoc report with `overall_verdict=true`, `expected_variant_view_count=6`, `composition_view_ids=1,2,3,4,5,6`, correct reduced-variant stage omission, and Stage 22 scene-lighting-only proof, allocation report with `steady_state_frame_count=60` and `steady_state_allocations_after_warmup=0`, assertion report with 65 runtime records for every variant and `overall_verdict=pass`, absence of the prior missing-SceneColor compositing warning, and user visual confirmation on 2026-04-28 approving the proof visuals, shadow stability, compact labels, and `BLACK expected` markers. | No open VTX-M06C closure gap. |
| VTX-M07 | Production readiness and legacy retirement | `validated` | Detailed plan `design/vortex/plan/VTX-M07-production-readiness-legacy-retirement.md` exists and is updated with closure evidence. Static source seam guard `tools/vortex/Assert-VortexLegacySeams.ps1` passed with tooling at `out\build-ninja\analysis\vortex\m07-closeout\legacy-seams-tooling.txt`, scanning 581 current source/build/tooling files with no forbidden legacy renderer seams. Stale uncompiled MultiView legacy source (`Examples/MultiView/ImGuiView.*`) was removed. Active DemoShell/TexturedCube/Physics compatibility seams under `oxygen::renderer` / `renderer::` were removed. Async README/tooling now executes current Vortex proof without `Capture-AsyncLegacyReference.ps1` or `ReferenceRoot`. Required demo refresh fixed DemoShell scene-authored environment behavior, fixed exposure seeding, TexturedCube/InputSystem/Physics sun/environment setup, no-texture-sampling solid materials, and the Physics +Z-up floor; Physics direct-light RenderDoc probe `out\build-ninja\analysis\physics\physics-after-scene-cleanup-direct-lighting-probe.txt` passed with nonzero floor direct lighting proof. Full registered example build matrix passed. Short D3D12/debug-layer smokes passed for Async, InputSystem, LightBench, TexturedCube, Physics, RenderScene, VortexBasic, and MultiView; Devices, Platform, and OxCo examples are classified with build/help/smoke evidence. Closure build passed `Oxygen.Vortex.LinkTest`, `oxygen-vortex`, and all required Vortex example targets. Focused CTest passed 14/14 after explicitly rebuilding a stale `Oxygen.Vortex.OcclusionModule.Tests` binary. Current-path doc/source audit `out\build-ninja\analysis\vortex\m07-closeout\current-source-legacy-doc-audit.txt` passed with zero matches under `src/Oxygen`, `Examples`, and `tools/vortex`. Binary dependency audit `out\build-ninja\analysis\vortex\m07-closeout\binary-dependencies.txt` passed with zero legacy renderer dependency matches. VortexBasic, Async, MultiView standard/auxiliary, Offscreen, and Feature-variant runtime proof wrappers passed under `out\build-ninja\analysis\vortex\m07-closeout` with CDB/debug-layer and RenderDoc/scripted-analysis reports. ShaderBake/catalog validation was not run because no shader source, shader ABI, root-binding, or catalog data changed. `oxygen::imgui` remains allowed UI infrastructure and not a legacy renderer seam. | No open VTX-M07 closure gap. VTX-M08 is validated as the first post-baseline family. |
| VTX-M08 | Skybox and static specified-cubemap SkyLight | `validated` | Plan `design/vortex/plan/VTX-M08-skybox-static-skylight.md` and validated-reference LLDs `design/vortex/lld/cubemap-processing.md` / `design/vortex/lld/skybox-static-skylight.md` record the closed implementation and UE5.7 source-derived contract. Slice A validated SkyLight authored fields, current 92-byte `SkyLightEnvironmentRecord`, no legacy cooked-record decoding, scene descriptor/schema/PakGen/PakDump/DemoShell propagation, Vortex `SkyLightEnvironmentModel` hashing, and per-view `VisibleSkyBackground` plan state with no-environment gating. Slice B validated explicit static SkyLight product state, dedicated `diffuse_sh_srv`, CPU/HLSL `EnvironmentFrameBindings` mirror update, specified-cubemap asset validation, CPU cubemap processing with yaw/lower-hemisphere/HDR scale/mip/average-brightness/eight-`float4` SH output, renderer-owned TextureCube plus SH upload/cache/publication, `GpuSkyLightParams::diffuse_sh_slot`, and `ForwardMesh_PS.hlsl` / `ForwardDebug_PS.hlsl` / `LocalFogVolumeCommon.hlsli` migration away from visual-skybox and cubemap-irradiance fallbacks. Slice C validated scene-authored `SkySphere` publication, invalid default cubemap descriptors, Sky pass solid/cubemap branch before procedural-atmosphere LUT requirements, SkySphere pass gating without procedural-atmosphere opt-in, explicit `SkySphere::intensity` identity semantics, RenderScene startup-skybox loading through `SkyboxService`, focused builds/tests, CDB/debug-layer audit, RenderDoc skybox proof, and 65-frame allocation-churn proof. Slice D validated Stage 12 static SkyLight diffuse SH consumption in deferred lighting, `ibl-only` / `direct-plus-ibl` service-pass debug routing, stable product-cache state after upload-ticket retirement, focused Vortex tests plus ShaderBake/catalog validation, CDB/debug-layer audit, and IBL-only RenderDoc proof with one static SkyLight draw, no direct/sky scopes, valid processed cubemap and diffuse SH slots, non-black SceneColor, and passing pixel histories. Slice E validated direct-plus-IBL interaction and off/on lifecycle proof with RenderDoc, CDB/debug-layer, valid SkyLight re-publication after re-enable, and zero post-warmup allocation churn. Final focused validation passed `ctest --preset test-debug -R "Oxygen\.(Vortex\.CompositionPlanner\|Vortex\.EnvironmentLightingService\|Examples\.DemoShell\.EnvironmentSettingsService)" --output-on-failure`; `git diff --check` passed; user visual confirmation approved sun enable/disable/enable, visible SkyLight contribution, SkyBox disable behavior, and SkyBox suppression when procedural atmosphere is active. | No open VTX-M08 closure gap. Accepted future gaps: captured-scene SkyLight, real-time capture, cubemap blend transitions, SkyLight AO/DFAO/bent-normal/cloud occlusion, baked/static-lightmap SkyLight integration, static-cubemap specular reflections, broader reflection probes, volumetric-cloud sky capture, and procedural sun-disk overlay in static cubemap skybox imagery. |
| VTX-FUTURE | Reserved post-baseline families | `future` | Some placeholder directories or shader inventory may exist. | Geometry virtualization, material composition, broader indirect lighting/GI/reflections, VSM, clouds, heterogeneous volumes, water, hair, distortion, light shafts. |

## 5. Open Cross-Milestone Gaps

| Gap | Blocks | Required Resolution |
| --- | --- | --- |
| Captured/real-time SkyLight, broader SkyLight occlusion, baked SkyLight, and reflection-probe/specular IBL remain future features. | Future indirect-lighting/reflection parity beyond the VTX-M08 static specified-cubemap diffuse baseline. | Promote a future indirect-lighting/reflection milestone with LLDs, UE5.7 grounding, runtime proof, and explicit product ownership before claiming broader IBL/reflection parity. |
| Reflection/360-view aerial perspective lacks a runtime resource path. | Future reflection-capture parity. | Implement with the future reflection-capture resource path before claiming reflection/360 AP parity. |

## 6. Update Checklist

Before changing a milestone status:

1. Update the milestone's design and detailed plan if scope changed.
2. Update exactly one row in the milestone ledger.
3. State implementation evidence, validation evidence, and residual gap in that
   row.
4. Run `git diff --check` for docs-only edits, and the relevant focused
   build/test/runtime proof for implementation edits.
