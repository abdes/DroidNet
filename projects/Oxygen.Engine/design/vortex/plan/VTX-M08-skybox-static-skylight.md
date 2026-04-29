# VTX-M08 Skybox And Static Specified-Cubemap SkyLight

Status: `in_progress`

This is the milestone planning and evidence file for the first post-M07
environment feature family. It exists so PRD, architecture, plan, status, and
the required LLDs stay aligned as implementation proceeds.

## Purpose

Deliver a production-clean static cubemap environment baseline:

- visual cubemap skybox/background rendering
- static specified-cubemap SkyLight diffuse lighting
- clear separation between visual sky background behavior and scene-lighting
  behavior
- deterministic interaction between cubemap skybox, procedural sky, and
  directional sun lighting
- preservation of VTX-M07 production-readiness and legacy-retirement proof

## UE5.7 Source Inputs

Initial source study was performed against local UE5.7 source and shader files.
The LLDs must preserve these inputs and expand them where needed:

- `Engine/Classes/Components/SkyLightComponent.h`
- `Engine/Private/Components/SkyLightComponent.cpp`
- `Renderer/Private/SkyPassRendering.cpp`
- `Renderer/Private/SkyAtmosphereRendering.cpp`
- `Renderer/Private/DeferredShadingRenderer.cpp`
- `Renderer/Private/ReflectionEnvironmentCapture.cpp`
- `Renderer/Private/IndirectLightRendering.cpp`
- `Shaders/Private/BasePassPixelShader.usf`
- `Shaders/Private/ReflectionEnvironmentShared.ush`

Non-UE engine behavior is not a VTX-M08 parity gate. M08 design and closure
decisions are grounded in local UE5.7 source and Vortex architecture.

## Required LLDs Before Implementation

Implementation must not start until these LLD drafts are reviewed:

- [`../lld/cubemap-processing.md`](../lld/cubemap-processing.md)
- [`../lld/skybox-static-skylight.md`](../lld/skybox-static-skylight.md)

The LLDs define data products, product validity states, shader contracts, pass
ordering, feature gates, proof scenes, proof scripts, and accepted/deferred
parity gaps.

Implementation slices, focused tests, and proof tooling currently live in
[`../lld/skybox-static-skylight.md`](../lld/skybox-static-skylight.md) §9-§10
and [`../lld/cubemap-processing.md`](../lld/cubemap-processing.md) §8 by
reference. Promote this stub to the full slice plan before implementation if
review asks for M05D/M06A-style slice evidence tracking in the plan file.

## In Scope

- Use existing Oxygen cubemap import as the ingestion baseline.
- Render a cubemap-backed skybox/background from scene-authored environment
  state.
- Implement static specified-cubemap SkyLight diffuse lighting through an
  environment-published product boundary.
- Define and validate the policy for scenes that author both procedural sky and
  cubemap skybox.
- Define and validate that directional sun lighting affects scene geometry and
  procedural sky behavior without modifying a static cubemap skybox image.
- Preserve no-environment, no-volumetrics, diagnostics, multi-view, offscreen,
  and feature-gated variants.
- Cleanly upgrade the environment shader ABI so static SkyLight diffuse SH has
  a dedicated binding and no shader aliases `irradiance_map_slot` between
  TextureCube and structured-buffer meanings.
- Include every current shader consumer in that ABI migration, including
  `ForwardMesh_PS.hlsl`, `ForwardDebug_PS.hlsl`, and
  `LocalFogVolumeCommon.hlsli`.
- Remove or gate any diffuse SkyLight path that falls back to the visual skybox
  cubemap instead of the processed SkyLight product.
- Add focused tests, runtime proof, CDB/debug-layer proof, RenderDoc scripted
  analysis, and documentation/status evidence.

## Out Of Scope

- Captured-scene SkyLight.
- Real-time sky capture.
- Cubemap blend transitions.
- Distance-field ambient occlusion / SkyLight occlusion.
- Baked/static-lightmap SkyLight integration.
- Reflection-capture recapture and broader reflection-probe ecosystem.
- Volumetric clouds, heterogeneous volumes, water, hair, distortion, and VSM.

