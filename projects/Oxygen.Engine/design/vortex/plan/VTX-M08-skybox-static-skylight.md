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
- Grew `SkyLightEnvironmentRecord` to 96 bytes with legacy 84-byte read support.
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

Status: `in_progress`

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
- Kept specified-cubemap SkyLight unavailable with
  `kShaderConsumerMigrationIncomplete` until resource generation and shader
  consumer migration land, so shaders cannot sample mismatched descriptors.

Validation evidence:

- `cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.EnvironmentLightingService.Tests --parallel 4` passed.
- `ctest --preset test-debug -R "Oxygen\.Vortex\.EnvironmentLightingService" --output-on-failure` passed 1/1 test executable, 47/47 tests.
- `cmake --build out\build-ninja --config Debug --target oxygen-graphics-direct3d12_shaders Oxygen.Graphics.Direct3D12.ShaderBakeCatalog.Tests --parallel 4` passed; ShaderBake updated 186 modules.
- `ctest --preset test-debug -R "Oxygen\.Graphics\.Direct3D12\.ShaderBakeCatalog" --output-on-failure` passed 1/1 test executable, 4/4 tests.

Remaining gaps:

- No GPU processed cubemap resource, mip generation, SH structured-buffer upload,
  or runtime product publication exists yet.
- `diffuse_sh_slot` in `GpuSkyLightParams` and shader consumer migration remain
  open for Slice D.
- Runtime CDB/debug-layer, RenderDoc, allocation-churn, and visual proof remain
  open for later slices/closeout.
