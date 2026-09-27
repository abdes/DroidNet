# VX-IBL-01 — Captured sky lighting

Status: `in_progress`

| Field     | Summary                                                                                                                                                                                                                                                                                 |
| --------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Outcome   | Atmosphere/height-fog and specified-cubemap diffuse/specular IBL, immediate authoring and budgeted runtime updates.                                                                                                                                                                     |
| Remaining | [S7 reusable infrastructure](#s7--reusable-infrastructure) has shared group reductions and integrated qualification remaining; [S8](#s8--atmosphere-and-fog-correctness) addresses seven confirmed atmosphere/fog defects. Track [VX-IBL-01](../../OPEN_ITEMS.md#p1--current-delivery). |
| Evidence  | [Lighting acceptance](#acceptance) · [Validation](validation.md). S1–S6 remain validated; S7 distant-sky synchronization, S7.1–S7.4 and S7.6 checks pass; shared group reductions and integrated qualification remain.                                                                  |

Read: [scope](#scope-and-ownership), [delivery sequence](#delivery-sequence),
[DemoShell UI](#demoshell-ui), [validation tools](#validation-tools),
[lighting acceptance](#acceptance), [S7 plan](#s7--reusable-infrastructure),
[S8 correctness fixes](#s8--atmosphere-and-fog-correctness). The [IBL design](../../lld/captured-sky-ibl.md) owns
the algorithms, product formats, scheduling rules and performance targets.

## Scope and ownership

Deliver captured atmosphere plus exponential height fog, specified-cubemap
diffuse/specular products, Stage 13 indirect lighting and the shared
forward/translucent evaluator. Immediate first-use/authoring and budgeted
incremental runtime updates ship together. Remove the Stage 12 ambient bridge
and obsolete authored capture-mode bool in the canonical activation.

Environment owns source capture, processing and publication; IndirectLighting
owns surface evaluation. Graphics supplies existing submission, barriers, fences
and lifetime tracking. Native/editor authoring supplies transient edit intent.
Use Nexus `IndexReuse` and retirement tickets for versioned product-slot reuse;
Graphics supplies completion and managed registration lifetimes.
No new scheduling controls appear in the Inspector or DemoShell. Keep the job
queue and admission limit inside Environment; reuse Graphics and profiling.

This is the native lighting dependency of [ED-M08](../ED-M08/README.md).
Its editor-wide slice order stays in the
[editor plan](../../../../../../design/editor/plan/ED-M08-runtime-parity-and-standalone-validation.md).
Clouds, local/volumetric fog capture, geometry reflections, GI/SSR, local probes,
AO, cubemap blending and new material models remain outside this milestone.

## Delivery sequence

Implement and commit each buildable slice in order. Update these rows in place.

| ID           | Deliverable                                                                                                                                                                                                                                                                       | Slice exit                                                                                                                                                                                                                                                              | State       |
| ------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------- |
| VX-IBL-01.S1 | Common HDR products/metadata ABI, GPU SH/prefilter producers, BRDF lookup and shared evaluators, with frozen inputs, submission-owned publication and fence/reader retirement from the start.                                                                                     | Runnable native offscreen fixture renders diffuse and glossy/rough material references from a known cube. Independent numerical checks cover orientation, scale, SH, roughness mapping and BRDF integration.                                                            | validated   |
| VX-IBL-01.S2 | Immediate atmosphere/height-fog capture at the global anchor, shared visible-sky fog evaluation, Stage 13 activation and forward/translucent integration. Remove ambient-bridge consumers and migrate the retired capture bool across native/source/packed/editor/example routes. | Normal RenderScene renders diffuse/specular through the production ownership path. Both lights, fog-only, disk suppression, specified-cube and independent contribution gates pass; current inputs recook/load. No temporary unsafe lifetime or duplicate ambient path. | validated   |
| VX-IBL-01.S3 | Complete cache/edit-intent behavior and stress the S1/S2 lifetime foundation: immutable LUT versions, bounded IBL admission, capture-busy outcomes, scene/device teardown and generation retirement.                                                                              | Repeated authoring drags consume the current snapshot; unchanged keys reuse products. Failure/discard, late completion, teardown, view recreation and retained-reader tests pass.                                                                                       | validated   |
| VX-IBL-01.S4 | Budgeted incremental scheduling, work tiling, one active candidate/latest desired snapshot, immediate preemption and automatic selection.                                                                                                                                         | Continuous runtime edits cannot starve publication. Candidate completion, published-source age and GPU cost meet the design gates; results match immediate processing of the same frozen source.                                                                        | validated   |
| VX-IBL-01.S5 | DemoShell panel/VM/settings changes below, native/editor visual scenarios, and existing diagnostics for generation age, CPU/GPU costs and failures.                                                                                                                               | Deferred/forward/translucent IBL, multi-view and offscreen images agree. Real DemoShell widgets exercise immediate edits, reset/load/save, source changes, fog participation and stable status; editor controls use the same owners.                                    | validated   |
| VX-IBL-01.S6 | Integrated correctness, performance and resource qualification; update owner designs and operating guidance to delivered behavior.                                                                                                                                                | Every acceptance row below has its result and evidence link; both schedules and the canonical migration are complete.                                                                                                                                                   | validated   |
| VX-IBL-01.S7 | Promote six proven patterns into Nexus, renderer upload/resources/feedback and shared shader utilities; migrate the named production consumers.                                                                                                                                   | All six work items below and the integrated S7 exit pass, preserving S1–S6 lighting, performance and lifetime contracts.                                                                                                                                                | in_progress |
| VX-IBL-01.S8 | Correct seven atmosphere/fog defects through the existing Environment owners; see the bounded plan below.                                                                                                                                                                         | Focused native regressions, visible composition and same-budget performance checks pass; the approved holdout coverage contract is implemented.                                                                                                                         | planned     |

Primary implementation entry points:

- [Environment processing](../../../../src/Oxygen/Vortex/Environment/Internal/IblProcessor.cpp),
  [probe state](../../../../src/Oxygen/Vortex/Environment/Passes/IblProbePass.cpp),
  [publication/LUT scheduling](../../../../src/Oxygen/Vortex/Environment/EnvironmentLightingService.cpp).
- [Fog evaluation](../../../../src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Environment/Fog.hlsl),
  [sky rendering](../../../../src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Environment/Sky.hlsl),
  [frame stages](../../../../src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp).
- [Native SkyLight](../../../../src/Oxygen/Scene/Environment/SkyLight.h),
  [packed records](../../../../src/Oxygen/Data/PakFormat_world.h),
  [GPU environment ABI](../../../../src/Oxygen/Vortex/Types/EnvironmentStaticData.h).

## S7 — Reusable infrastructure

**State: `in_progress`.** Six refactorings, with concrete production adopters and
technical contracts in the owning documents linked below.
The starting implementation is `a58b00e0c`; S1–S6 results remain the qualified
lighting baseline. The [distant-sky barrier correction](evidence/s7-barrier/run.json)
passes native sky checks and the authoring timing gate.
[S7.4 registry methods](evidence/s7-resources-final/run.json) and
[S7.1 immutable uploads](validation.md#s71-immutable-lut-uploads) and
[S7.3 retirement](validation.md#s73-retirement-accounting) and
[S7.2 feedback](validation.md#s72-bounded-gpu-feedback) and
[S7.6 cubemap geometry](validation.md#s76-cubemap-geometry) pass their focused checks;
shared group reductions and integrated qualification remain.

Read: [work items](#s7-work-items), [execution and exit](#s7-execution-and-exit),
[C++ guidance](../../../../../../design/oxygen/RULES.md#c).

### S7 work items

IDs preserve the six review items. First land S7.5's distant-sky barrier fix as
an isolated correctness commit with its native sky/LUT checks; S7.5 remains open
until its extraction is complete. Then follow the dependency order:
**S7.4 → S7.1 → S7.3 → S7.2 → S7.6 → S7.5**, then the integrated exit.
Each extraction lands as a buildable, reviewed commit with its adopters and checks.

| ID   | Deliverable / boundary                                                                                                                                                               | Production adopters                                                                                       | Focused exit                                                                                                                                             | State       |
| ---- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | --------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------- |
| S7.1 | [Immutable texture initialization](../../lld/upload.md#immutable-texture-initialization) in Upload; reuse existing 2D packing/planning and explicit submission.                      | IBL BRDF and direct-light BRDF-energy resources and their consuming recordings.                           | Exact LUT contents; managed lifetime, budget accounting, failed submission/retry and same-frame use pass; duplicate pack/copy initialization is removed. | validated   |
| S7.2 | [Bounded typed feedback](../../lld/gpu-feedback.md#bounded-gpu-feedback) over ReadbackManager; transport only.                                                                       | IBL metadata, PostProcess exposure status, SpatialLightGrid demand.                                       | All three retain their capacity, polling order, identity checks and failure behavior; no new stalls, submissions or readback frequency.                  | validated   |
| S7.3 | [Retirement accounting](../../../../src/Oxygen/Nexus/Docs/slot-retirement.md#10-owner-and-use-accounting) above Nexus IndexReuse; preserve family admission and Graphics completion. | IBL ProductVersion and shared-shadow ShadowSlotCore.                                                      | Ordinary ownership and retained pins drain independently; discard/fault/close and allocation-denied retirement pass in both families.                    | validated   |
| S7.4 | [Managed resource/view setup](../../../../src/Oxygen/Graphics/Common/README.md#managed-resources-and-views); caller chooses allocation and view descriptors.                         | IBL allocations, captured-sky target setup and shared-shadow backing/initial SRV setup; S7.1 consumes it. | Transactional setup, domains, mip views and cleanup pass; shadow budget fallback and lazy DSVs stay intact.                                              | validated   |
| S7.5 | [Shared group reductions](../../lld/shader-contracts.md#111-group-reductions); preserve fused trees and numerical contracts.                                                         | IBL reductions, distant-sky sum and three 64-lane exposure suitability reductions.                        | Native product/exposure checks pass with unchanged tolerances; distant sky gains the required offset-2 barrier; shader cost is inspected and measured.   | in_progress |
| S7.6 | [Shared cubemap geometry](../../lld/shader-contracts.md#112-cubemap-geometry); pure coordinate/solid-angle math.                                                                     | IBL processing, environment conversions and forward debug face/UV mapping.                                | Face order, axis ties, seams, orientation and solid angles pass independent references; sky/material images remain qualified.                            | validated   |

These helpers have immediate reuse and useful future consumers:

| Mechanism                    | Future application; outside S7 implementation                                                                         |
| ---------------------------- | --------------------------------------------------------------------------------------------------------------------- |
| Upload and managed views     | Grading/lookup textures, CPU-produced terrain inputs, procedural noise inputs, bloom/terrain/cloud resource setup.    |
| Feedback and retirement      | Terrain generation demand/completion, retained tile or mesh versions, cloud-volume statistics and cached generations. |
| Reductions and cube geometry | Terrain min/max/error summaries, luminance/cloud statistics, probes and cube-based planetary mapping.                 |

S7 does not generalize IblGpuJob/IblWorkBudget, add a scheduler/render graph,
implement terrain/clouds, introduce a universal mip filter, change IBL sample
counts/precision limits, or add UI controls. Existing frame-transient uploads and
retained-output pools remain their current mechanisms. Domain policies stay
local even where two services share storage or transport code.
[`RetainedTexturePool`](../../../../src/Oxygen/Vortex/Internal/RetainedTexturePool.h)
and `SceneTextureLeasePool` retain their per-view lifetime contracts; IBL keeps
its scene-global generations and separately admitted captures.

### S7 execution and exit

1. **Lock the comparison inputs.** Record the actual starting commit, binaries,
   shader archives and relevant S1–S6 evidence. Use the existing fixtures and
   isolated settings; preserve user settings. Update the named helper's module
   CMake source/header lists and test membership in its own commit. Register new
   shared HLSL includes in ShaderBake and existing test-shader dependency lists
   so later helper edits cannot reuse stale bytecode.
2. **Extract with consumers.** Implement the LLD contracts in the order above.
   Remove replaced local bodies in the same item; preserve existing Graphics
   receipts, registration ownership and diagnostic scope attribution. Apply the
   [C++20/23 guidance](../../../../../../design/oxygen/RULES.md#c):
   small value/span interfaces, move-only RAII, explicit errors, narrow templates
   and no new per-dispatch allocation or generic callback/policy framework.
3. **Run focused checks per item.** Use existing CPU/fake/native fixtures. Add
   failure/lifetime cases where the extracted interface creates a new boundary;
   do not build another test platform or repeat unrelated suites. Build/run the
   affected owners in Debug and Release at integration.
4. **Inspect the actual GPU path.** RenderDoc checks both LUT producers and
   consumers after S7.1; delayed/captured IBL and shadow reads after S7.3/4;
   reductions, cube orientation and complete-generation publication after S7.5/6.
   Use the existing analyzers and report resources, barriers and stage use.
   Extend test-side inspection only where a named gate lacks coverage; production
   shader diagnostics are unchanged.
5. **Check cost once the refactor is integrated.** Run existing native Release
   static/runtime/authoring scene workloads (120 warmup + 1,800 frames at 60 Hz)
   and isolated update gates. Preserve [§4.3 limits](../../lld/captured-sky-ibl.md#43-performance-and-latency-gates),
   first-use reporting, zero stable-source work and bounded product/registration
   counts. Record changed CPU recording/poll cost and shader resource/barrier
   statistics. Use Tracy only to explain a measured regression, then recheck
   native timings. No numerical tuning or repeated runs to chase invisible error.
6. **Close the slice.** All six rows validated; no duplicate implementation of
   the extracted operations; the installed SDK's editor-facing include surface
   and actual Interop consumer compile in C++20. Install matching SDK dev/runtime/
   data components and verify the consumed shader archive. Native Release shaders
   contain no qualification probes. Re-run the
   current material/render-path images and a real DemoShell/editor IBL smoke
   through the affected owners. Reuse the accepted migration/scene-image evidence
   where producer inputs and code are unchanged. Update this table, the technical
   owners and the adjacent validation record, then remove the tracker item.

Owning check surfaces, not a new suite hierarchy:

| Items  | Existing check targets / fixtures to extend                                                                                                                                                                                                                                         |
| ------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| S7.1/4 | `Oxygen.Vortex.UploadPlanner.Tests`, `Oxygen.Vortex.UploadCoordinator.Tests`, `Oxygen.Vortex.LightingService.Tests`, `Oxygen.Vortex.ShadowService.Tests`; Graphics managed-registration tests; native `Oxygen.Vortex.LightingGpuAbi.Tests` BRDF/IBL and allocation-budget fixtures. |
| S7.2   | `Oxygen.Vortex.PostProcessService.Tests`, `Oxygen.Vortex.LightingService.Tests`, native `Oxygen.Vortex.Exposure.Tests` IBL/exposure status and `Oxygen.Vortex.LightingGpuAbi.Tests` light-grid fixtures; Graphics readback tests.                                                   |
| S7.3   | `Oxygen.Nexus.Reuse.Tests`, `Oxygen.Nexus.AllocationFailure.Tests`, `Oxygen.Vortex.ShadowService.Tests`, native IBL retirement/queued-pressure and `Oxygen.Vortex.LightingImageReference.Tests` shadow-admission fixtures.                                                          |
| S7.5/6 | ShaderBake production catalog; native `Oxygen.Vortex.LightingGpuAbi.Tests` and `Oxygen.Vortex.Exposure.Tests` products, sky, fog, exposure and images; existing independent CPU references.                                                                                         |

Use the existing `validation.md` for concise results and reproduction commands;
keep reproducible captures and generated artifacts in ignored `out/`. Nexus, Upload, renderer feedback,
Graphics registration and shader contracts each own their reusable capability; this section
owns adopter migration, sequence, state and exit. No parallel refactoring plan or
second progress ledger is introduced.

## S8 — Atmosphere and fog correctness

**Status: planned; implementation has not started.** Complete after S7. The seven
findings below are confirmed by source review; the three numerical examples were
independently reproduced. They are additional cases beyond the S1–S6 qualification.
[EnvironmentLightingService](../../lld/environment-service.md#atmosphere-and-fog-evaluation-contracts)
owns the contracts. S8 changes neither IBL formats/scheduling nor atmosphere's
finite-order multiple-scattering model.

| ID        | Confirmed defect and implementation boundary                                                                                                                                                                                                                    | Regression / exit                                                                                                                                                                                                                                                                     |
| --------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| S8.1 · P1 | `VolumetricFog.hlsl::EvaluateDirectionalContribution` uses two outward directions with the minus-sign HG helper. Negate their dot product; keep the shared helper's convention and other callers intact.                                                        | Through the actual volumetric producer, positive g brightens toward the light, negative g reverses it and g=0 is isotropic. The isolated g=0.8 forward/backward phase ratio is 729; do not impose that ratio on integrated scene pixels.                                              |
| S8.2 · P2 | `DistantSkyLightLutPass.{h,cpp}` / `DistantSkyLightLut.hlsl` gate explicit slots by occupied count. Publish/use per-slot participation, without compacting Primary/Secondary assignments.                                                                       | The same light in slot 0 or slot 1 produces equal distant radiance; none/each/both and disable/re-enable transitions work.                                                                                                                                                            |
| S8.3 · P2 | The distant-sky pass omits `sky_and_aerial_perspective_luminance_factor_rgb`. Apply it once to illuminance, preserving the separate final sky-only factor. Its cache hash already includes both factors.                                                        | Zero/scalar/RGB factors change the real distant product and fog ambient consistently; a live edit rebuilds it, unchanged inputs reuse it. Update CPU/HLSL constants together.                                                                                                         |
| S8.4 · P2 | `ComputeUniformSphereDirection` uses one scalar for both sphere coordinates: mean x=-0.141833 and 14.18% error for L=1+x. Replace it with fixed, two-dimensional equal-area stratification, following UE's 8×8 sampling approach at the existing 64-ray budget. | Constant/linear radiance, first moments and a sun-azimuth sweep against a dense reference expose directional bias. Compare with UE's fixed-seed 64-sample set; preserve temporal determinism and reduction synchronization. Do not increase ray count to chase sub-percent agreement. |
| S8.5 · P2 | `AerialPerspective.hlsli` adds the orthographic near-plane offset. Subtract it so distance is measured from the producer's per-pixel ray origin.                                                                                                                | A point 1,000 m sideways and 100 m forward with near=1 m samples 99 m, not 2,002.55 m. Exercise centered/edge pixels, translated/rotated views, both depth conventions and unchanged perspective behavior.                                                                            |
| S8.6 · P2 | `ComputeAerialPerspectiveLut` returns neutral transmittance below strength=0.0001. Remove this semantic discontinuity: strength scales added radiance only.                                                                                                     | At strength 0, just below/above the old threshold and 1, sampled T is unchanged and RGB scales continuously. Verify deferred and shared forward/translucent composition.                                                                                                              |
| S8.7 · P2 | `Sky.hlsl` ignores published atmosphere holdout and forces alpha=1, although the sky LUT already stores transmittance. Implement the approved coverage behavior below using existing `SkyPass` blending.                                                        | Check sky/disks, geometry AP, independent height fog, nonblack background and captured IBL. Ordinary sky output stays unchanged.                                                                                                                                                      |

**Approved visible-sky holdout:** zero
atmosphere/disk RGB and coverage `1-T_atmosphere`; retain AP extinction. Compose
independent height fog normally, yielding `L_fog` and coverage
`1-T_fog*T_atmosphere`. Holdout must not darken captured IBL. Oxygen already uses
premultiplied coverage; no new global alpha mode is needed. All seven fixes are
included. Further changes to quality, runtime cost or this contract require approval
before implementation.

Execution uses three focused batches: volumetric phase; distant-sky slots/factors/
sampling; AP/holdout composition. Extend `EnvironmentLightingService_test.cpp`
and the existing native `Exposure/SkyRadiance_test.cpp`, `FogComposition_test.cpp`
and relevant volumetric fixtures, with test-only probes outside `shaders.bin`.
Use `Oxygen.Vortex.EnvironmentLightingService.Tests` and
`Oxygen.Vortex.Exposure.Tests`; run affected CPU/HLSL layout and production shader
build checks. Format before builds and batch oxytidy across touched C++ files.

Validate each batch through the production producer/consumer, then run one final
Debug/Release affected-test pass and a representative visible-sky/fog scene.
Use RenderDoc only where bindings, coverage or production inputs need inspection;
keep captures local. Measure warmed Release distant-sky/fog/AP GPU costs at fixed
settings before/after; use Tracy only to investigate a material regression. Preserve
64 sky rays, LUT sizes, dispatch count and existing numerical formats. The zero-
strength AP fix necessarily retains the texture sample needed for extinction.
Any additional runtime cost or unresolved visible error requires a concrete
quality/cost choice before expanding this scope.

Keep only the compact comparison (configuration, revision, affected-pass timings
and correctness results) in `validation.md`. Tests and commands provide repeatable
proof; do not archive logs, captures or source/binary bundles.

## DemoShell UI

Update `EnvironmentDebugPanel.cpp`, `EnvironmentVm.*`,
`EnvironmentSettingsService.*` and `SkyboxService.*` with their existing tests:

- Replace `Captured Scene (Unavailable)` and the unsupported-IBL messages with
  the implemented source and actual status. Preserve native specified-cubemap
  selection and its resource/error feedback.
- Keep Enabled/intensity primary. Use existing advanced controls for tint,
  diffuse, hemisphere and volumetric contribution; expose the existing specular
  multiplier and AffectReflections accessors when their consumers are active.
- Remove the retired SkyLight capture bool from VM/service/settings/startup
  routes. Keep fog capture visibility, labelled for its effect on sky lighting.
  Source, light and fog UI edits carry immediate authoring intent; runtime
  animation uses incremental scheduling automatically.
- During incremental work keep the valid lighting visible. Show concise updating
  or failure feedback where needed; generation IDs, age and timings belong in
  existing diagnostics. Do not show normal refresh as unsupported/unavailable.
- Qualify real widget enable/disable, source switch, light/fog drags, independent
  diffuse/specular/reflection edits, reset/save/load/reopen and scene replacement.
  Reuse the existing widget-test integration; add no new test platform or UI panel.

## Validation tools

| Tool / checkpoint                           | What to inspect                                                                                                                                                                                                  | Retained result                                                                                                                                               |
| ------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| RenderDoc — S1/S2 first native result       | Six faces, formats/mips, SH/BRDF/metadata, linear HDR scale, fog-only and disk-hidden inputs; producer barriers and consumers; Stage 13 contributes once and the Stage 12 bridge is absent.                      | Annotated capture plus scripted product/pixel checks for the diffuse and roughness references.                                                                |
| RenderDoc — S3/S4 scheduling/lifetime       | Representative authoring, intermediate incremental and final-publish frames; frozen generation/LUT bindings, complete old set while updating, atomic new set, view/scene replacement and retained-capture reads. | Generation-labelled captures and the matching native lifecycle test results. Fault injection/retirement tests remain necessary beyond a single-frame capture. |
| Native Release timing — S4/S6 acceptance    | Matched static, continuous sun/fog and authoring-drag workloads using existing built-in GPU timestamps; update interval unions, whole-frame p95/p99, source age, creation counts and retained bytes.             | Non-Tracy, non-RenderDoc acceptance results against the design's budgets.                                                                                     |
| Tracy — S3/S4/S6 diagnosis and optimization | CPU invalidation/snapshot/recording/submission, queue waits, GPU capture/fog/reduction/mips/SH/prefilter, publication and allocation churn. Identify the dominant stage before changing it.                      | Matched baseline/candidate trace and attribution, then recheck correctness and native Release timings for the chosen correction.                              |

Use `CpuProfileScope` / `GpuEventScope` through the
[profiling guide](../../../profiling/profiling-developer-guide.md); keep stable
telemetry scopes for the update and diagnostic scopes for its phases. Reuse
`out/build-ninja` for native Release acceptance and `out/build-tracy-ninja` for
Tracy attribution. Freeze scene/settings/shader/build identities between paired
runs, run GPU workloads serially and keep diagnostic readback outside timed work.
RenderDoc replay timings and Tracy-instrumented FPS are not the acceptance metric.

Use the existing [RenderScene capture CLI](../../../../Examples/RenderScene/DEVELOPMENT.md#gpu-captures-and-native-screenshots)
and [Vortex analysis helpers](../../../../tools/vortex/README.md). Choose captures
from the required generation/readiness state, rather than a hard-coded warmup
frame. Extend those tools only for the named IBL checks; do not launch a general
benchmark, capture or telemetry framework project.

## Acceptance

| Gate                     | Required result                                                                                                                                                                                                                          | Result and evidence                                                                                                                                                                                                                                                                      |
| ------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Products                 | Independent constant/directional/HDR cube references, SH normalization, GGX/BRDF mapping, seams and radiance-scale checks pass at the prescribed formats and sample counts.                                                              | Pass — [independent SH/GGX/BRDF](evidence/s1-reuse/run.json), [HDR/texels](evidence/s2-precision/run.json), [native filtering](evidence/s2-half-admission/run.json).                                                                                                                     |
| Fog and appearance       | Atmosphere-only, fog-only, combined and disabled sources; both layers, fixed far-ray and visibility/distance controls; disks do not gate fog scattering; sky fog applied once with existing opaque fog preserved.                        | Pass — [shared fog](evidence/s2-height-fog/run.json), [both-light capture](evidence/s2-atmosphere-source/run.json), [town4new images](evidence/s5-native-appearance/run.json).                                                                                                           |
| Lighting                 | Dielectrics and glossy/rough metals; independent diffuse/specular controls; both atmosphere lights; normal mapping/sidedness; forward/deferred/translucent agreement.                                                                    | Pass — [material/control cases](evidence/s2-surfaces/run.json), [144 render-path cases](evidence/s5-path-images/run.json), [editor/standalone images](evidence/s5-editor-images/run.json).                                                                                               |
| Scheduling               | Same-frame authoring, bounded runtime source age, steady generation progress under nonstop edits, final-key convergence, immediate preemption and no mixed-generation products.                                                          | Pass — [edit/cache identity](evidence/s3-cache-identity/run.json), [frozen jobs](evidence/s4-jobs/run.json), [convergence/preemption](evidence/s4-scheduling/run.json), [matched workloads](evidence/s4-scene/run.json).                                                                 |
| Performance              | Meet every [reference GPU/latency/resource gate](../../lld/captured-sky-ibl.md#43-performance-and-latency-gates). Record first use, static reuse, continuous sun/fog animation and authoring drag workloads; preserve filtering quality. | Pass — [128-face reference gates](evidence/s4-scene/run.json), [first use and pressure](evidence/s4-update-qualification/run.json), [specified-size scaling and source audit](evidence/s6-integrated/run.json).                                                                          |
| Lifetime and integration | Bounded products/descriptors under preemption and pinned captures; capture-busy admission; CPU failures versus GPU-invalid output; retry/teardown/shared-view ownership; no Stage 12 indirect contribution.                              | Pass — [capture admission](evidence/s3-capture-admission/run.json), [fault/teardown](evidence/s3-lifetime-faults/run.json), [allocation rollback](evidence/s3-producer-allocation/run.json), [GPU-invalid output](evidence/s5-metadata/run.json).                                        |
| Migration and operation  | Retired bool rejected after migration; maintained scenes/settings recooked; DemoShell and editor workflows/screenshots qualify; ordinary builds run without qualification instrumentation.                                               | Pass — [reject/migrate/recook](evidence/s2-migration/run.json), [native lifecycle](evidence/s5-lifecycle/run.json), [editor workflows](evidence/s5-editor/run.json), [Release build](evidence/s5-editor-pack/run.json), [ordinary-path audit](evidence/s6-integrated/source-audit.json). |

Qualify town4new with matched camera, sky and fixed exposure: shadowed exterior
façades retain material detail while direct-sun shadow contrast remains visible.

Run focused native/CPU tests as each slice lands, shader/catalog checks for
changed HLSL/ABI, and Debug/Release owning gates at integration. Use the existing
GPU profiling/capture tools for numerical images, barriers, timings and resource
accounting. Record actual commands, identities and results as work completes.
Add one adjacent `validation.md` when results exist; put raw artifacts in its
`evidence/` directory. This README remains the milestone's scope and status owner.
