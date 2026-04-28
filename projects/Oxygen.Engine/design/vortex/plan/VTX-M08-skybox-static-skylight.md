# VTX-M08 Skybox And Static Specified-Cubemap SkyLight

Status: `planned`

This is the milestone planning stub for the first post-M07 environment feature
family. It exists so PRD, architecture, plan, status, and the required LLD
drafts can be reviewed before implementation starts.

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
parity gaps. They remain draft design until reviewed.

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

VTX-M08 remains `planned` until the PRD, milestone scope, and required LLD
drafts are reviewed. No implementation or validation evidence exists for this
milestone yet.