## Planning Gate

VTX-M08 remains `in_progress` until every implementation slice and closure proof
gate passes. Do not mark it validated until skybox rendering, static
specified-cubemap SkyLight diffuse lighting, shader ABI migration, focused
tests, ShaderBake/catalog validation where required, CDB/debug-layer proof,
RenderDoc scripted analysis, allocation-churn proof, and residual-gap recording
are complete.

## Slice Evidence

### Slice A: Data And Plan State

Status: `validated`

Implementation evidence:

- Added SkyLight authored state for source cubemap angle, lower-hemisphere solid
  color toggle, and lower-hemisphere blend alpha.
- Updated `SkyLightEnvironmentRecord` to the current 92-byte layout after
  removing the stale SkyLight GI authoring switch. Older cooked records are not
  accepted; scene content must be re-cooked with the current schema.
- Updated scene descriptor import/schema, PakGen, PakDump, DemoShell scene
  record application, Vortex `SkyLightEnvironmentModel`, and model hashing.
- Added per-view plan state for `VisibleSkyBackground`, sky atmosphere/sphere
  availability, SkySphere source, and cubemap-authored status.
- Added feature-mask gating so `kNoEnvironment` views suppress sky pass and sky
  LUT work while preserving immutable scene sky state on the plan.

Validation evidence:

- `cmake --build out\build-ninja --config Debug --target Oxygen.Scene.EnvironmentComponents.Tests Oxygen.Cooker.AsyncImportSceneDescriptor.Tests Oxygen.Vortex.CompositionPlanner.Tests --parallel 4`
- `ctest --preset test-debug -R "Oxygen\.(Scene\.EnvironmentComponents|Vortex\.CompositionPlanner|Cooker\.AsyncImportSceneDescriptor)" --output-on-failure` passed 3/3 test executables.
- `python -m pytest src/Oxygen/Cooker/Tools/PakGen/tests/test_writer_scene_basic.py -q` passed 3 tests.
- `cmake --build out\build-ninja --config Debug --target oxygen-cooker-pakdump oxygen-examples-demoshell --parallel 4` passed.

Remaining gaps:

- Slices B-E are not complete.
- No runtime CDB/debug-layer, RenderDoc, allocation-churn, or visual proof exists
  for M08 yet.

### Slice B: Cubemap Product Processing

Status: `validated`

Implementation evidence:

- Added explicit static SkyLight product key/status/unavailable-reason/product
  state under Vortex environment types.
- Extended environment probe bindings with a dedicated `diffuse_sh_srv` slot
  while preserving the 112-byte `EnvironmentFrameBindings` payload size.
- Updated the CPU/HLSL `EnvironmentFrameBindings` mirror and invalid binding
  initialization.
- Updated `IblProbePass` / `IblProcessor` to classify disabled, captured-scene,
  real-time-capture, missing-specified-cubemap, and specified-cubemap states.
- Added cached asset-loader resolution and source `TextureCube` validation for
  specified-cubemap SkyLight inputs, including unresolved-resource,
  non-cubemap/incorrect-face-count, and unsupported-LDR-format unavailable
  reasons.
- Extended the static SkyLight product key with source content revision,
  selected output face size, and source format class when a valid HDR cube is
  resolved.
- Added a Vortex-native CPU static SkyLight cubemap processor that reads
  imported HDR cubemaps, applies source yaw selection, lower-hemisphere solid
  color replacement, FP16-domain source-radiance scaling metadata, average
  brightness, and UE5.7-shaped eight-`float4` diffuse irradiance SH packing.
- Added a CPU evaluator for the packed diffuse SH product so focused tests can
  prove the packing/evaluation contract before GPU shader migration.
- Extended the CPU processed cubemap product to include a complete mip chain.
- Added renderer-owned static SkyLight product caching in `IblProcessor`:
  processed RGBA16F TextureCube creation/upload, diffuse SH structured-buffer
  creation/upload, descriptor registration, upload-ticket gating, key-change
  resource replacement, and deferred release through existing Vortex
  Graphics/UploadCoordinator systems.
