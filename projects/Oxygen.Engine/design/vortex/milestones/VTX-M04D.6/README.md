# VTX-M04D.6 — UE5.7 aerial perspective parity

Status: `validated`

| Field     | Summary                                                                   |
| --------- | ------------------------------------------------------------------------- |
| Outcome   | Main-view aerial-perspective sampling and composition.                    |
| Remaining | Extensions: [VX-AP-01](../../OPEN_ITEMS.md#p3--unscheduled-capabilities). |
| Evidence  | [Validation record](validation.md)                                        |

[Roadmap](../../PLAN.md) · [Design index](../../lld/README.md)

Dependencies: VTX-M04D.1, VTX-M04D.2

## Delivered scope

Main-view camera aerial-perspective volume generation/sampling, main-pass application, height-fog coupling boundary, focused enabled/disabled proof, and city-scale `CityEnvironmentValidation` capture proof. Reflection/360-view AP resource behavior is explicitly deferred to the future reflection-capture resource path.

## Scope and acceptance

Required work:

- Audit Vortex camera aerial-perspective generation, sampling, and main-pass
  application against UE5.7 `SkyAtmosphere` and base-pass usage.
- Replace stale comments or implementation shortcuts that still describe the
  removed midpoint Beer-Lambert approximation or imply unverified parity.
- Verify camera-volume depth mapping, start-depth behavior, slice-center
  sampling, transmittance storage, exposure handling, orthographic behavior,
  and per-view resource validity. Reflection-capture or 360-view AP resource
  behavior is explicitly deferred to the future reflection-capture resource
  path.
- Define and implement the height-fog coupling contract used when aerial
  perspective is composed with exponential height fog.
- Propagate any AP-driven contract, binding, or shader-behavior changes back
  into VTX-M04D.1 publication truth and VTX-M04D.2 height-fog parity, with
  updated tests and status evidence. AP fixes must not silently invalidate the
  validated publication baseline or the height-fog implementation evidence.
- Add focused tests and shader/capture evidence for enabled, disabled,
  authored parameter changes, resource-unavailable states, and main-pass
  application.

[Results](validation.md#scope-and-acceptance--results).

Exit gate:

- Aerial perspective has a source-to-target mapping to the relevant UE5.7
  files, implementation deltas are resolved or explicitly accepted as Vortex
  differences, any impact on VTX-M04D.1 and VTX-M04D.2 is implemented and
  revalidated, focused tests pass, shader bake/catalog validation runs where
  shader behavior changes, and runtime/capture proof records that camera
  aerial perspective affects the expected pixels without replacing height fog,
  local fog, volumetric fog, or SkyLight proof.

Status: `validated` for main-view camera aerial perspective.

This plan covers Vortex aerial perspective only. It is a scope-correction
milestone created after the VTX-M04D.2 height-fog work removed the old
simplified midpoint Beer-Lambert fog approximation from the aerial-perspective
shader path. The existing implementation is preserved behavior, not verified
UE5.7 aerial-perspective parity.

## Reader And Action

The reader is an internal Vortex engineer. After reading this plan, they should
be able to implement and validate aerial-perspective parity without reopening
height fog, local fog, volumetric fog, SkyLight capture/filtering, or legacy
renderer fallback work.

## Scope

- Audit Vortex camera aerial-perspective LUT generation, resource publication,
  sampling, and main-pass application against UE5.7.
- Preserve VTX-M04D.1 publication truth and VTX-M04D.2 height-fog shader
  cleanup; do not reintroduce an aerial-perspective fog approximation.
- Verify or fix camera-volume depth mapping, start depth, slice-center
  addressing, transmittance storage, exposure handling, orthographic behavior,
  per-view validity, and resource-unavailable behavior.
- Define the explicit height-fog coupling contract for aerial perspective,
  including when fog is already baked into camera aerial perspective and when
  Vortex composes height fog separately.
- Propagate every AP-driven contract, binding, publication, or shader-behavior
  change back into VTX-M04D.1 publication truth and VTX-M04D.2 height-fog
  parity. The implementation must update affected tests and status evidence
  instead of treating AP as an isolated shader cleanup.
- Add focused tests and runtime/capture evidence for enabled, disabled,
  authored settings, resource state, and main-pass pixel impact.
- Reflection/360-view AP resource behavior is explicitly deferred to the
  future reflection-capture resource path. VTX-M04D.6 does not claim that path.

## Out Of Scope

- Exponential height fog parity beyond the coupling boundary already tracked by
  VTX-M04D.2.
- Local fog volume parity.
- Volumetric fog parity.
- Real SkyLight capture/filtering.
- Async runtime migration proof.
- DiagnosticsService implementation.
- Legacy renderer fallback.
- Reflection/360-view AP resource generation and capture proof.

## UE5.7 Grounding

Implementation and review must check the relevant UE5.7 references:

- `F:\Epic Games\UE_5.7\Engine\Shaders\Private\SkyAtmosphere.usf`
- `F:\Epic Games\UE_5.7\Engine\Shaders\Private\SkyAtmosphereCommon.ush`
- `F:\Epic Games\UE_5.7\Engine\Shaders\Private\BasePassPixelShader.usf`
- `F:\Epic Games\UE_5.7\Engine\Shaders\Private\BasePassVertexShader.usf`
- `F:\Epic Games\UE_5.7\Engine\Source\Runtime\Renderer\Private\SceneRendering.cpp`
- `F:\Epic Games\UE_5.7\Engine\Source\Runtime\Renderer\Private\SkyAtmosphereRendering.cpp`

The minimum source-to-target mapping must cover UE5.7 camera aerial-perspective
volume generation, base-pass sampling/application, view uniform publication,
height-fog contribution controls, and dummy/invalid resource behavior.

## Implementation Steps

1. Build a source-to-target mapping from UE5.7 aerial-perspective generation and
   base-pass application to the Vortex atmosphere service, environment static
   data, shaders, and scene renderer publication seams.
2. Remove stale comments and any remaining implementation shortcuts that imply
   midpoint or average Beer-Lambert approximation in the aerial-perspective
   path.
3. Verify or correct camera-volume producer parameters: volume dimensions,
   start depth, depth slice length, nonlinear depth mapping, slice-center
   sampling, sample count per slice, view-distance scale, and per-view resource
   ownership.
4. Verify or correct consumer sampling: screen UV reconstruction, orthographic
   handling, transmittance alpha interpretation, exposure conversion,
   invalid-resource fallback, and main-pass application.
5. Define height-fog coupling explicitly, including whether Vortex AP contains
   height fog contribution in the sampled volume or composes height fog in the
   separate fog pass.
6. Audit all AP changes against VTX-M04D.1 and VTX-M04D.2. If publication
   truth, invalid-resource behavior, environment bindings, height-fog shader
   math, or height-fog tests are affected, update those artifacts and record
   fresh evidence in the status ledger.
7. Add focused tests for enabled/disabled AP, authored setting changes,
   unavailable resources, invalidation/publication truth, depth mapping, and
   main-pass application. Add shader/capture proof for pixel impact where unit
   tests cannot observe the behavior.
8. Update `PLAN.md` and `milestone README` with files changed, UE5.7
   references checked, exact validation commands/results, shader bake/catalog
   result when shader behavior changes, and residual gaps.

## Proof Gate

VTX-M04D.6 is validated for the main-view camera aerial-perspective path by the
evidence below. Required validation before any future revalidation claim:

```powershell
cmake --build --preset windows-debug --target Oxygen.Vortex.EnvironmentLightingService Oxygen.Vortex.SceneRendererPublication
ctest --preset test-debug -R "Oxygen.Vortex.(EnvironmentLightingService|SceneRendererPublication)" --output-on-failure
```

If CPU/HLSL layout, shader behavior, shader requests, or baked shader catalogs
change, run the project shader bake/catalog validation and record the exact
command/result. RenderScene visual confirmation exists for the main scene path,
and capture/analyzer proof is now required before changing this status again.
If AP work changes publication truth or height-fog behavior, rerun the affected
VTX-M04D.1 and VTX-M04D.2 focused validation and update their status evidence.

Validated evidence from 2026-04-26:

- UE5.7 source mapping checked:
  `SkyAtmosphereCommon.ush`, `SkyAtmosphere.usf`, `BasePassPixelShader.usf`,
  `BasePassVertexShader.usf`, `SceneRendering.cpp`, and
  `SkyAtmosphereRendering.cpp`. The checked paths cover
  `GetAerialPerspectiveLuminanceTransmittance`,
  `GetAerialPerspectiveLuminanceTransmittanceWithFogOver`,
  `RenderCameraAerialPerspectiveVolumeCS`, base-pass non-sky material
  application, view uniform camera AP publication, LUT dimensions/depth, and
  dummy black-3D/alpha-1 fallback resources.
- Build passed:
  `cmake --build --preset windows-debug --target Oxygen.Vortex.EnvironmentLightingService Oxygen.Vortex.SceneRendererPublication oxygen-examples-vortexbasic Oxygen.Examples.RenderScene.exe --parallel 4`.
- Focused tests passed:
  `ctest --preset test-debug -R "Oxygen.Vortex.(EnvironmentLightingService|SceneRendererPublication)" --output-on-failure`,
  with EnvironmentLightingService `40/40` and SceneRendererPublication `16/16`.
- Shader validation passed after the AP compose shader change:
  `cmake --build --preset windows-debug --target oxygen-graphics-direct3d12 --parallel 4`;
  `ShaderBake rebuild ... --mode dev` wrote `184` modules to
  `bin/Oxygen/Debug/dev/shaders.bin`; shader catalog tests passed `4/4`.
- VortexBasic enabled proof passed:
  `Verify-VortexAerialPerspectiveProof.ps1` against
  `out/build-ninja/analysis/vortex/m04d6-aerial-proof/vortexbasic_aerial_enabled_depthfix_frame5_capture.rdc`.
  Validation records `overall_verdict=pass`, `runtime_cli_observed=1`,
  `camera_aerial_volume_dims=64x64x32`,
  `camera_aerial_probe_rgb_sum=410703.604`,
  `aerial_scattering_strength=1`,
  `aerial_perspective_start_depth_km=0`, and
  `stage15_atmosphere_scene_color_delta_max=10`.
- VortexBasic disabled proof passed:
  the same wrapper with `-ExpectDisabled` against
  `vortexbasic_aerial_disabled_depthfix_frame5_capture.rdc`.
  Validation records `overall_verdict=pass`, `runtime_cli_observed=0`,
  `aerial_scattering_strength=0`, and
  `stage15_atmosphere_scene_color_delta_max=0`.
- City-scale RenderScene proof passed:
  `Verify-VortexAerialPerspectiveProof.ps1` against
  `out/build-ninja/analysis/vortex/m04d6-aerial-proof/renderscene_city_aerial_tuned_frame90_capture.rdc`.
  Validation records `overall_verdict=pass`,
  `camera_aerial_volume_dims=64x64x32`,
  `camera_aerial_probe_rgb_sum=12.785145`,
  `aerial_perspective_distance_scale=1`,
  `aerial_scattering_strength=1`,
  `aerial_perspective_start_depth_km=0.0400000028`,
  `camera_aerial_consumed_by_atmosphere=true`,
  `static_atmosphere_camera_volume_srv_valid=true`, and
  `stage15_atmosphere_scene_color_delta_max=448`.

## Residual Gaps Not Closed By This Plan

- Reflection/360-view camera aerial perspective is represented in shader helper
  shape but is not driven by a Vortex runtime resource path or capture proof.
  This is explicitly deferred and not claimed by the validated main-view AP
  package.
- Local fog, volumetric fog, SkyLight capture/filtering, and Async proof remain
  separate milestones.

## Validation

See the [validation record](validation.md) for commands, conditions, results and remaining work.
