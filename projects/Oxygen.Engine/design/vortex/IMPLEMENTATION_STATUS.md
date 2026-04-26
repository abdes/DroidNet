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

**Next milestone:** `VTX-M05A - Diagnostics Product Service`

**Why:** The Phase 4D environment/fog parity packages, Async migration proof,
and single-view composition/presentation closeout now have implementation and
recorded validation. The next gate is making diagnostics a product-owned
service instead of relying on proof tooling, scattered debug vocabulary, and ad
hoc UI controls.

**Active work package:** `VTX-M05A - Diagnostics Product Service`

**Plan:** [plan/VTX-M05A-diagnostics-product-service.md](./plan/VTX-M05A-diagnostics-product-service.md)

**Current M05A scope:** runtime `DiagnosticsService`, authoritative
`ShaderDebugModeRegistry`, frame ledger, GPU timeline facade with Tracy kept
behind the profiling/backend layer, minimal recoverable runtime issues, capture
manifest export, DemoShell diagnostics panel registry, automation hardening for
M05A-owned or touched proof scripts, and optional GPU debug primitives only if a
required spatial-debug proof gap demands them.

**Current M05A status:** `in_progress`. The LLD and detailed plan are updated.
Runtime slices now include the DiagnosticsService shell, diagnostics types,
renderer-owned lifetime, shader-debug forwarding, authoritative
ShaderDebugModeRegistry exposure, the core frame ledger with bounded
recoverable issues, major SceneRenderer pass/product writer hooks, the GPU
timeline facade over the existing profiler, and capture manifest export. Panel
integration, proof script hardening, runtime capture, ShaderBake, and
debug-layer evidence remain open until their slices land.

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
| VTX-M05A | Diagnostics product service | `in_progress` | LLD/plan define the compact diagnostics toolkit. Runtime slices landed `DiagnosticsTypes`, `DiagnosticsService`, `ShaderDebugModeRegistry`, `DiagnosticsFrameLedger`, and `DiagnosticsCaptureManifest`, with CMake/test wiring, renderer-owned service lifetime, shader-debug forwarding, feature flags using `OXYGEN_FLAG`/`OXYGEN_DEFINE_FLAGS_OPERATORS`, enum `to_string` helpers, capability clamping, frame begin/end snapshots, pass/product/issue recording, disabled-ledger no-op behavior, canonical debug-mode names/families/paths/defines/requirements, service-level registry enumeration/resolution, VortexBasic CLI parsing through the registry, bounded recoverable issue context, per-frame issue deduplication/occurrence counts, fail-fast record contract checks, explicit minimal issue codes for feature unavailable, manifest write failed, timeline overflow/incomplete scope, unsupported debug mode, missing debug-mode product, and stale product, SceneRenderer writer hooks for major stage/product truth at existing publication boundaries, a GPU timeline facade over the existing profiler for enablement, max scopes, latest-frame retention, sinks, one-shot export, latest-frame access, snapshot state, and profiler diagnostic conversion, plus `vortex.diagnostics.capture-manifest.v1` JSON/file export that omits transient descriptor indices. DemoShell Diagnostics panel work replaces the Vortex runtime-facing `RenderingPanel`/`RenderingVm` surface with `DiagnosticsPanel`/`DiagnosticsVm`, renames example panel config to `diagnostics`, drives Diagnostics and light-culling debug-mode UI from `ShaderDebugModeRegistry`, persists requested shader debug mode separately from the effective renderer mode, and displays requested/effective state plus renderer capabilities. The registry explicitly records `ibl-no-brdf-lut` as unsupported until `SKIP_BRDF_LUT` runtime selection is wired. Validation passed: `cmake --build out\build-ninja --config Debug --target oxygen-examples-vortexbasic Oxygen.Vortex.DiagnosticsService Oxygen.Vortex.ShaderDebugModeRegistry --parallel 4`; `ctest --preset test-debug -R "Oxygen\.Vortex\.(ShaderDebugModeRegistry|DiagnosticsService)" --output-on-failure` with DiagnosticsService 7/7 and ShaderDebugModeRegistry 7/7; `cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.DiagnosticsFrameLedger Oxygen.Vortex.DiagnosticsService --parallel 4`; `ctest --preset test-debug -R "Oxygen\.Vortex\.(DiagnosticsFrameLedger|DiagnosticsService)" --output-on-failure` with DiagnosticsFrameLedger 5/5 and DiagnosticsService 7/7; `cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.DiagnosticsService Oxygen.Vortex.SceneRendererPublication --parallel 4`; `ctest --preset test-debug -R "Oxygen\.Vortex\.(SceneRendererPublication|DiagnosticsService|DiagnosticsFrameLedger)" --output-on-failure` with SceneRendererPublication 17/17, DiagnosticsService 7/7, and DiagnosticsFrameLedger 5/5; `cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.DiagnosticsService Oxygen.Vortex.GpuTimelineProfiler.Tests Oxygen.Vortex.DiagnosticsFrameLedger --parallel 4`; `ctest --preset test-debug -R "Oxygen\.Vortex\.(DiagnosticsService|GpuTimelineProfiler|DiagnosticsFrameLedger)" --output-on-failure` with DiagnosticsService 8/8, GpuTimelineProfiler 8/8, and DiagnosticsFrameLedger 5/5; `cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.DiagnosticsCaptureManifest Oxygen.Vortex.DiagnosticsService --parallel 4`; `ctest --preset test-debug -R "Oxygen\.Vortex\.(DiagnosticsCaptureManifest|DiagnosticsService)" --output-on-failure` with DiagnosticsCaptureManifest 2/2 and DiagnosticsService 8/8; `cmake --build out\build-ninja --config Debug --target oxygen-examples-demoshell Oxygen.Examples.DemoShell.RenderingSettingsService.Tests --parallel 4`; `ctest --preset test-debug -R "Oxygen\.Examples\.DemoShell\.RenderingSettingsService" --output-on-failure` with RenderingSettingsService 6/6; `cmake --build out\build-ninja --config Debug --target oxygen-examples-async oxygen-examples-lightbench oxygen-examples-physics oxygen-examples-renderscene oxygen-examples-texturedcube --parallel 4`. | Release/`NDEBUG` default policy is implemented but not proven in a release build. Diagnostics panel runtime UI smoke, hardened touched proof wrappers, runtime/capture/debug-layer evidence, direct `EngineShaderCatalog.h` introspection, and any ShaderBake evidence for later shader-visible changes remain open. |
| VTX-M05B | Occlusion consumer closeout | `planned` | Generic Screen HZB implementation and bindings exist. | Full occlusion query/consumer policy, visibility classifications, batching, temporal handoff, and proof. |
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
| Diagnostics product surface is not implemented. | M05A and later observability. | Land the M05A service surface without confusing proof tooling with runtime diagnostics. |

## 6. Update Checklist

Before changing a milestone status:

1. Update the milestone's design and detailed plan if scope changed.
2. Update exactly one row in the milestone ledger.
3. State implementation evidence, validation evidence, and residual gap in that
   row.
4. Run `git diff --check` for docs-only edits, and the relevant focused
   build/test/runtime proof for implementation edits.
