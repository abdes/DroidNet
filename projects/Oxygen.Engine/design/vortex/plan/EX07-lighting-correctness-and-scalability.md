# EX07 — Physical lighting and many-light qualification

Status: **in_progress — EX07A validated; EX07B references and instruments are current**.
The [A completion audit](EX07A-completion-audit.md) records the scoped contract,
interface and native ABI gate. Six approved decisions freeze mathematical,
property, scene-v7 and resource/failure targets; their complete implementation
and qualification remain required in B–F. EX07 owns end-to-end lighting
correctness and performance. The
[exposure delivery plan](exposure-and-lightbench-correction.md#slice-7---complete-the-reference-lighting-unit-chain)
owns package order and the [tracker](../IMPLEMENTATION_STATUS.md#34-slice-7-work-items)
owns progress. This document owns the bounded EX07 workload and qualification gate.

## Outcome and boundaries

Directional, point and spot lights must produce independently predicted direct
illumination, and large supported light sets must render without missing lights,
unsafe resource use or uncontrolled CPU/GPU work. Deliver measured improvements
to the actual production bottlenecks and publish the supported operating envelope.

Preserve the [physical contract](../../renderer-core/physically-based-rendering.md#physical-light-conversion),
material/precision budgets, scene-owned lights and explicit dual atmospheric
assignments. Qualify ordinary unassigned directionals, each atmospheric slot and
both sources together. Demo sun inference/injection remains explicitly requested.
Coordinate the directional-array implementation with its
[existing owner](editor-v01-rendering-contract.md#3-independent-directional-array-and-atmosphere-assignments);
keep one representation, no compatibility path or primary-only substitute.

Include scene eligibility/mutation, per-view culling, publication/uploads, active
forward/deferred lighting shaders, local shadow integration, submission cost,
resource lifetime, diagnostics and editor-authored input. Preserve existing
contracts for opaque/masked/forward/translucent behavior and repair violations.
Broad GI/IBL, new light types, scattering algorithms, new shadow techniques and automatic quality
degradation are outside this step. Existing shadows must remain correctly
associated and functional; a missing active-path shadow consumer is an in-scope
integration defect, not a reason to omit that case.

## Delivery responsibility

EX07 implementation owns **reviewing, repairing, improving, optimizing and
validating** the complete lighting path covered by this plan. That responsibility
includes pre-existing defects and defects discovered during execution, regardless
of which module or earlier milestone introduced them.

- Trace scene/editor input through selection, publication, culling, shaders,
  shadows, HDR accumulation and output. Review algorithms, physical units, ABI,
  validation, resource lifetime and failure behavior; the starting observations
  below are not an exhaustive defect list.
- Correct faulty production code and necessary cross-module dependencies as part
  of EX07. Coordinate architectural ownership and update its documents without
  moving a required repair to a later slice or treating it as an external blocker.
- Establish independent expected results, add meaningful regression coverage,
  and fix discrepancies before using the path as an optimization baseline.
- Profile the corrected path, implement justified improvements, then validate
  correctness and performance together on the final code. Prior test passes or
  inherited behavior do not exempt a demonstrated defect from correction.
- Keep both gates open until they pass. A defect inventory, benchmark report,
  warning, documented limitation or faster incorrect image cannot close EX07.
  Do not narrow cases, accept missing contributions or redefine supported behavior
  merely to accommodate an existing defect.

The exclusions above concern additional product families. They do not exclude
repairs needed to deliver the agreed lighting behavior. Routine review, repair,
optimization and validation are the implementation responsibility; changing the
agreed product scope or quality target is a separate decision.

## Starting evidence and owners

The initial source review identifies the repair and measurement backlog below;
timings remain unmeasured. These observations are work to resolve and validate,
not accepted limitations or qualifications on delivery responsibility.

| Owner                                                                            | Current source observation                                                                                       | Consequence for EX07                                                                                         |
| -------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------ |
| `Lighting/Internal/ForwardLightPublisher.cpp`                                    | Every cluster references the complete local-light list. CPU vectors/grid entries are rebuilt during publication. | Establish real conservative spatial lists; measure preparation, upload and shader work.                      |
| `Lighting/Internal/LightGridBuilder.cpp`                                         | Builds records and grid metadata; this service path does not dispatch GPU spatial culling.                       | A baked culling shader alone is not evidence of an operational culler.                                       |
| Shader `Services/Lighting/LightCulling.hlsl`                                     | Scans all lights for each cell and clamps output count to per-cell capacity.                                     | Review ABI/coordinates before reuse; eliminate silent light loss on overflow.                                |
| `Types/LightCullingConfig.h`                                                     | 64-pixel XY cells, 32 depth slices and nominal 32 entries per cell.                                              | Exercise 31/32/33 overlap and viewport/depth boundaries; these constants are not a proven scalable envelope. |
| `Lighting/Passes/DeferredLightPass.cpp`                                          | Produces per-local-light volume/fullscreen draws and per-draw constants.                                         | Measure CPU submission, visibility rejection, camera-inside fallback and overlapping volume cost.            |
| `Shadows/Internal/{Point,Spot}ShadowSetup.cpp` and `Types/ShadowFrameBindings.h` | Bindings currently hold four point shadows and eight spot shadows.                                               | Test capacity, mapping and overflow explicitly; never equate total light count with shadow count.            |
| `Diagnostics/ShaderDebugModeRegistry.cpp`                                        | Light-grid debug views are marked unsupported.                                                                   | Publish truthful culling statistics and a useful opt-in visualization for qualification.                     |

Paths above are under `src/Oxygen/Vortex`; shader paths are under
`src/Oxygen/Graphics/Direct3D12/Shaders/Vortex`. Existing `LightingService`, shadow
owners, view publications, upload allocators and profiling remain authoritative.

## Six ordered implementation steps

| Step                                      | Required result                                                                                                                                     | Gate before proceeding                                                                                                                |
| ----------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------- |
| EX07A — Review and freeze contracts       | Canonical LLD/ABI, field-to-consumer inventory, directional authority, GPU scheduling, capacities and failure/recovery behavior.                    | One documented contract; no conflicting historical interface; complete review decisions and test obligations before consumer changes. |
| EX07B — References and instruments        | Independent physical oracle, known-input GPU probes, unculled image reference, deterministic fixtures and bounded CPU/GPU/resource instrumentation. | Reference/instrument validity is established independently of the renderer being tested.                                              |
| EX07C — Repair correctness                | Physical response, every retained property, directional/local/shadow identities, complete lists, ingress/round-trip behavior and safe lifetime.     | Each workload admitted to timing passes its applicable numerical/image/mutation/capacity checks.                                      |
| EX07D — Qualified baselines and budgets   | Correctness-qualified workload baselines, frozen CPU/GPU/memory budgets, useful-improvement thresholds and regression/noise policy.                 | Baseline identity/evidence and numeric comparison thresholds recorded before optimization candidates.                                 |
| EX07E — Scalable culling and optimization | Real spatial rejection and measured shader/submission/upload/shadow/resource improvements.                                                          | Candidates continuously pass the same correctness references and predeclared performance comparisons.                                 |
| EX07F — Final validation and delivery     | Final-code Debug/Release correctness, native performance, editor/native operation, inspected images and complete operating docs.                    | All EX07 gates pass together, with supported limits and no unexplained failures or quality reduction.                                 |

EX07-01–14 remain stable tracking IDs. Contracts and property review precede their
implementation; EX07-06/13 reference and instrument foundations start in EX07B,
and EX07-07's qualified baseline closes only in EX07D. Round-trip/live editor
checks accompany repairs in EX07C; EX07F confirms final integration rather than
discovering missing transport for the first time.

Short exploratory profiles may guide review at any step but cannot be labelled
qualified baselines. EX07C may use a complete conservative list while spatial
rejection is optimized in EX07E; such a baseline must include every valid light
and requested shadow. If existing culling loses contributions, repair it first.
Freeze a baseline separately for each correctly rendered workload. A valid
unculled comparison preserves the measured benefit of introducing spatial culling;
never time a defective image as the acceptance reference.

### Contract and property review deliverables

EX07A must complete the [canonical LightingService contract](../lld/lighting-service.md#2-canonical-data-and-interfaces)
and its [authored-property inventory](../lld/lighting-service.md#5-authored-property-coverage).
For each retained field, identify its source/editor/script/native ingress,
validation/default, persisted representation, selection/GPU member, consumer,
invalidation and positive/negative/round-trip tests. Include per-light compensation,
source radius, cone pairs and all supported shadow settings; defaults alone cannot
qualify these fields. Approved [EX07A D2](EX07A-contract-review.md#d2--physical-lighting-and-artistic-attenuation-scope)
removes the attenuation selector and custom decay exponent through a strict
API/source/packed/tooling migration; it supersedes their earlier retention
requirement. Track rejection of obsolete inputs and migration of every producer.
Approved D3 retains physical sphere/disk emitter extent with conserved flux and
common diffuse/specular emission. Approved D4 selects correlated Smith GGX with
multiple-scattering compensation, retaining Schlick Fresnel and normalized
diffuse. Specify/qualify these models independently and migrate affected shared
BRDF integration consumers together. Explicit product exclusions stay distinct
from defects in supported settings.

Freeze CPU/HLSL field offsets/stride/types and add sentinel decode tests before
connecting the culling shader. Describe shared preparation versus per-view GPU
recording, upload/compute/read dependencies and frame-fence lifetime. The
[capacity/failure contract](../lld/lighting-service.md#4-capacity-failure-and-recovery)
defines candidate rejection, invalid view results, caller diagnostics and recovery;
fault tests must verify the actual application outcome. No unresolved capacity
choice can be silently made while optimizing.

## Deterministic workload envelope

Use seed-controlled static scenes and fixed simulation-time camera/light paths.
Record light positions, ranges, cones, colors, intensities, shadow settings,
geometry/materials, view projection and asset hashes. Freeze actual overlap
histograms and coverage before optimizing; do not change them to improve results.

| Family                            | Required cases                                                                                                                                                                                                                     | Purpose                                                                                                                   |
| --------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------- |
| Calibration and list boundaries   | 0/1 lights; directional None/Primary/Secondary/both; 31/32/33 and 64 lights contributing to one region.                                                                                                                            | Physical response, complete lists, zero-light clearing and additive contribution.                                         |
| Sparse indoor/city distribution   | 64, 256, 1,024 and 4,096 local lights, mixed point/spot; static and moving.                                                                                                                                                        | Separate total count from visible count and local overlap; primary target is 1,024.                                       |
| Mostly irrelevant lights          | Fixed visible subset, with the remainder off-frustum or outside all receiver influence; include off-screen lights whose volumes do reach receivers.                                                                                | Culling removes irrelevant work without rejecting valid off-screen sources.                                               |
| Dense overlap                     | At least 33, 64 and 256 relevant lights in the same region, narrow/wide cones and large ranges.                                                                                                                                    | Correctness when spatial culling cannot reduce real lighting work; stress, not a blanket 60-fps promise.                  |
| Projection and receiving surfaces | Perspective/orthographic; near-plane/camera-inside volumes; viewport offsets, partial tiles, reverse-Z and depth-slice edges; opaque, masked, two-sided/normal-mapped and translucent receivers.                                   | Conservative bounds and valid shader lookups for every supported consumer.                                                |
| Multiple views and lifecycle      | 1080p main plus 960x540 secondary; conflicting visibility, reordered views, resize/hide/recreate, scene replacement, moving/disabled/deleted lights and in-flight frames.                                                          | Per-view isolation, identity, update invalidation and resource retirement.                                                |
| Shadowed subset                   | In a 1,024-light scene: 0/1/4 point and 0/1/8 spot shadows; 5th/9th requests must work when budget permits. Include finite and 90-degree soft spots, isolated/combined directionals, and deliberate actual-byte-budget exhaustion. | Shadow cost and stable mapping, removal of inherited 4/8 limits, explicit failure/recovery at the real resource boundary. |

Both active forward and deferred paths require correctness coverage. Use 1920x1080
as the primary timing resolution and 3840x2160 for scaling. Execute the full light
count sweep at 1080p; restrict 4K to the primary and stress endpoints. The two-view
case and shadowed subset are selected interaction rows, not a Cartesian product
of every feature/count/resolution. The 4,096-light and dense-overlap cases remain
required correctness/scaling evidence, with measured throughput rather than an
unqualified 60-fps claim.

The primary case has equal point/spot populations and at least 256 local lights
with nonzero contribution to visible receiver samples in each timed frame. Record
projected influence area and overlap as well as that count. A scene with almost
all lights off-screen cannot substitute for this case. The separate irrelevant-
light workload measures that optimization explicitly.

## Correctness gates

1. **Independent reference.** Verify selected receiver samples with double-precision
   CPU physical/BRDF calculations. Compare production images against a small,
   opt-in unculled or serially accumulated reference using all eligible lights.
   That reference may reuse shading to isolate culling errors; it cannot replace
   the independent photometric oracle. Keep it in qualification targets, not a
   second shipping renderer. Match material decoding, P, output transform and time.
2. **No false negatives.** Check that every light with a nonzero supported
   contribution at a sampled receiver is present exactly once. Conservative
   false positives are acceptable and measured. Test tangencies, spot-axis/sign
   conventions, near/far depth, off-screen source volumes and origin offsets.
   Do not use opaque-depth or normal rejection for translucent/two-sided/normal-
   mapped receivers unless its conservativeness is independently established.
3. **No truncation.** Size/scan/allocate complete lists or implement another
   correctness-preserving overflow path. Do not keep only the first/brightest N,
   silently clamp counts, or claim fixed capacity implies unsupported illumination.
   Freeze and validate total-light/list-memory limits in EX07A. Follow the LLD's
   atomic input rejection and invalid-view publication contract for overflow or
   allocation failure; verify caller diagnostics, presentation and recovery.
   Partial lighting cannot be reported as success; diagnostics do not spam frames.
4. **Image and accumulation.** Apply the PBR physical/BRDF budget to independent
   predictions and the 0.5% relative + 2e-5 absolute float-product budget to matched
   culled/reference results. Check light-order permutations, HDR accumulation,
   finite outputs and exactly-once pre-exposure. No disappearance/pop when crossing
   cells or culling boundaries. Freeze any summation-specific bound before results.
5. **Visibility and mutation.** Node visibility, affects-world, range/cone,
   intensity/color, transform and assignment edits invalidate the proper selection
   and view products. Light removal cannot reuse another light's shadow/index.
   Zero-light frames clear prior lists; recreated scenes/views inherit no stale state.
6. **Shadow semantics.** Direct-light admission and shadow-map capacity are separate.
   Preserve light-to-shadow identity after reordering/culling. Test supported local
   shadows in both consuming families, off-screen casters and moving receivers.
   Over-capacity requests must be explicit; silently rendering requested shadows
   as unshadowed is not a passing result. Freeze the supported shadow envelope and
   any capacity-policy decision before optimization; do not imply thousands of
   fully shadowed lights from a thousands-of-unshadowed-lights result.
7. **Lifetime and editor input.** Use immutable per-frame/view light products and
   fence-safe retirement for growing/reused buffers, descriptors and constants.
   Exercise failed submissions/readback delay under the D3D12 debug layer. Load a
   deterministic editor-authored fixture through source/cook/PAK/runtime and compare
   its resolved inputs with the native case; check live supported light mutations.
   Preserve EX06's C++20 SDK boundary and strict formats.

## Measurements and improvement order

Measure CPU scene gather/selection, transform/bounds processing, grid/list
construction, allocation, upload bytes and submission separately. Distinguish
active work from waits. Measure GPU culling/list build, forward evaluation,
deferred volume/lighting, shadow depth and shadow sampling, plus total GPU and
native frame intervals. Use non-overlapping intervals; do not add nested scopes
or independent percentiles. Attribute shared passes with matched controls when
their work cannot be separated by timestamps; label estimates as estimates.

Record selected/rejected lights, cell occupancy p50/p95/max, candidate references,
overflow/required capacity, shaded light evaluations or a clearly labelled proxy,
draws/dispatches/PSO switches, uploaded bytes, live/in-flight/cached memory and
steady allocation churn. Deep shader counters are opt-in and outside timed runs.
Counters/heatmaps must describe the executed product, not nominal configuration.

Address bottlenecks in this order, selecting algorithms from evidence:

- Eliminate the all-lights-per-cluster publication. Start with conservative view
  and cell assignment; evaluate light-driven versus cell-driven work and compact
  lists against occupancy. Reuse the existing grid contract where suitable, but
  review the old shader's ABI and coordinate conventions before connecting it.
- Reject noncontributing deferred draws conservatively and reduce avoidable
  per-light CPU binding/constant/PSO overhead. Compare light volumes with a
  tiled/clustered deferred candidate only if profiling justifies it. Such a change
  stays within Stage 12 and requires an explicit LLD/ABI decision; do not retain
  duplicate production algorithms without a measured, documented need.
- Inspect actual light loops, repeated BRDF/conversion work, buffer layout/access,
  bandwidth, divergence, register pressure/spills and overdraw. Hoist only
  view/light/material invariants. Avoid blanket loop unrolling, forced precision
  loss, source-range reduction or removing valid contributions for speed.
- Reuse/grow resources safely, share immutable light records across views, and
  keep visibility/list products view-specific. Remove avoidable steady CPU/GPU
  allocations and duplicate uploads. Any cache must invalidate on every relevant
  light, view and scene mutation; caching is not required if measurement rejects it.
- Optimize existing shadow setup/caster culling/update work when it dominates.
  Do not hide that cost inside unshadowed results, drop shadow requests or reduce
  resolution/update frequency silently. Preserve off-screen contributing casters.

Independent directional contributions and supported shader families must satisfy
their contracts after these changes. Keep implementation focused on the measured
lighting path and necessary dependencies; avoid speculative frameworks and
unrelated exposure optimization. Required correctness and bottleneck repairs
remain part of EX07 until both gates pass.

## Performance acceptance and run discipline

### Bounded shadow memory work

The [2026-09-22 stencil/ownership audit](EX07-shadow-memory-review.md) selects
three focused changes under EX07-10/11. Source tracing and a native GPU format
comparison support the design; production correctness/performance remain open.

| Work                            | Scope and implementation owner                                                      | Required result                                                                                                                                                                 |
| ------------------------------- | ----------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Depth-only conventional maps    | `ConventionalShadowTargetAllocator`, `ShadowDepthPass`, views/PSOs and owning tests | D32 depth with unchanged resolution, bias, reversed-Z and PCF; no shadow stencil clears. Scene/custom stencil, receiver/contact products and VSM remain independently owned.    |
| Compatible local-map sharing    | `ShadowService`, `CascadeShadowPass` and indexed bindings                           | One render/allocation for identical local-light shadow content within the frame; distinct per-view CSM/contact products. Different caster content or generations cannot share.  |
| Bounded allocation reuse/growth | Existing shadow allocator and frame leases/fences                                   | Correct per-light resolution buckets, no redundant complete-set duplication, only affected buckets grow, fence-safe retirement and bounded spare capacity without steady churn. |

EX07C first repairs ownership, complete caster coverage, identity and requested
quality. EX07D freezes a correctly rendered baseline and numeric regression/noise
limits. Introduce each optimization separately in EX07E, and verify the integrated
result in EX07F. Do not use existing multi-view overwrite or resolution-promotion
behavior as a valid reference. No new shadow algorithm, cross-frame content cache,
global aliasing framework or automatic quality reduction is introduced.

The linked audit owns the consumer inventory and targeted regression cases:
CSM motion/blends, masked casters, local face/seam coverage, forward/deferred/
translucent consumption, stencil preservation, incompatible view content and
delayed/discarded submissions. Include whole-frame CPU/GPU timing and lifecycle
spikes; a memory win cannot waive the existing absolute budgets or predeclared
regression limits. Avoid queue drains, duplicate raster work or reduced concurrency
introduced solely to lower allocation counts.

The 4 GiB / 128 MiB ceilings remain upper bounds, not normal allocation targets.
Report actual unique map bytes, allocator committed bytes/slack, pending/retired/
cached resources and whole-process DXGI usage/budget separately. Charge other
engine commitments and explicit headroom at the parent allocator before admission;
do not present the standalone allocation query as whole-engine capacity proof.

### Timing and acceptance

Use the package reference RTX 3080 / Ryzen 9950X for comparable native optimized
Release measurements; record actual adapter/LUID, driver, clocks/power conditions,
compiler/shader identities and memory configuration. Other hardware is reported
separately. VSync/caps, captures, debug/GBV and deep instrument counters are disabled
for timing; correctness/debug-layer runs are separate. Keep production precision,
physical light values, geometry, shadow quality and actual simulation dt fixed.

- **Primary objective:** the frozen 1,024-light sparse/mixed 1080p workload, in
  each rendering family, meets the package's GPU-frame p95 <=14 ms / p99 <=16.67 ms
  and uncapped complete-frame p99 <=16.67 ms. This is a planning target, not an
  existing measured capability; EX07B freezes the recipe and EX07D records its
  correctness-qualified timing baseline.
- **Stage budgets:** EX07D sets numeric CPU preparation/submission, GPU
  culling/evaluation and memory budgets from a correctness-qualified breakdown.
  Record them with baseline identities and correctness evidence before optimizing.
  Establishing these measured budgets is EX07D work. A missed target requires further
  investigation and improvement; it does not justify silently narrowing the
  workload or labelling it complete. Only a proposed change to agreed product
  scope or quality requires a separate decision with measured alternatives.
- **Improvement evidence:** fix the observed all-light publication and any failed
  correctness/capacity gate. Retain performance changes only when matched results
  demonstrate benefit to the targeted cost without an unexplained whole-frame,
  small-light, shadow or multi-view regression under the thresholds below.
  Report before/after absolute times, percentage changes, occupancy and memory;
  code cleanup alone is not improvement.
- **Scaling and resources:** report all count endpoints, dense overlap, 4K,
  two-view and shadowed cases with p50/p95/p99/max and supported capacities.
  Repeated fixed-envelope lifecycle cycles stabilize memory; warmed fixed-capacity
  resources have no recurring GPU allocation churn. Resource growth is counted
  across live, queued, retired and cached allocations.
- **Bounded runs:** warm once until assets/PSOs/capacities are ready, for at least
  300 frames and 10 seconds. Per frozen timing condition collect at least 1,800
  frames and 30 seconds, including complete movement cycles. Select common counts
  for matched comparisons before recording. Keep slow frames; reject incomplete
  or overflowed profiler output. Use short targeted experiments during EX07E;
  collect the final matrix once. Repeat a pair only for diagnosed noise or a
  changed implementation, not to obtain a more favorable percentile.

### Frozen comparison policy

Each timed case's EX07D manifest records the recipe/input hash, build/shader
identity, correctness-result reference, metric/units, absolute budget, baseline
value, minimum useful gain (absolute and relative), maximum regression (absolute
and relative), memory ceiling and measured noise bound. Populate every applicable
threshold with a numeric value before recording a candidate; no post-result
threshold changes or explanatory waiver closes a failed comparison.

A targeted improvement must exceed both its declared useful-gain requirement and
the noise bound. Every designated control (including zero/small-light, shadowed
and multi-view rows) must satisfy its declared regression allowance and absolute
budget. Define the absolute/relative combination rule in the manifest; do not pick
whichever is favorable afterward. A result inside the noise bound is inconclusive,
not improvement. Resolve diagnosed noise with at most the justified matched repeat
described above; otherwise retain the last qualified implementation and continue
with another supported hypothesis. Performance changes cannot waive correctness.

Create correctness tests under the owning `Test` directories and opt-in timing
workloads under `Vortex/Benchmarks` in their own lighting benchmark executable.
Reuse current GPU/CPU profiling, fixtures and capture tooling. This precedes EX08:
it must not depend on its future measurement API, console work or ImGui automation.
Record exact targets/commands as implemented; planned commands are not usable tools.

## Exit artifacts

- Qualified physical references and per-view culling/image/lifetime/capacity tests.
- Canonical ABI/property inventory and tested caller-visible failure/recovery contracts.
- Frozen workload/budget manifest, native baseline/candidate timing and memory
  reports, accepted/rejected optimization decisions and actual supported limits.
- Inspected native captures of sparse/dense/moving/two-view and shadowed scenes,
  useful culling diagnostics, and the editor-authored input check. State the
  expected visible result before each launch. No unexplained warnings/errors.
- Updated LightingService/shader/publication/shadow documentation, relevant
  examples and operating commands; concise EX07 tracker closure linked to evidence.

EX07 closes only when calibration **and** many-light correctness/performance are
qualified. The previously accepted EX051 exposure operating point stays closed;
rerun its affected cases only if shared changes invalidate that evidence.

## Technical references

- [Clustered Deferred and Forward Shading](https://research.chalmers.se/en/publication/161725)
  motivates spatial assignment shared across rendering families; it does not
  establish Oxygen's performance or prescribe its implementation.
- [GPU occupancy guidance](https://gpuopen.com/learn/occupancy-explained/)
  supports profiling register pressure and spills rather than maximizing occupancy
  blindly. Use tools/counters appropriate to the actual adapter.
- [D3D12 GPU-based validation](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-d3d12-debug-layer-gpu-based-validation)
  covers descriptor/lifetime checks and its instrumentation overhead.
