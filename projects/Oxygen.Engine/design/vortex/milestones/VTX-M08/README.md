# VTX-M08 — Skybox and static specified-cubemap SkyLight

Status: `validated`

| Field     | Summary                                                                                                                                                                                          |
| --------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Outcome   | Static cubemap skyboxes and specified-cubemap diffuse SH lighting.                                                                                                                               |
| Remaining | Extensions: [VX-IBL-01](../../OPEN_ITEMS.md#p1--current-delivery), [VX-SKY-01](../../OPEN_ITEMS.md#p3--unscheduled-capabilities), [VX-SKY-02](../../OPEN_ITEMS.md#p3--unscheduled-capabilities). |
| Evidence  | [Validation record](validation.md)                                                                                                                                                               |

[Roadmap](../../PLAN.md) · [Design index](../../lld/README.md)

Dependencies: VTX-M07, VTX-M04D environment publication truth, VTX-M05D shadows, VTX-M06C feature gates

## Delivered scope

Visual cubemap skybox/background rendering, static specified-cubemap SkyLight diffuse lighting, shader ABI migration, static SkyLight product processing/publication, deferred SH consumption, RenderScene/DemoShell startup plumbing, interaction/lifecycle proof, focused tests, ShaderBake/catalog validation where shader ABI changed, CDB/debug-layer audits, RenderDoc scripted analysis, allocation-churn proof, final `git diff --check`, and manual visual confirmation are recorded in the detailed M08 plan and status ledger. Captured-sky diffuse/specular and specified-cubemap specular lighting are planned under [ED-M08](../../lld/captured-sky-ibl.md); implementation and rendered qualification remain pending. Continuous time-sliced capture, blending, SkyLight occlusion/baking, broader probes, cloud capture and static-skybox sun-disk overlay remain future work.

Status: `validated`

This is the milestone planning and evidence file for the first post-M07
environment feature family. It records the implementation and proof evidence
that closed VTX-M08, and preserves the accepted future gaps that remain outside
this milestone.

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

- [`../lld/cubemap-processing.md`](../../lld/cubemap-processing.md)
- [`../lld/skybox-static-skylight.md`](../../lld/skybox-static-skylight.md)

The LLDs define data products, product validity states, shader contracts, pass
ordering, feature gates, proof scenes, proof scripts, and accepted/deferred
parity gaps.

Implementation slices, focused tests, and proof tooling currently live in
[`../lld/skybox-static-skylight.md`](../../lld/skybox-static-skylight.md) §9-§10
and [`../lld/cubemap-processing.md`](../../lld/cubemap-processing.md) §8 by
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

## Closure Gate

VTX-M08 is `validated` only because every implementation slice and closure proof
gate below has recorded evidence: skybox rendering, static specified-cubemap
SkyLight diffuse lighting, shader ABI migration, focused tests,
ShaderBake/catalog validation where required, CDB/debug-layer proof, RenderDoc
scripted analysis, allocation-churn proof, manual visual confirmation, and
residual-gap recording.

## Remaining Work

These remain outside the closed VTX-M08 milestone. ED-M08 now owns captured-sky
diffuse/specular lighting, specified-cubemap specular products, Stage 13 activation
and ambient-bridge retirement under the [captured-sky contract](../../lld/captured-sky-ibl.md).
That extension is specified but not implemented or rendered-qualified. Its
capture policy is automatic and change-driven.

- Captured-scene SkyLight and static-cubemap specular contribution: planned in ED-M08.
- Continuous time-sliced SkyLight capture: future work outside ED-M08.
- Cubemap blend transitions / time-of-day blending.
- SkyLight AO, DFAO, bent-normal occlusion, and cloud AO.
- Baked/static-lightmap SkyLight integration.
- Reflection captures and broader reflection-probe arrays.
- Volumetric-cloud sky capture.
- Procedural sun-disk overlay composited into static cubemap skybox imagery.

## Supporting records

- [validation](validation.md)
