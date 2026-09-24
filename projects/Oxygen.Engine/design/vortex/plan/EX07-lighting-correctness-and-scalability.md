# EX07 — Physical lighting and many-light qualification

Status: **in_progress**; current stage and deliverable status are maintained only
in [tracker section 3.4](../IMPLEMENTATION_STATUS.md#34-slice-7-work-items).
EX07 owns end-to-end lighting correctness and performance. The
[current PBR model](../../renderer-core/physically-based-rendering.md#production-local-lighting-and-brdf-model-2),
[GPU ABI](../lld/lighting-gpu-abi.md) and [property inventory](../lld/lighting-properties.md)
define production behavior. The [exposure delivery plan](exposure-and-lightbench-correction.md#slice-7---complete-the-reference-lighting-unit-chain)
owns package order; this document owns workloads and qualification. A/B audits
retain the contract and independent-reference evidence.

## Conventional-shadow implementation sequence

The following implementation sequence, automated acceptance checks and user
visual acceptance are **complete**. Visual approval was received on 2026-09-24.
Evidence is recorded
in [tracker section 3.4](../IMPLEMENTATION_STATUS.md#34-slice-7-work-items).
Completed EX07C integration uses these delivered implementations:

1. **Eligibility and spatial caster culling.** Use the same energy, range,
   participation and view-relevance decisions in allocation, setup, recording
   and publication. Cull world-space caster bounds per point face, spot
   projection and extruded directional cascade, retaining contributing
   off-screen casters. Publish empty eligible maps as fully lit and distinguish
   irrelevant lights from failed required allocations.
2. **Per-light allocation and D32.** Group local maps by resolution, retain
   stable scene/light owners through Nexus `FrameDrivenIndexReuse<ShadowSlotIndex>`,
   publish fixed surface/layer associations, reconcile empty local selections, grow
   only affected buckets and retire resources after their consumer fences.
   Migrate conventional textures, views, PSOs and clears to depth-only D32.
   Scene/custom stencil is unchanged.
3. **Cross-frame local depth caching.** Reuse unchanged point/spot map contents
   with complete light-space coverage. Re-evaluate current caster membership so
   entering and leaving casters invalidate the affected map. Track light,
   projection, resolution and each relevant caster's geometry, transform and
   depth-affecting material/texture inputs. Geometry content revisions invalidate
   unchanged handles/SRVs after hot reload, including while a light is out of view. Recorded
   contents become reusable only after successful submission; retain and honor
   the producing queue/fence dependency. Failed/discarded work is never valid
   cache content. Directional cascades remain view-dependent.
4. **Spatial light lists.** Build conservative per-view compact cell membership
   from the shared typed light selection. Reserve the complete-list sentinel for
   actual capacity fallback, publish truthful status and preserve references
   and buffers through their final consumers. Start compact storage from bounded
   occupancy and grow from completed GPU counts, sharing the aggregate budget
   across all active views and frame slots. Never silently truncate lights.
5. **Projected-benefit quality.** Select resolution buckets within the chosen
   quality profile and authored ceiling, with hysteresis and defined fading.
   Resolution changes invalidate cached depths. Keep intentional quality
   choices distinct from allocation failure. Orthographic quality uses projected
   size regardless of camera distance; the user confirms visual tuning.

Repair related correctness defects, duplicated decisions and superseded code
within the implementing step. These changes supply parts of C/D/E; later stages
reuse the implementation rather than creating another allocator, cache or culler.
Current status and remaining integration work live in
[tracker section 3.4](../IMPLEMENTATION_STATUS.md#34-slice-7-work-items).

Build affected targets and use existing focused unit/GPU tests for each step.
Use the shadow-enabled Instancing scene for incremental static-scene runtime and
Tracy checks with `--fps 0 --vsync false`. The user owns interactive camera/caster
visual checks. Run New Sponza only after all five steps and their unit tests are
complete, then make one comparison against the matching uncapped/VSync-off
baseline. Do not add a validation framework or automate an exhaustive visual
matrix. VSM is excluded from this work.

## Outcome and boundaries

Directional, point and spot lights must produce independently predicted direct
illumination, and large supported light sets must render without missing lights,
unsafe resource use or uncontrolled CPU/GPU work. Deliver measured improvements
to the actual production bottlenecks and publish the supported operating envelope.

Preserve the [physical contract](../../renderer-core/physically-based-rendering.md#physical-light-conversion),
material mapping and exposure precision, scene-owned lights and explicit dual atmospheric
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

Production model 2 uses analytic source evaluation, center-cone attenuation,
view-dependent energy compensation and one compact hardware-filtered LUT.
Ordinary spots use cone proxies and projected shadows. Review quality, GPU/CPU
time and memory together; numerical-reference error alone does not select the
production algorithm. The PBR owner explains each compromise and rejected
alternative. Reuse material/view terms and preserve safe frames in flight while
profiling the complete application with native tools.

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

The initial review established the areas below. Current behavior and remaining
work are summarized here; measured regression-repair results are in the tracker.

| Owner                                                                            | Implementation and remaining work                                                                                | Consequence for EX07                                                                                         |
| -------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------ |
| `Lighting/Internal/ForwardLightPublisher.cpp`                                    | Every cluster references the complete local-light list. CPU vectors/grid entries are rebuilt during publication. | Establish real conservative spatial lists; measure preparation, upload and shader work.                      |
| `Lighting/Internal/LightGridBuilder.cpp`                                         | Builds records and grid metadata; this service path does not dispatch GPU spatial culling.                       | A baked culling shader alone is not evidence of an operational culler.                                       |
| Shader `Services/Lighting/LightCulling.hlsl`                                     | Scans all lights for each cell and clamps output count to per-cell capacity.                                     | Review ABI/coordinates before reuse; eliminate silent light loss on overflow.                                |
| `Types/LightCullingConfig.h`                                                     | 64-pixel XY cells, 32 depth slices and nominal 32 entries per cell.                                              | Exercise 31/32/33 overlap and viewport/depth boundaries; these constants are not a proven scalable envelope. |
| `Lighting/Passes/DeferredLightPass.cpp`                                          | Produces per-local-light volume/fullscreen draws and per-draw constants.                                         | Measure CPU submission, visibility rejection, camera-inside fallback and overlapping volume cost.            |
| `Shadows/Internal/{Point,Spot}ShadowSetup.cpp` and `Types/ShadowFrameBindings.h` | Indexed dynamic families replace the former four-point/eight-spot arrays.                                        | Test capacity, mapping and overflow explicitly; never equate total light count with shadow count.            |
| `Diagnostics/ShaderDebugModeRegistry.cpp`                                        | Light-grid debug views are marked unsupported.                                                                   | Publish truthful culling statistics and a useful opt-in visualization for qualification.                     |

Paths above are under `src/Oxygen/Vortex`; shader paths are under
`src/Oxygen/Graphics/Direct3D12/Shaders/Vortex`. Existing `LightingService`, shadow
owners, view publications, upload allocators and profiling remain authoritative.

## Six ordered implementation steps

| Step                                      | Required result                                                                                                                                     | Gate before proceeding                                                                                                                      |
| ----------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------- |
| EX07A — Review and freeze contracts       | Canonical LLD/ABI, field-to-consumer inventory, directional authority, GPU scheduling, capacities and failure/recovery behavior.                    | One documented contract; no conflicting historical interface; complete review decisions and test obligations before consumer changes.       |
| EX07B — References and instruments        | Independent physical oracle, known-input GPU probes, unculled image reference, deterministic fixtures and bounded CPU/GPU/resource instrumentation. | Reference/instrument validity is established independently of the renderer being tested.                                                    |
| EX07C — Repair correctness                | Physical response, every retained property, directional/local/shadow identities, complete lists, ingress/round-trip behavior and safe lifetime.     | Each workload admitted to timing passes its applicable numerical/image/mutation/capacity checks.                                            |
| EX07D — Many-light scene and baseline     | Runnable deterministic many-light scene and its correctness-qualified CPU/GPU, memory and image baseline; existing accepted baselines are credited. | The scene and required presets are reproducible, their baseline evidence is recorded, and the user can assess the measured operating point. |
| EX07E — Scalable culling and optimization | Real spatial rejection and measured shader/submission/upload/shadow/resource improvements.                                                          | Candidates preserve implementation checks and report quality, timing and memory against matched baselines.                                  |
| EX07F — Final validation and delivery     | Final-code Debug/Release correctness, native performance, editor/native operation, inspected images and complete operating docs.                    | All EX07 gates pass together, with supported limits and no unexplained failures or quality reduction.                                       |

Current stage and item status live only in [tracker section 3.4](../IMPLEMENTATION_STATUS.md#34-slice-7-work-items).
This plan defines requirements; the audits preserve proof.

EX07-01–14 remain stable tracking IDs. Contracts and property review precede their
implementation; EX07-06/13 reference and instrument foundations start in EX07B,
and EX07-07's many-light scene and baseline close in EX07D. Round-trip/live editor
checks accompany repairs in EX07C; EX07F confirms final integration rather than
discovering missing transport for the first time.

Short exploratory profiles may guide review at any step but cannot be labelled
qualified baselines. EX07C may use a complete conservative list while spatial
rejection is optimized in EX07E; such a baseline must include every valid light
and requested shadow. If existing culling loses contributions, repair it first.
Freeze a baseline separately for each correctly rendered workload. A valid
unculled comparison preserves the measured benefit of introducing spatial culling;
never time a defective image as the acceptance reference.

### EX07D delivery: many-light scene and baseline

**EX07D closed (2026-09-24):** implementation is committed in `137b681b2` and
the expanded baseline register/evidence in `894a25e57`:
54 timed, image-qualified benchmark rows and four current Instancing/New Sponza
application runs, with separate Tracy/non-Tracy Ninja Release identities,
percentiles, memory scope, images, traces, recipes and historical controls.
CPU/GPU preflight is recorded for 24 benchmark rows and all application runs;
the other 30 timings predate that requirement and are explicitly supporting
references. Noisy B05-D requires a targeted comparison before a timing claim.
See the [baseline register](EX07D-baseline-report.md). Do not restart
accepted baseline campaigns or treat slow deferred results as an architecture
policy change. Deferred draw culling must conservatively retain off-screen
lights whose influence reaches the view, as UE5.7's light-volume frustum test
does, and must not prune light/shadow or off-screen caster selection.

Credit the accepted model-2 MultiView operating point, conventional-shadow
New Sponza/Instancing measurements and B's reference/instrument qualification.
Their evidence and original coverage are recorded in
[tracker section 3.4](../IMPLEMENTATION_STATUS.md#ex07d--credited-evidence-and-established-baseline).
Keep those results as controls for their measured workloads. Reuse existing
captures for additional statistics where possible. Reopen an accepted case only
when a specific change invalidates its evidence or diagnosed noise prevents the
required comparison; record that reason before repeating it.

D's completed delivery comprises the many-light test scene and its baseline:

**Rendering-family scope:** preserve Vortex's deferred-first desktop contract.
D measures both deferred and forward rendering on the same many-light recipes.
Deferred scaling problems remain visible baseline results and EX07E inputs;
poor performance does not remove a workload from qualification.

1. **Create a runnable, inspectable scene from the existing recipe.** Reuse
   `Test/Support/LightingWorkload.{h,cpp}` and
   `Test/Lighting/LightingWorkloads.json` under `src/Oxygen/Vortex`, along with
   the native preview's scene setup. Supply the receiver geometry, materials,
   cameras and real scene-owned lights through the production rendering paths.
   The primary preset is 1,024 lights (512 point / 512 spot) at 1920x1080, with
   at least 256 visible contributors throughout the deterministic motion cycle.
   Provide the count, distribution, motion, resolution, view and shadow variants
   in the workload envelope below as presets of the same scene. Preserve the
   frozen inputs and add the receiver/caster geometry required by each preset.
2. **Qualify that scene for measurement.** Reuse the existing references and
   focused correctness checks to verify contributions, actual output size,
   spatial lists, shadow identity and applicable mutation/lifetime behavior.
   Make the rendered scene available for visual inspection. The 64x36 preview
   and allocation tests remain useful prerequisites; full-resolution rendering
   is part of this delivery. No new oracle or general validation framework is
   required.
3. **Record its baseline.** Use the existing opt-in lighting benchmark target
   and native profiling/capture tools. Measure the primary scene in both forward
   and deferred rendering, then the prescribed count sweep and selected stress,
   motion, 4K, multi-view and shadow presets. Follow the bounded run discipline
   below; variants do not form an exhaustive Cartesian product. Record CPU/GPU
   costs, frame percentiles, actual memory, image quality and measurement noise,
   with exact launch commands, recipe/code/shader identities and capture paths.

D exits with the runnable scene/presets and their reproducible, correctly rendered
baseline report. The user decides whether its measured operating point is
acceptable. E uses this same scene and baseline for justified optimizations;
F reuses unaffected evidence and validates the integrated result. Stage changes
alone do not require another baseline campaign. The workload envelope and full
EX07 exit requirements remain unchanged.

### EX07E handoff — closed

**E06 implementation resumed by user instruction, 2026-09-24.** The independent
review is accepted with the descriptor-cleanup and uncertain-submission recovery
amendments. The [authoritative checkpoint](../IMPLEMENTATION_STATUS.md#ex07e--work-items-and-resume-checkpoint)
records completed S1–S9 implementation/qualification and user visual acceptance
on 2026-09-25. The requested numeric review is complete and evidence is committed
in `b8f1376e1`. The [sharing comparison report](EX07E-shadow-sharing-results.md)
owns final benchmark, scene, memory and operating-limit results.

**Closed: implementation, qualification and user acceptance complete (2026-09-25).** The authoritative
[E01–E08 work ledger and resume checkpoint](../IMPLEMENTATION_STATUS.md#ex07e--work-items-and-resume-checkpoint)
record all eight investigation/delivery obligations, completion evidence and
next action. Update that ledger as findings and fixes are delivered. Earlier
measured repairs remain credited. A–E are closed, including the
[C caller/importer follow-up](EX07C-completion-report.md). Overall EX07 remains
open only for F acceptance.

Begin with the committed
[baseline register](EX07D-baseline-report.md#how-e-and-f-use-this-register) and
[source/UE5.7 analysis](EX07-NewSponza-regression-analysis.md). Use the current
A-SPONZA and A-INSTANCING traces to isolate the dominant local-light shader work
before selecting an implementation. Explain the expected cost reduction and
correctness/quality implications against UE5.7's corresponding path. Preserve
conservative influence-volume culling, off-screen contributing lights/casters,
authored ranges and shadow requests. Broader bias/filtering parity remains an
explicit investigation, not an established equivalence.

Select matched baseline IDs and freeze candidate acceptance/noise criteria before
timing. Requalify only affected or insufficiently controlled rows; B05-D and the
30 rows without recorded CPU preflight cannot support small timing claims as-is.
Use only existing `out/build-ninja` and `out/build-tracy-ninja`, both Release,
keeping Tracy attribution separate from native throughput. Check sustained heavy
CPU/GPU contention while allowing ordinary desktop use. Do not repeat the full
baseline campaign merely to enter E. Candidate reports must preserve image and
physical-reference checks, show whole-frame/stage and relevant memory results,
and record retained limits. F owns final integrated and interactive acceptance.

### Recorded E priorities after the New Sponza regression analysis

The [source and Tracy analysis](EX07-NewSponza-regression-analysis.md) records
performance work still required:

| Priority                                 | Evidence / problem                                                                                                                        | Recommendation and acceptance                                                                                                                                                                                               |
| ---------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| GPU local-light evaluation               | Registered Sponza Tracy: 45.288 ms across 23 point draws within 46.309 ms deferred lighting; Instancing: 26.433 ms across 39 point draws. | Isolate filter, material-fetch and BRDF/register costs. Measure specialization and uniform-data reuse first, preserving contributions and matching content, quality and instrumentation.                                    |
| Translucent lighting                     | Registered Sponza Tracy: 7.726 ms.                                                                                                        | Profile the shared forward evaluator and overlap before selecting changes; retain physical/material and shadow references in both families.                                                                                 |
| Shadow bias/filtering                    | Receiver-footprint repair passes point/spot, short/long-range tests; the whole point pipeline is not quantitatively UE5.7-equivalent.     | Audit units, depth producer/consumer and nonzero authored bias together. Comparison filtering needs a quality contract and contact/grazing/cube-seam coverage; do not copy constants or hide fewer samples as optimization. |
| Shared-grid scaling                      | Registered B09 grid means: 10.959 ms deferred / 10.800 ms forward, non-Tracy Release.                                                     | Compare against count, overlap, irrelevant-light, motion and view baselines; preserve complete-list fallback and every valid contributor.                                                                                   |
| Application lifetime/final qualification | Premature DemoShell composite-target release caused the close failure; the fixed debug-layer run closes cleanly.                          | F includes application close, resize/view removal, movement and scene replacement alongside offscreen checks.                                                                                                               |

Current Sponza has 4,096 m point ranges; the accepted older capture had 10 m.
Their timing difference is not an isolated code regression, and shortening ranges
is not an acceptable manufactured speedup. VSM/clustered shadow routing is not a
drop-in fix: UE5.7's shadowed clustered eligibility depends on its VSM one-pass
path. Review a larger architecture change separately after the measured shader
work, with its ownership, resource and quality implications.

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
D3 uses analytic source-size diffuse/specular response with center-based range and
cone support. D4 uses shared correlated Smith GGX, Schlick Fresnel and
view-dependent multiple-scattering compensation. Direct and indirect consumers
share the compact energy texture and material interpretation. Compare the
approximations with independent references and report image, energy and lobe
differences alongside timing and memory.

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
   `tools/vortex/AssertLightingPhysicalProbe.py <native-results.json>` checks
   report completeness, schema and model revision. Native implementation checks
   must pass; independent numerical differences quantify model-2 approximation
   quality rather than enforce the former reciprocal/integrated-source model.
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
4. **Image and accumulation.** Report model-2 differences from independent
   predictions. Apply the 0.5% relative + 2e-5 absolute float-product budget to
   matched culled/unculled results using the same shading model. Check light-order permutations, HDR accumulation,
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

The conventional-shadow sequence above governs the prerequisite delivery. The areas below
are covered by that work or assessed afterward from the remaining measured costs;
they are not a second ordered implementation list:

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
  allocations and duplicate uploads. Any cache must invalidate on relevant
  content or projection mutation. A camera move alone does not invalidate
  a complete light-space local-depth cache; directional cascades remain view-dependent.
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
comparison supported the initial design. D32 cutover, retained ownership,
cross-frame cache reuse and focused/static performance acceptance are complete;
broader workload qualification and cross-view sharing remain later work.

| Work                            | Scope and implementation owner                                                      | Required result                                                                                                                                                                 |
| ------------------------------- | ----------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Depth-only conventional maps    | `ConventionalShadowTargetAllocator`, `ShadowDepthPass`, views/PSOs and owning tests | D32 depth with unchanged resolution, bias, reversed-Z and PCF; no shadow stencil clears. Scene/custom stencil, receiver/contact products and VSM remain independently owned.    |
| Compatible local-map sharing    | `ShadowService`, `CascadeShadowPass` and indexed bindings                           | One render/allocation for identical local-light shadow content within the frame; distinct per-view CSM/contact products. Different caster content or generations cannot share.  |
| Bounded allocation reuse/growth | Existing shadow allocator and frame leases/fences                                   | Correct per-light resolution buckets, no redundant complete-set duplication, only affected buckets grow, fence-safe retirement and bounded spare capacity without steady churn. |

The conventional-shadow sequence completes D32, bounded allocation ownership and
cross-frame point/spot depth-content reuse with complete light-space coverage.
Current local contents are retained per view; compatible cross-view sharing is a
later EX07E optimization using the delivered allocation/content ownership.
Cascades remain view-dependent. Do not schedule a
second allocator, D32 conversion or cache implementation under EX07E. Step 5
separately selects a user-confirmed resolution/fade policy; memory exhaustion
remains distinct from that intentional quality choice. No VSM or global aliasing
framework is introduced.

The linked audit owns the consumer inventory and targeted regression cases:
CSM motion/blends, masked casters, local face/seam coverage, forward/deferred/
translucent consumption, stencil preservation, incompatible view content and
delayed/discarded submissions. Include whole-frame CPU/GPU timing and lifecycle
spikes; evaluate memory savings against quality and whole-frame cost. Avoid
queue drains, duplicate raster work or reduced concurrency
introduced solely to lower allocation counts.

The 4 GiB / 128 MiB ceilings remain upper bounds, not normal allocation targets.
Report actual unique map bytes, allocator committed bytes/slack, pending/retired/
cached resources and whole-process DXGI usage/budget separately. Charge other
engine commitments and explicit headroom at the parent allocator before admission;
do not present the standalone allocation query as whole-engine capacity proof.

### Timing and acceptance

Use the reference RTX 3080 / Ryzen 9950X for comparable optimized Release
measurements. Record adapter/LUID, driver, clocks/power conditions, compiler and
shader identities, scene and memory configuration. Keep VSync and frame caps
disabled. Use Tracy captures for CPU/GPU attribution; report uninstrumented
throughput separately when measured. Debug/GBV, RenderDoc and deep shader counters
belong to separate diagnostic runs. Keep geometry, light values, shadow quality,
simulation time and physical output resolution identical within a comparison.

- **Operating points:** measure the 1,024-light sparse/mixed 1080p workload in
  each rendering family, plus required count, dense-overlap, 4K, multi-view and
  shadowed cases. Report p50/p95/p99/max frame and stage costs, quality differences
  and actual memory. Select the quality/time/memory compromise from measured
  alternatives; the user decides whether the resulting budget is acceptable.
- **Controlled MultiView profiling:** use fullscreen `--resolution 2560x1440`
  (the parser also accepts `X`), `--fps 0`, `--vsync false`,
  `--directional-array-proof true`, `--offscreen-proof-layout true`,
  `--pip-wireframe false` and `-v=-1`. Verify actual framebuffer dimensions and
  uncapped presentation in the trace. This regression workload complements the
  many-light matrix; it does not establish its scaling result.
- **Resources:** distinguish live, queued, retired and cached allocations,
  allocator slack and whole-process usage. Repeated lifecycle cycles should
  stabilize memory; warmed fixed-capacity resources should not allocate each
  frame. The resource admission ceilings remain separate from performance goals.
- **Bounded runs:** warm until assets, PSOs and capacities are ready, and record
  the excluded warmup and sampled frame counts. Include complete movement cycles
  for dynamic workloads. Use short targeted experiments during optimization and
  collect the final matrix on integrated code. Retain slow frames and reject
  incomplete profiler output. Repeat only when code changes or diagnosed noise
  warrant it.
- **Visual captures:** let exposure settle and record applied/target values for
  each view. The qualified model-2 MultiView capture uses frame 2000; verify
  convergence rather than treating an early fixed frame as correctly exposed.

### Comparison and acceptance

Record baseline/candidate recipe hashes, code and shader identities, correctness
results, absolute times, memory and image/numerical comparisons. Measure noise
and report changes within it as inconclusive. Include small-light, shadowed and
multi-view controls so a local optimization does not conceal a whole-frame cost.
Explain retained compromises and rejected alternatives in the owning design doc.

Implementation defects such as missing lights, invalid lifetimes, ABI mismatches
or nonfinite results must be fixed. Deliberate shading approximations are assessed
through their measured quality, timing and memory together. Do not change inputs,
shadow settings or image coverage to manufacture an apparent improvement.

Create correctness tests under the owning `Test` directories and opt-in timing
workloads under `Vortex/Benchmarks` in their own lighting benchmark executable.
Reuse current GPU/CPU profiling, fixtures and capture tooling. This precedes EX08:
it must not depend on its future measurement API, console work or ImGui automation.
Record exact targets/commands as implemented; planned commands are not usable tools.

## Exit artifacts

- Qualified physical references and per-view culling/image/lifetime/capacity tests.
- Canonical ABI/property inventory and tested caller-visible failure/recovery contracts.
- Workload/measurement manifest, native baseline/candidate timing and memory
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
