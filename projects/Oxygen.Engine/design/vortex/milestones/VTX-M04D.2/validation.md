# VTX-M04D.2 — validation

This record preserves the conditions and results at each implementation checkpoint.
The milestone README owns the final outcome and remaining work.

## Validation and remaining work

**Qualification:** `validated`

CPU/HLSL height-fog payload, authored layer translation, analytic `HeightFogCommon`-shaped line integral, sky-depth exclusion, DemoShell/RenderScene controls, focused build/tests, ShaderBake/catalog validation, focused VortexBasic enabled/disabled RenderDoc proof, and city-scale RenderScene capture proof are recorded.

**Remaining work:** Cubemap inscattering resource binding/sampling remains explicitly unavailable and deferred.

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