- Replaced the spare `GpuSkyLightParams` ABI word with `diffuse_sh_slot` while
  preserving `sizeof(GpuSkyLightParams) == 64` and
  `sizeof(EnvironmentStaticData) == 672`, and updated the HLSL mirror.
- Migrated the current shader consumers: `ForwardMesh_PS.hlsl` and
  `LocalFogVolumeCommon.hlsli` evaluate the dedicated diffuse SH binding, and
  `ForwardDebug_PS.hlsl` samples only the processed SkyLight cubemap for
  SkyLight raw-sky diagnostics with `radiance_scale` applied exactly once.
- Removed the visual `sky_sphere.cubemap_slot` fallback from SkyLight
  consumers and added a focused source audit covering `ForwardMesh_PS.hlsl`,
  `ForwardDebug_PS.hlsl`, and `LocalFogVolumeCommon.hlsli`.
- Completed renderer-owned product publication into
  `EnvironmentProbeBindings`: completed uploads publish processed cubemap and
  diffuse SH SRVs, keep unavailable specular resources invalid, mark the
  static SkyLight product `kValidCurrentKey`, and expose `diffuse_sh_slot`,
  processed cubemap slot/mip, and HDR `source_radiance_scale` through
  `EnvironmentStaticData`.

Validation evidence:

- `cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.EnvironmentLightingService.Tests --parallel 4` passed.
- `ctest --preset test-debug -R "Oxygen\.Vortex\.EnvironmentLightingService" --output-on-failure` passed 1/1 test executable, 50/50 tests.
- `cmake --build out\build-ninja --config Debug --target oxygen-graphics-direct3d12_shaders Oxygen.Graphics.Direct3D12.ShaderBakeCatalog.Tests --parallel 4` passed; ShaderBake updated 186 modules.
- `ctest --preset test-debug -R "Oxygen\.Graphics\.Direct3D12\.ShaderBakeCatalog" --output-on-failure` passed 1/1 test executable, 4/4 tests.

Remaining gaps:

- Deferred static SkyLight diffuse consumption remains open; the current slice
  publishes valid products and migrates existing forward/fog/debug consumers.
- Visual skybox rendering and deterministic runtime procedural-sky versus
  cubemap-sky selection remain open.
- Runtime CDB/debug-layer, RenderDoc, allocation-churn, and visual proof remain
  open for later slices/closeout.

### Slice C: Visual SkySphere Background

Status: `validated`

Implementation evidence:

- Published scene-authored `SkySphere` data through
  `EnvironmentStaticData::sky_sphere` for environment-enabled views, including
  source type, solid color, tint, intensity, rotation, cubemap descriptor slot,
  and cubemap max mip.
- Changed `GpuSkySphereParams::cubemap_slot` default to
  `kInvalidBindlessIndex` so the shader never treats descriptor slot zero as an
  implicit skybox texture.
- Reused the Vortex `TextureBinder` resource path for authored SkySphere
  cubemap descriptors; invalid, missing, or not-yet-ready cubemap descriptors
  remain explicit invalid bindings instead of using procedural-sky fallback.
- Updated `VortexSkyPassPS` to render a SkySphere branch before the procedural
  atmosphere LUT requirement. The branch supports solid-color sky and cubemap
  sky sampling with Oxygen +Z-up yaw rotation and tint/intensity application.
- Updated `SkyPass` request gating so SkySphere backgrounds can render without
  requiring procedural-atmosphere opt-in, while `kNoEnvironment` views still
  suppress the sky pass.

Validation evidence:

