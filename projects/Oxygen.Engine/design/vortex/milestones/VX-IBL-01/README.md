# VX-IBL-01 — Captured sky lighting

Status: `in_progress`

| Field     | Summary                                                                                                                                                                                                                |
| --------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Outcome   | Target: atmosphere/height-fog IBL with diffuse/specular lighting, immediate authoring and budgeted runtime updates.                                                                                                    |
| Remaining | [VX-IBL-01](../../OPEN_ITEMS.md#p1--current-delivery): S1–S4 validated. Remaining: editor workflows and integrated qualification.                                                                                      |
| Evidence  | [Current checks](validation.md): matched static/runtime/authoring scene gates and queued preemption pass. Native widgets and GPU validity pass; editor workflows and [integrated acceptance](#acceptance) remain open. |

Read: [scope](#scope-and-ownership), [delivery sequence](#delivery-sequence),
[DemoShell UI](#demoshell-ui), [validation tools](#validation-tools),
[acceptance](#acceptance). The [IBL design](../../lld/captured-sky-ibl.md) owns
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
| VX-IBL-01.S5 | DemoShell panel/VM/settings changes below, native/editor visual scenarios, and existing diagnostics for generation age, CPU/GPU costs and failures.                                                                                                                               | Deferred/forward/translucent IBL, multi-view and offscreen images agree. Real DemoShell widgets exercise immediate edits, reset/load/save, source changes, fog participation and stable status; editor controls use the same owners.                                    | in_progress |
| VX-IBL-01.S6 | Integrated correctness, performance and resource qualification; update owner designs and operating guidance to delivered behavior.                                                                                                                                                | Every acceptance row below has its result and evidence link; both schedules and the canonical migration are complete.                                                                                                                                                   | planned     |

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

| Gate                     | Required result                                                                                                                                                                                                                          |
| ------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Products                 | Independent constant/directional/HDR cube references, SH normalization, GGX/BRDF mapping, seams and radiance-scale checks pass at the prescribed formats and sample counts.                                                              |
| Fog and appearance       | Atmosphere-only, fog-only, combined and disabled sources; both layers, fixed far-ray and visibility/distance controls; disks do not gate fog scattering; sky fog applied once with existing opaque fog preserved.                        |
| Lighting                 | Dielectrics and glossy/rough metals; independent diffuse/specular controls; both atmosphere lights; normal mapping/sidedness; forward/deferred/translucent agreement.                                                                    |
| Scheduling               | Same-frame authoring, bounded runtime source age, steady generation progress under nonstop edits, final-key convergence, immediate preemption and no mixed-generation products.                                                          |
| Performance              | Meet every [reference GPU/latency/resource gate](../../lld/captured-sky-ibl.md#43-performance-and-latency-gates). Record first use, static reuse, continuous sun/fog animation and authoring drag workloads; preserve filtering quality. |
| Lifetime and integration | Bounded products/descriptors under preemption and pinned captures; capture-busy admission; CPU failures versus GPU-invalid output; retry/teardown/shared-view ownership; no Stage 12 indirect contribution.                              |
| Migration and operation  | Retired bool rejected after migration; maintained scenes/settings recooked; DemoShell and editor workflows/screenshots qualify; ordinary builds run without qualification instrumentation.                                               |

Qualify town4new with matched camera, sky and fixed exposure: shadowed exterior
façades retain material detail while direct-sun shadow contrast remains visible.

Run focused native/CPU tests as each slice lands, shader/catalog checks for
changed HLSL/ABI, and Debug/Release owning gates at integration. Use the existing
GPU profiling/capture tools for numerical images, barriers, timings and resource
accounting. Record actual commands, identities and results as work completes.
Add one adjacent `validation.md` when results exist; put raw artifacts in its
`evidence/` directory. This README remains the milestone's scope and status owner.
