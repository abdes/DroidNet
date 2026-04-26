# Vortex Implementation Status

Status: `active milestone ledger`

This file records Vortex implementation status at milestone granularity. It is
not a commit log. Each milestone gets one ledger item that is updated in place
with the current implementation evidence, validation evidence, and residual
gap.

## 1. Ledger Rules

1. Do not add per-commit evidence rows.
2. Update the existing milestone row when work advances.
3. Keep evidence concise: implementation files/areas, validation commands or
   artifact names, and the remaining exit-gate gap.
4. Do not mark a milestone `validated` unless implementation exists, required
   docs/plans are current, and validation evidence is stated in the row.
5. If scope changes, update the design document and detailed milestone plan
   before claiming progress.

## 2. Status Vocabulary

| Status | Meaning |
| --- | --- |
| `validated` | Implementation exists, docs/status are current, and fresh validation evidence is recorded here. |
| `landed_needs_validation` | Substantial code exists, but this ledger does not contain fresh closure proof. |
| `in_progress` | Implementation exists but known gaps remain, or scope is actively being corrected. |
| `planned` | Scope is defined; implementation has not started for this milestone. |
| `blocked` | Required design, dependency, or proof surface is missing. |
| `future` | Explicitly deferred beyond the production-complete desktop deferred baseline. |

## 3. Current Next Step

**Next milestone:** `VTX-M05B - Occlusion Consumer Closeout`

**Why:** `VTX-M05A` is validated. The next planned gate is closing the stage-5
occlusion consumer path over the existing generic Screen HZB implementation.

**Active work package:** `VTX-M05B - Occlusion Consumer Closeout`

**Plan:** [plan/VTX-M05B-occlusion-consumer-closeout.md](./plan/VTX-M05B-occlusion-consumer-closeout.md)
defines the detailed work slices. The design authority is
[lld/occlusion.md](./lld/occlusion.md), with [lld/hzb.md](./lld/hzb.md) as the
existing Screen HZB producer contract.

**Current M05B scope:** full occlusion/query/visibility policy over Screen HZB,
including visibility classifications, batching, temporal handoff, consumers, and
proof.

**Current M05B status:** `in_progress`.

## 4. Milestone Ledger