- `cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.EnvironmentLightingService --parallel 4` passed.
- `ctest --preset test-debug -R "Oxygen\.Vortex\.EnvironmentLightingService" --output-on-failure` passed 1/1 test executable, 54/54 tests.
- `cmake --build out\build-ninja --config Debug --target oxygen-graphics-direct3d12_shaders Oxygen.Graphics.Direct3D12.ShaderBakeCatalog.Tests --parallel 4` passed; ShaderBake updated 186 modules.
- `ctest --preset test-debug -R "Oxygen\.Graphics\.Direct3D12\.ShaderBakeCatalog" --output-on-failure` passed 1/1 test executable, 4/4 tests.
- `cmake --build out\build-ninja --config Debug --target oxygen-examples-renderscene oxygen-examples-demoshell oxygen-examples-texturedcube oxygen-graphics-direct3d12_shaders Oxygen.Vortex.EnvironmentLightingService.Tests Oxygen.Graphics.Direct3D12.ShaderBakeCatalog.Tests --parallel 4` passed after adding the RenderScene startup-skybox proof route, texture-domain SkySphere descriptor sampling, and EV-authored skybox radiance-scale UI.
- `ctest --preset test-debug -R "Oxygen\.Vortex\.EnvironmentLightingService|Oxygen\.Graphics\.Direct3D12\.ShaderBakeCatalog|Oxygen\.Content\.Loaders|Oxygen\.Content\.AssetLoader" --output-on-failure` passed 6/6 matched test executables.
- `python -m py_compile tools\vortex\AnalyzeRenderDocVortexSkybox.py` passed.
- CDB/debug-layer runtime audit passed for `Oxygen.Examples.RenderScene.exe -v=-1 --frames 40 --fps 0 --capture-provider off`; log `out\build-ninja\analysis\vortex\m08-skybox\renderscene-skybox-intensity.cdb.log` records exit code 0, no D3D12/DXGI errors after excluding the known non-blocking live `IDXGIFactory` shutdown warning, no device removal/hang, and no access violation.
- RenderDoc capture passed for `RenderScene -v=-1 --frames 100 --fps 0 --capture-provider renderdoc --capture-load search --capture-output out\build-ninja\analysis\vortex\m08-skybox\renderscene-skybox-intensity.rdc --capture-from-frame 75 --capture-frame-count 1`; capture `out\build-ninja\analysis\vortex\m08-skybox\renderscene-skybox-intensity_capture.rdc` was produced.
- `powershell -NoProfile -ExecutionPolicy Bypass -File tools\vortex\Verify-VortexSkyboxProof.ps1 -CapturePath out\build-ninja\analysis\vortex\m08-skybox\renderscene-skybox-intensity_capture.rdc -CaptureReportPath out\build-ninja\analysis\vortex\m08-skybox\renderscene-skybox-intensity.vortex-skybox.txt -AnalysisTimeoutSeconds 240` passed. The report proves one `Vortex.Stage15.Sky` scope, one sky draw, zero `Vortex.Stage15.Atmosphere` scopes, `atmosphere_enabled=0`, cubemap `SkySphere` source/enabled/texture-domain descriptor state, authored `sky_sphere_intensity=1000`, one `SceneColor` output, non-black Stage-15 sky samples, and 25/25 non-black Stage-22 tonemap sky samples.
- `RenderScene --frames 65 --fps 0 --capture-provider off` passed with startup SkySphere cubemap enabled from local demo settings; runtime log `out\build-ninja\analysis\vortex\m08-skybox\renderscene-skybox-allocation.verbose.stderr.log` records the startup skybox route and 65 `Vortex.SceneTextureLeasePool.Churn` telemetry records.
- `powershell -NoProfile -ExecutionPolicy Bypass -File tools\vortex\Assert-VortexSkyboxAllocationChurn.ps1 -RuntimeLogPath out\build-ninja\analysis\vortex\m08-skybox\renderscene-skybox-allocation.verbose.stderr.log -RunFrames 65 -WarmupFrames 5 -ReportPath out\build-ninja\analysis\vortex\m08-skybox\renderscene-skybox-allocation.allocation-churn.txt` passed. The report proves `telemetry_frame_count=65`, `steady_state_frame_count=60`, `steady_state_allocations_after_warmup=0`, `steady_state_allocations_zero=true`, and `overall_verdict=pass`.
- User visual confirmation accepted the corrected RenderScene skybox settings
  after the black-window configuration issue was fixed and before the final
  short 10 fps capture proof.
