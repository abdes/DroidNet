# VTX-M04D.2 — UE5.7 Exponential Height Fog Parity

Status: `validated`

This plan covers only exponential height fog. It preserves the validated
VTX-M04D.1 publication-truth baseline and does not claim local fog,
volumetric fog, real SkyLight capture/filtering, async migration proof, or full
environment runtime closure.

## Scope

- Preserve current atmosphere, sky, below-horizon, SkyLight/IBL unavailable,
  and Stage 14 observability contracts from VTX-M04D.1.
- Implement UE5.7-informed height-fog authoring and runtime translation for:
  primary and secondary fog layers; density, falloff, and height offset;
  start, end, cutoff distance, and max opacity; fog inscattering luminance;
  directional inscattering luminance, exponent, and start distance;
  sky-atmosphere ambient contribution color scale; visibility flags already
  present in the model.
- Preserve explicit unavailable cubemap-resource behavior without implying
  usable cubemap fog. Runtime cubemap sampling is deferred until after the
  environment/fog runtime artifacts that affect current validation scenes are
  closed.
- Replace simplified height-fog shader/math behavior with a
  `FogRendering` / `HeightFogCommon`-shaped implementation.
- Keep far-depth sky pixels excluded from fog contribution.

## UE5.7 Grounding

Implementation and review must check:

- `F:\Epic Games\UE_5.7\Engine\Source\Runtime\Renderer\Private\FogRendering.cpp`
- `F:\Epic Games\UE_5.7\Engine\Shaders\Private\HeightFogCommon.ush`
- `F:\Epic Games\UE_5.7\Engine\Source\Runtime\Engine\Private\Components\ExponentialHeightFogComponent.cpp`
- Sky-atmosphere files only where height fog consumes sky-atmosphere ambient
  contribution or sky-depth exclusion semantics.

## Implementation Steps

1. Audit existing Vortex height-fog model, state translation, CPU/HLSL static
   data, fog pass, and tests against the UE5.7 reference fields and shader
   equations.
2. Widen or correct CPU data translation only where gaps exist, keeping
   publication truth and invalid-resource states explicit.
3. Update `Fog.hlsl` and any required environment shader contract structs in
   lockstep with CPU layout changes.
4. Add focused EnvironmentLightingService tests for enabled and disabled
   height fog, authored parameter changes, secondary layer, directional
   inscattering, start/end/cutoff/max-opacity semantics, sky-depth exclusion,
   deferred cubemap unavailable behavior, and publication truth preservation.
5. Extend SceneRenderer publication tests only if the renderer boundary needs
   additional assertions beyond the VTX-M04D.1 Stage 14 baseline.
6. Record changed files, UE5.7 references checked, build/test results,
   shader-bake/catalog validation when contracts or shader behavior changed,
   doc/status scans, `git diff --check`, and residual gaps in
   `IMPLEMENTATION_STATUS.md`.

## Proof Gate

VTX-M04D.2 remains `in_progress` unless all required implementation exists,
docs/status are updated, and the following fresh evidence is recorded:

```powershell
cmake --build --preset windows-debug --target Oxygen.Vortex.EnvironmentLightingService Oxygen.Vortex.SceneRendererPublication
ctest --preset test-debug -R "Oxygen.Vortex.(EnvironmentLightingService|SceneRendererPublication)" --output-on-failure
```

If CPU/HLSL layout or shader behavior changes, run the project shader
bake/catalog validation and record the exact command and result.

## Residual Gaps Not Closed By This Plan

- Cubemap inscattering authoring is translated into the GPU contract and
  published as authored, but Vortex does not yet have a bindable height-fog
  cubemap resource path. Runtime cubemap sampling remains explicitly
  unavailable, with `CubemapAuthored` set and `CubemapUsable` unset. This is a
  deferred nice-to-have, not the current priority for VTX-M04D closure. It must
  not block city-scale atmosphere/fog artifact work, aerial-perspective proof,
  local-fog proof, volumetric-fog proof, or environment runtime proof.
- Aerial perspective capture/reflection proof remains VTX-M04D.6 scope.
- Full environment runtime closure remains VTX-M04D.5 scope.
- Real SkyLight capture/filtering.
- Async runtime migration proof.
- DiagnosticsService implementation.

## Current Evidence

2026-04-25 implementation evidence:

- UE5.7 references checked:
  `Renderer\Private\FogRendering.cpp`,
  `Shaders\Private\HeightFogCommon.ush`, and
  `Engine\Private\Components\ExponentialHeightFogComponent.cpp`.
- CPU/HLSL fog static data now carries primary and secondary density/falloff
  layers, height offsets, start/end/cutoff/max-opacity, fog and directional
  inscattering, sky-atmosphere ambient scale, visibility flags, and explicit
  cubemap authored/unusable state.
- `Fog.hlsl` replaces the simplified color/alpha path with a
  `HeightFogCommon`-shaped analytic line integral. It uses UE's unnormalized
  vertical ray deltas, start-distance exclusion, end-distance clamp,
  cutoff-distance exclusion, max-opacity transmittance floor, directional
  inscattering lobe, sky-atmosphere ambient contribution via the distant sky
  light LUT, and far-depth sky exclusion.