| ID | Milestone | Status | Current Evidence | Missing To Close |
| --- | --- | --- | --- | --- |
| VTX-M00 | Planning and status truth surface | `validated` | `PLAN.md` was rewritten as a milestone-first plan; this milestone/status ledger exists; restricted doc scans and `git diff --check` passed on 2026-04-25. | No open planning-status gap. |
| VTX-M01 | Renderer Core and SceneRenderer baseline | `landed_needs_validation` | Vortex module, Renderer Core, publication, upload/resource substrate, SceneRenderer shell, SceneTextures, non-runtime facades, resolve/cleanup, and related tests are present in source. | Fresh build/test evidence and any required parity proof are not recorded here. |
| VTX-M02 | Deferred core visual path | `landed_needs_validation` | InitViews, depth prepass, generic Screen HZB, base pass/GBuffer/velocity, Stage 10 publication, deferred lighting, shader families, debug views, and focused tests/tools are present. | Fresh build, ShaderBake/catalog validation, tests, and capture/analyzer proof are not recorded here. |
| VTX-M03 | Migration-critical non-environment services | `landed_needs_validation` | LightingService, directional ShadowService baseline, PostProcessService, Stage 8/12/22 routing, and focused tests are present. Directional conventional CSM projected shadows were later validated as part of the M04D.4 blocker work. | Service-specific fresh validation, RenderDoc proof, and residual parity review are not recorded for the full M03 milestone. Spot/point/local-light shadows remain future work. |
| VTX-M04D | Environment / fog parity closure | `validated` | VTX-M04D.1 through VTX-M04D.6 validated environment publication truth, UE5.7-shaped height fog, local fog volumes, volumetric fog, consolidated environment runtime proof, and main-view aerial perspective. Evidence includes focused Vortex builds/tests, ShaderBake/catalog validation where shaders changed, VortexBasic and RenderScene RenderDoc proof, CDB/D3D12 debug-layer audits where required, and user visual confirmation for projected shadows and city scene shadows. | Real SkyLight cubemap capture/filtering remains a separate IBL/indirect-lighting resource implementation before usable IBL output can be claimed. Reflection/360 AP remains deferred to the future reflection-capture resource path. |
| VTX-M04D.1 | Environment publication and sky/fog contract truth | `validated` | EnvironmentLightingService sanitizes IBL probe bindings, distinguishes authored SkyLight from usable IBL resources, keeps invalid SkyLight/volumetric products explicit, exposes Stage 14 local-fog state through SceneRenderer, and focused EnvironmentLightingService/SceneRendererPublication tests passed on 2026-04-25. | Real SkyLight capture/filtering and fog parity are outside VTX-M04D.1 and tracked by later milestones. |
| VTX-M04D.2 | UE5.7 exponential height fog parity | `validated` | CPU/HLSL height-fog payload, authored layer translation, analytic `HeightFogCommon`-shaped line integral, sky-depth exclusion, DemoShell/RenderScene controls, focused build/tests, ShaderBake/catalog validation, focused VortexBasic enabled/disabled RenderDoc proof, and city-scale RenderScene capture proof are recorded. | Cubemap inscattering resource binding/sampling remains explicitly unavailable and deferred. |
| VTX-M04D.3 | UE5.7 local fog volume parity | `validated` | Analytical local-fog volume path is implemented and proven: UE5.7 source mapping, authoring sanitization, sorting/capping, HZB-backed tiled culling, UE-shaped single draw-indirect rendering, analytical shader path, SceneColor contribution, far-depth no-op behavior, focused tests, ShaderBake/catalog validation, VortexBasic runtime/capture proof, and focused RenderDoc draw-args probe. | Local-fog participating-media injection into volumetric fog is validated under VTX-M04D.4, not VTX-M04D.3. |
| VTX-M04D.4 | UE5.7 volumetric fog parity | `validated` | Stage-14 compute volumetric fog allocates and publishes `IntegratedLightScattering`, Stage 15 composes it, and proof covers captured fog payload/SRV/grid state, volume min/max/slices, directional CSM shadowed-light sampling, local-fog participating-media injection, Oxygen distant-SkyLight volumetric ambient, temporal jitter/reset/history-miss reprojection, city-scale `CityEnvironmentValidation`, and D3D12 debug-layer audit. Directional CSM projected-shadow blocker was closed with RenderDoc proof and user visual confirmation. | Accepted Oxygen divergence: single integrated-scattering temporal product without UE conservative-depth history fixup or pre-exposure transfer. Real SkyLight cubemap capture/filtering remains outside this milestone. |
| VTX-M04D.5 | Environment runtime proof and Async preparation | `validated` | `Run-VortexBasicRuntimeValidation.ps1` builds VortexBasic, runs a CDB/D3D12 debug-layer audit, captures RenderDoc frame 5, and asserts one runtime path with atmosphere, main-view AP, height fog, local fog, volumetric fog, authored SkyLight unavailable state, and SkyLight volumetric injection. Focused EnvironmentLightingService tests passed. | Real SkyLight cubemap capture/filtering remains a later IBL/resource gap. Async proof is validated separately by VTX-M04E. |
| VTX-M04D.6 | UE5.7 aerial perspective parity | `validated` for main-view AP | Vortex AP sampling uses UE5.7-shaped camera-volume lookup helpers, preserves raw camera-volume generation with apply-time strength control, exposes effective DemoShell/VortexBasic AP controls, fixes city-scale reversed-Z far-depth AP composition, and has focused enabled/disabled plus city-scale RenderDoc proof. | Reflection/360-view AP is explicitly deferred to the future reflection-capture resource path. |
| VTX-M04E | Async migration parity gate | `validated` | Async authors a Vortex-ready scene environment, sun directional light, shadow receiver ground, lifted sphere/two-submesh geometry, and Vortex runtime view metadata. DemoShell no longer overrides the scene-authored environment. `Run-AsyncRuntimeValidation.ps1` passed with RenderDoc structural/product proof, final present proof, and overlay composition proof on 2026-04-26. | No open M04E closure gap. |
| VTX-M04F | Single-view composition and presentation closeout | `validated` | Runtime composition registration, queued single-view composition copy, Stage 22 post-process routing, and overlay blend path exist. `AnalyzeRenderDocAsyncProducts.py` proves exactly one post-Stage-22 composition copy from `Async.SceneColor`, exactly one overlay blend after scene copy, final present output, and focused `RendererCompositionQueue` tests passed. | No open M04F closure gap. |
| VTX-M05A | Diagnostics product service | `validated` | Diagnostics runtime service, authoritative shader-debug registry, frame ledger, capture manifest, GPU timeline facade, Diagnostics panel, proof script hardening, direct/masked debug corrections, unsupported IBL/light-culling mode handling, and solid/wireframe/wireframe-overlay render controls are implemented and documented in the LLD/plan. Validation evidence: focused Vortex/DemoShell builds and tests, release DiagnosticsService test, VortexBasic runtime RenderDoc/debug-layer proof, TexturedCube panel-registration smoke, RenderScene build/ShaderBake validation, and user visual confirmation for solid plus wireframe overlay. | No open M05A closure gap. Optional GPU debug primitives remain deferred until a concrete proof gap requires them. |
| VTX-M05B | Occlusion consumer closeout | `in_progress` | Generic Screen HZB implementation and bindings exist; M05B LLD/plan now map UE5.7 `FHZBOcclusionTester`/`HZBOcclusion.usf` behavior onto a Vortex-native HZB consumer over `ScreenHzbModule`. | Implement result substrate, HZB tester pass/readback, consumer filtering, diagnostics, ShaderBake/catalog validation, runtime/capture proof, and D3D12 debug-layer evidence. |
| VTX-M05C | Translucency stage | `planned` | Directory placeholders and LLD exist. | Stage 18 implementation consuming lighting/shadow/environment publications and validation proof. |
| VTX-M05D | Local-light conventional shadow expansion | `planned` | Directional ShadowService baseline exists; local-light LLD exists. | Spot-light shadows, point-light strategy, publication, capture proof, and debug-layer evidence. |
| VTX-M06A | Multi-view proof closeout | `planned` | CompositionView/ViewLifecycle/CompositionPlanner substrate exists. | Heterogeneous per-view runtime proof, PiP/multi-surface routing, and per-view product isolation. |
| VTX-M06B | Offscreen proof closeout | `planned` | `ForOffscreenScene` API and presets exist. | Focused tests and runtime/capture validation for offscreen deferred/forward scenarios. |
| VTX-M06C | Feature-gated runtime variants | `planned` | Capability vocabulary and many services exist. | Depth-only, shadow-only, no-environment, no-shadowing, no-volumetrics, diagnostics-only validation. |
| VTX-M07 | Production readiness and legacy retirement | `planned` | Not applicable yet. | All required examples/tests ported, legacy renderer retirement plan, and production-readiness proof. |
| VTX-FUTURE | Reserved post-baseline families | `future` | Some placeholder directories or shader inventory may exist. | Geometry virtualization, material composition, indirect lighting/GI/reflections, VSM, clouds, heterogeneous volumes, water, hair, distortion, light shafts. |

## 5. Open Cross-Milestone Gaps

| Gap | Blocks | Required Resolution |
| --- | --- | --- |
| No fresh validation recorded in this ledger for older landed systems. | Any `validated` status for VTX-M01 through VTX-M03 as whole milestones. | Run focused builds/tests and record concise milestone-row evidence. |
| Real SkyLight capture/filtering is not implemented. | Future SkyLight lighting parity and downstream image-based lighting quality. | Implement usable cubemap capture/filtering resources and update the relevant future IBL/indirect-lighting milestone. |
| Reflection/360-view aerial perspective lacks a runtime resource path. | Future reflection-capture parity. | Implement with the future reflection-capture resource path before claiming reflection/360 AP parity. |

## 6. Update Checklist

Before changing a milestone status:

1. Update the milestone's design and detailed plan if scope changed.
2. Update exactly one row in the milestone ledger.
3. State implementation evidence, validation evidence, and residual gap in that
   row.
4. Run `git diff --check` for docs-only edits, and the relevant focused
   build/test/runtime proof for implementation edits.