- `git diff --check` passed.

Remaining gaps:

- Deferred static SkyLight diffuse consumption remains open.

### Slice D: Deferred Static SkyLight Diffuse Consumption

Status: `validated`

Implementation evidence:

- Added a deferred static SkyLight Stage 12 draw owned by `LightingService` /
  `DeferredLightPass`, using the existing full-screen deferred-light pipeline
  with an explicit static SkyLight light kind.
- Added shader-side static SkyLight diffuse evaluation from the dedicated
  eight-`float4` SH binding, applying tint, radiance scale, and diffuse
  intensity exactly once before multiplying by non-metallic base color.
- Added service-pass debug routing for `ibl-only` and `direct-plus-ibl` so
  static SkyLight diffuse can be isolated without executing directional,
  point, spot, or visual-sky passes in the IBL-only proof.
- Preserved M08 specular scope by keeping `prefilter_map_slot` and
  `brdf_lut_slot` invalid while leaving guarded forward specular paths in
  place for future reflection work.
- Fixed the renderer-owned static SkyLight product cache so completed uploads
  remain valid after upload-ticket retirement instead of regressing to a
  regenerating state for the same product key.
- Extended RenderScene/DemoShell startup skybox plumbing so validation scenes
  can seed SkySphere and SkyLight settings through the existing `SkyboxService`
  path without adding renderer test hooks.
- Added scripted RenderDoc static SkyLight analysis and assertion tooling under
  `tools/vortex`.

Validation evidence:

- `cmake --build out\build-ninja --config Debug --target oxygen-examples-renderscene Oxygen.Vortex.EnvironmentLightingService.Tests Oxygen.Vortex.SceneRendererDeferredCore.Tests Oxygen.Vortex.ShaderDebugModeRegistry.Tests --parallel 4` passed and rebaked the changed deferred-light shader variants.
- `ctest --preset test-debug -R "Oxygen\.Vortex\.(EnvironmentLightingService|SceneRendererDeferredCore|ShaderDebugModeRegistry)|Oxygen\.Graphics\.Direct3D12\.ShaderBakeCatalog" --output-on-failure` passed 4/4 matched test executables.
- Runtime cache proof `RenderScene --frames 80 --fps 10 --capture-provider off` recorded steady `sky_light_ibl_status=valid-current-key`, `sky_light_ibl_valid=true`, and `sky_light_ibl_unavailable_reason=none` after product generation.
- CDB/debug-layer audit `cdb -logo out\build-ninja\analysis\vortex\m08-skylight\renderscene-static-skylight-prod-iblonly-10fps.cdb.log -c "g;q" out\build-ninja\bin\Debug\Oxygen.Examples.RenderScene.exe --frames 60 --fps 10 --capture-provider off` exited 0 and the D3D12/DXGI/device-removal scan found 0 blocking hits; the known live `IDXGIFactory` shutdown warning remains non-blocking for this gate.
- RenderDoc capture `out\build-ninja\analysis\vortex\m08-skylight\renderscene-static-skylight-prod-iblonly-10fps_capture.rdc` was produced with `RenderScene --frames 60 --fps 10 --capture-provider renderdoc --capture-load search --capture-output out\build-ninja\analysis\vortex\m08-skylight\renderscene-static-skylight-prod-iblonly-10fps --capture-from-frame 45 --capture-frame-count 1`.
- `tools\vortex\Verify-VortexStaticSkyLightProof.ps1 -CapturePath out\build-ninja\analysis\vortex\m08-skylight\renderscene-static-skylight-prod-iblonly-10fps_capture.rdc -AnalysisTimeoutSeconds 240` passed. The report proves one `Vortex.Stage12.StaticSkyLight` draw, zero directional/point/spot/Stage15 sky scopes in IBL-only mode, enabled specified-cubemap SkyLight state, texture-domain processed cubemap slot `35818`, valid diffuse SH slot `49`, invalid prefilter slot, positive radiance/diffuse intensity, `scene_color_max_luminance=0.128336914`, 31 non-black scanned pixels after the static SkyLight draw, 16/16 sampled pixel histories passing through the static SkyLight draw, and `static_skylight_pixel_history_verdict=true`.