- A separate midpoint Beer-Lambert approximation in
  `AerialPerspective.hlsli` was removed so Vortex has one height-fog runtime
  path for the main scene view.
- Meter-space defaults were aligned with UE's centimeter-space scaling:
  default density `0.002 1/m`, default height falloff `0.02 1/m`, black
  authored fog/directional inscattering, white sky-atmosphere ambient scale,
  directional exponent `4`, and directional start distance `10000 m`.
- DemoShell and RenderScene expose and persist only currently supported
  exponential height-fog controls for visual verification. RenderScene requests
  the height-fog pass from DemoShell state, and its local
  `demo_settings.json` uses height fog enabled, `end_distance_m = 0`, and no
  secondary layer by default.
- Focused build passed:
  `cmake --build --preset windows-debug --target Oxygen.Vortex.EnvironmentLightingService Oxygen.Vortex.SceneRendererPublication Oxygen.Examples.DemoShell.EnvironmentSettingsService.Tests Oxygen.Examples.RenderScene.exe`.
- Focused tests passed:
  `ctest --preset test-debug -R "Oxygen.Vortex.(EnvironmentLightingService|SceneRendererPublication)|Oxygen.Examples.DemoShell.EnvironmentSettingsService" --output-on-failure`
  with `Oxygen.Vortex.EnvironmentLightingService.Tests` 32/32,
  `Oxygen.Vortex.SceneRendererPublication.Tests` 16/16, and
  `Oxygen.Examples.DemoShell.EnvironmentSettingsService.Tests` 24/24.
- ShaderBake/catalog validation passed:
  `cmake --build --preset windows-debug --target oxygen-graphics-direct3d12`
  and direct `ShaderBake update`; the direct run reported
  `expanded_requests=182`, `dirty_requests=0`, `clean_requests=182`, and
  `stale_requests=0`.
2026-04-26 runtime/capture closure evidence:

- Added focused height-fog RenderDoc proof tooling:
  `tools/vortex/AnalyzeRenderDocVortexHeightFog.py`,
  `tools/vortex/Assert-VortexHeightFogProof.ps1`, and
  `tools/vortex/Verify-VortexHeightFogProof.ps1`.
- Focused build passed:
  `cmake --build --preset windows-debug --target Oxygen.Vortex.EnvironmentLightingService Oxygen.Vortex.SceneRendererPublication --parallel 4`.
- Focused tests passed:
  `ctest --preset test-debug -R "Oxygen.Vortex.(EnvironmentLightingService|SceneRendererPublication)" --output-on-failure`
  with `Oxygen.Vortex.EnvironmentLightingService.Tests` 40/40 and
  `Oxygen.Vortex.SceneRendererPublication.Tests` 16/16.
- VortexBasic build passed:
  `cmake --build out/build-ninja --config Debug --target oxygen-examples-vortexbasic --parallel 4`.
- Focused VortexBasic enabled proof passed:
  `tools/vortex/Verify-VortexHeightFogProof.ps1` against
  `out/build-ninja/analysis/vortex/m04d2-heightfog-proof/vortexbasic_heightfog_enabled_frame5_capture.rdc`.
  Validation report records `overall_verdict=pass`,
  `stage15_fog_scope_count=1`, `stage15_fog_draw_count=1`,
  `runtime_cli_observed=true`, `height_fog_scene_color_delta_max=43`,
  and `height_fog_far_depth_sample_count=33`.
- Focused VortexBasic disabled proof passed:
  `tools/vortex/Verify-VortexHeightFogProof.ps1 -ExpectDisabled` against
  `out/build-ninja/analysis/vortex/m04d2-heightfog-proof/vortexbasic_heightfog_disabled_frame5_capture.rdc`.
  Validation report records `overall_verdict=pass`,
  `stage15_fog_scope_count=0`, `stage15_fog_draw_count=0`, and
  `runtime_cli_observed=false`.
- City-scale RenderScene proof passed:
  `tools/vortex/Verify-VortexHeightFogProof.ps1 -SkipRuntimeCliCheck` against
  `out/build-ninja/analysis/vortex/m04d4-city-volumetric-proof/renderscene_city_volumetric_frame90_capture.rdc`.
  Validation report records `overall_verdict=pass`,
  `stage15_fog_scope_count=1`, `stage15_fog_draw_count=1`,
  `height_fog_scene_color_delta_max=593.5`,
  `height_fog_far_depth_sample_count=53`, captured 672-byte
  `EnvironmentStaticData`, positive primary density, valid max
  opacity/min-transmittance, enabled/render-in-main-pass flags, and cubemap
  unavailable state.
- Status: VTX-M04D.2 is validated for UE5.7-informed exponential height-fog
  authored parameters, CPU/HLSL publication, analytic Stage-15 application,
  disabled fast path, focused runtime/capture proof, and city-scale capture
  proof. Cubemap inscattering resource binding/sampling remains explicitly
  deferred and is not claimed.