Remaining gaps:

- User visual confirmation and residual-gap recording remain open for full
  VTX-M08 closure.

### Slice E: Interaction And Lifecycle Proof

Status: `validated`

Implementation evidence:

- Extended `tools/vortex/Verify-VortexStaticSkyLightProof.ps1` and
  `AnalyzeRenderDocVortexStaticSkyLight.py` with a `direct-plus-ibl` proof mode.
  The default remains IBL-only; the new mode requires one directional draw, one
  static SkyLight draw, no local-light scopes, no visual sky scope, and sampled
  pixel histories touched by both the directional and static SkyLight draws.
- Added a RenderScene-only static SkyLight lifecycle proof toggle sourced from
  local demo settings. The toggle mutates the scene-authored `SkyLight` at
  frame boundaries without adding renderer runtime test hooks.
- Added `tools/vortex/Assert-VortexStaticSkyLightLifecycle.ps1` to prove
  disabled -> regenerating -> valid-current-key state recovery and post-toggle
  steady-frame churn.

Validation evidence:

- Direct-plus-IBL RenderDoc capture
  `out\build-ninja\analysis\vortex\m08-skylight\renderscene-static-skylight-direct-plus-ibl-10fps_capture.rdc`
  was produced with `RenderScene --frames 60 --fps 10 --capture-provider renderdoc --capture-load search --capture-output out\build-ninja\analysis\vortex\m08-skylight\renderscene-static-skylight-direct-plus-ibl-10fps --capture-from-frame 45 --capture-frame-count 1`.
- `tools\vortex\Verify-VortexStaticSkyLightProof.ps1 -CapturePath out\build-ninja\analysis\vortex\m08-skylight\renderscene-static-skylight-direct-plus-ibl-10fps_capture.rdc -ProofMode direct-plus-ibl -AnalysisTimeoutSeconds 240` passed. The report proves one static SkyLight draw, one directional draw, zero point/spot/Stage15 sky scopes, valid processed cubemap and diffuse SH bindings, invalid prefilter slot, `scene_color_max_luminance=6292.476`, 31 non-black scanned pixels, 16/16 static SkyLight histories passed, 16/16 directional histories passed, `direct_plus_ibl_both_history_count=16`, and `static_skylight_pixel_history_verdict=true`.
- Lifecycle runtime proof `RenderScene --frames 115 --fps 10 --capture-provider off` passed with local proof settings toggling SkyLight off at frame 30 and back on at frame 45.
- `tools\vortex\Assert-VortexStaticSkyLightLifecycle.ps1 -RuntimeLogPath out\build-ninja\analysis\vortex\m08-skylight\renderscene-static-skylight-lifecycle-10fps.log -ToggleOffFrame 30 -ToggleOnFrame 45 -WarmupFramesAfterOn 10 -MinimumStableFrames 60 -ReportPath out\build-ninja\analysis\vortex\m08-skylight\renderscene-static-skylight-lifecycle-10fps.lifecycle.txt` passed. The report proves `disabled_status_after_off_count=15`, `regenerating_after_on_count=1`, `valid_current_key_after_on_count=70`, `unexpected_unavailable_reason_after_on_count=0`, `stable_churn_frame_count=61`, and `stable_churn_nonzero_delta_count=0`.
- CDB/debug-layer audit of the lifecycle path
  `cdb -logo out\build-ninja\analysis\vortex\m08-skylight\renderscene-static-skylight-lifecycle-10fps.cdb.log -c "g;q" out\build-ninja\bin\Debug\Oxygen.Examples.RenderScene.exe --frames 115 --fps 10 --capture-provider off` exited 0 and the D3D12/DXGI/device-removal scan found 0 blocking hits; the known live `IDXGIFactory` shutdown warning remains non-blocking for this gate.

Remaining gaps:

- User visual confirmation remains open for full VTX-M08 closure.
- Residual-gap recording and final closeout status update remain open.
