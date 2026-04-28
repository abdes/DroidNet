# Vortex Renderer Implementation Plan

Status: `active milestone plan`

This document is the execution plan for Vortex. It answers two questions:

1. What is the next milestone?
2. What scope, dependencies, and proof gates must close for each remaining
   milestone?

The product target is defined by [PRD.md](./PRD.md). The architecture target is
defined by [ARCHITECTURE.md](./ARCHITECTURE.md). Detailed subsystem contracts
live in [DESIGN.md](./DESIGN.md) and the LLD package under
[lld/](./lld/README.md). Current implementation state is tracked in
[IMPLEMENTATION_STATUS.md](./IMPLEMENTATION_STATUS.md).
Detailed per-milestone implementation-preparation plans live under
[plan/](./plan/README.md).

## 1. Non-Negotiable Rules

- `Oxygen.Renderer` is legacy dead code for Vortex planning. It is not a
  reference implementation, fallback path, completion shortcut, or parity
  source.
- Every milestone that claims UE5.7 parity must cite the relevant UE5.7 source
  and shader families under `F:\Epic Games\UE_5.7\Engine\Source\Runtime` and
  `F:\Epic Games\UE_5.7\Engine\Shaders`.
- Compile success, placeholder plumbing, null-safe no-ops, raw RenderDoc
  captures, and authored-data extraction do not close parity gates.
- A milestone closes only when implementation exists, required docs/status are
  updated, and validation evidence is recorded in
  [IMPLEMENTATION_STATUS.md](./IMPLEMENTATION_STATUS.md).
- If implementation scope is found insufficient for required parity, update the
  design and plan first. Do not relabel partial work as done.

## 2. Status Vocabulary

Use these values in this plan and in `IMPLEMENTATION_STATUS.md`.

| Status | Meaning |
| --- | --- |
| `validated` | Implementation exists, docs/status are current, and fresh validation evidence is recorded. |
| `landed_needs_validation` | Substantial code exists, but this ledger does not contain fresh proof for final closure. |
| `in_progress` | Implementation exists but known gaps remain, or scope is actively being corrected. |
| `planned` | Design and scope are known; implementation has not started for the milestone. |
| `blocked` | Required design, dependency, or proof surface is missing. |
| `future` | Explicitly out of the production-complete desktop deferred baseline or deferred after it. |

## 3. Current Baseline

The current repo is not a blank slate. Vortex already contains substantial
Renderer Core, SceneRenderer, deferred-core, service, and proof-tooling work.
The status ledger records this as `landed_needs_validation` unless there is
fresh milestone-level validation evidence tied to the current status file.

Important baseline facts:

- `SceneRenderer`, `SceneTextures`, core publication, stage ordering, Stage 21
  resolve, Stage 22 post-process handoff, and Stage 23 cleanup surfaces exist.
- Stage 2 InitViews, Stage 3 depth prepass, Stage 5 generic Screen HZB,
  Stage 9 base pass/velocity, Stage 10 GBuffer publication, and Stage 12
  deferred lighting are present in code.
- `LightingService`, `ShadowService`, `PostProcessService`, and
  `EnvironmentLightingService` are real code owners, not empty future names.
- `EnvironmentLightingService` is parity-closed for the Phase 4D environment
  gates: sky/atmosphere publication truth, main-view AP, height fog, local fog,
  volumetric fog, and one consolidated runtime proof are validated. Real
  SkyLight capture/filtering remains open resource work before usable IBL can
  be claimed.
- `DiagnosticsService`, `TranslucencyModule`, and the full `OcclusionModule`
  are not concrete runtime closures yet, even though supporting substrate and
  some placeholder directories exist.
- Multi-view and offscreen substrates exist, but their runtime proof gates are
  separate late milestones.

## 4. Next Milestone

### NEXT: VTX-M08 - Skybox And Static Specified-Cubemap SkyLight

**Status:** `planned`

**Purpose:** Implement and validate the first cubemap-backed environment
baseline after production readiness: visual skybox background rendering and
static specified-cubemap SkyLight diffuse lighting.

**Why this is next:** `VTX-M07` validated production readiness and legacy
retirement. UE5.7 parity requires separating a visual skybox/SkyPass-style
background from SkyLight lighting products. Oxygen already has scene authoring
and cubemap import surfaces, but real SkyLight cubemap processing remains an
open resource gap.

**Prerequisite work packages:** `VTX-M03` is landed with downstream validated
surfaces; `VTX-M04D.4`, `VTX-M05A`, `VTX-M05B`, `VTX-M05C`, `VTX-M05D`,
`VTX-M06A`, `VTX-M06B`, `VTX-M06C`, and `VTX-M07` are validated.

**Active work packages:** `VTX-M08` planning package only.

**Detailed plan:** [plan/VTX-M08-skybox-static-skylight.md](plan/VTX-M08-skybox-static-skylight.md).

**In scope:**

- Preserve the M07 proof boundary: no legacy renderer reference, fallback, or
  compatibility path.
- Use existing Oxygen cubemap import support as the asset-ingestion baseline.
- Author and review the required LLDs before implementation:
  `lld/cubemap-processing.md` must define cubemap ingestion boundaries,
  derived SkyLight product ownership, runtime publication states, shader-facing
  contracts, and validation surfaces; `lld/skybox-static-skylight.md` must
  define visual-background selection, procedural-sky interaction, directional
  sun interaction, SkyLight publication/consumption boundaries, feature gates,
  and proof scenes/scripts.
- Implement a visual skybox background path from `SkySphere` / cubemap
  authoring that writes SceneColor as unlit sky/background content.
- Implement static specified-cubemap SkyLight diffuse lighting through a
  documented environment-product boundary, with exact product formats,
  filtering, shader bindings, and validation defined in the new LLDs.
- Preserve no-environment, no-volumetrics, diagnostics, multi-view, offscreen,
  and feature-variant behavior from VTX-M06/VTX-M07.

**Out of scope:**

- Captured-scene SkyLight.
- Real-time sky capture.
- Cubemap blend transitions.
- Distance-field ambient occlusion / SkyLight occlusion.
- Baked/static-lightmap SkyLight integration.
- Reflection-capture recapture and broader reflection-probe ecosystem.
- Volumetric clouds, heterogeneous volumes, water, hair, distortion, and VSM.
- Treating a visual skybox texture sample or a constant ambient color as
  SkyLight parity.

**Dependencies:**

- Validated Vortex composition, diagnostics, occlusion, translucency,
  conventional shadows, multi-view, offscreen, and feature-gated variant
  surfaces.

**Next candidate after VTX-M08:** Virtual Shadow Maps or the broader
IndirectLighting/reflection family. Those remain `future` until VTX-M08 is
reviewed and the next detailed plan is selected.

## 5. Milestone Roadmap

The roadmap is ordered by dependency and proof value. Milestone IDs are stable
planning handles; do not renumber them when scopes are refined.

| ID | Milestone | Status | Depends On | Primary Outcome |
| --- | --- | --- | --- | --- |
| VTX-M00 | Planning and status truth surface | `validated` | current docs/source | `PLAN.md` and `IMPLEMENTATION_STATUS.md` are the canonical planning/status pair. |
| VTX-M01 | Renderer Core and SceneRenderer baseline | `landed_needs_validation` | none | Vortex module, Renderer Core substrate, SceneRenderer shell, SceneTextures, core facades, resolve/cleanup. |
| VTX-M02 | Deferred core visual path | `landed_needs_validation` | VTX-M01 | InitViews, depth prepass, generic HZB, base pass/GBuffer/velocity, Stage 10 publication, deferred lighting, debug views. |
| VTX-M03 | Migration-critical non-environment services | `landed_needs_validation` | VTX-M02 | LightingService, ShadowService directional baseline, PostProcessService, and their publication seams. |
| VTX-M04D | Environment / fog parity closure | `validated` | VTX-M02, VTX-M03 validation context | Parent milestone for environment publication truth, main-view aerial perspective, height fog, local fog, volumetric fog, and consolidated VortexBasic runtime proof. Real SkyLight cubemap capture/filtering remains a later IBL/indirect-lighting resource milestone. |
| VTX-M04D.1 | Environment publication and sky/fog contract truth | `validated` | VTX-M02, current environment code | Truthful environment binding/publication state, SkyLight/IBL truth, Stage 14 observability. |
| VTX-M04D.2 | UE5.7 exponential height fog parity | `validated` | VTX-M04D.1 | UE5.7-informed authored parameters, CPU/HLSL publication, analytic Stage-15 application, disabled fast path, focused runtime/capture proof, and city-scale capture proof. Cubemap inscattering resource binding/sampling remains explicitly deferred. |
| VTX-M04D.3 | UE5.7 local fog volume parity | `validated` | VTX-M04D.1, VTX-M04D.2, Stage 5 HZB | Analytical local-fog volume path is implemented and proven: authored parameter sanitization traced to UE5.7 local-fog inputs, deterministic sort/cap policy, HZB-backed tiled culling, single draw-indirect compose, SceneColor contribution, far-depth no-op behavior, focused tests, shader validation, and VortexBasic runtime/capture proof. Local-fog volumetric injection is validated separately under VTX-M04D.4. |
| VTX-M04D.4 | UE5.7 volumetric fog parity | `validated` | VTX-M04D.1, VTX-M04D.2, VTX-M04D.3, VTX-M03 directional CSM proof | Integrated-light-scattering runtime product path is implemented and proven with focused Stage-14 RenderDoc proof, captured Stage-15 fog static-data SRV/flag proof, focused integrated-volume sampling proof, and city-scale RenderScene capture proof. Evidence covers log-distributed froxel depth, primary/secondary height-fog spatial media density, primary directional CSM shadowed-light sampling, Oxygen distant-SkyLight volumetric ambient injection, local-fog participating-media injection, Halton jitter, reset, history-miss supersampling, and reprojection for Oxygen's integrated-scattering product. Term-isolated local-fog, SkyLight, directional-shadow, and temporal artifact proof exists from paired VortexBasic RenderDoc captures. Accepted Oxygen divergences are documented for UE conservative-depth history fixup, pre-exposure transfer, and UE's separate light-scattering history product. Real SkyLight cubemap capture/filtering is an IBL/indirect-lighting resource milestone, not a VTX-M04D.4 closure gate. |
| VTX-M04D.5 | Environment runtime proof and Async preparation | `validated` | VTX-M04D.2, VTX-M04D.3, VTX-M04D.4, VTX-M04D.6 | `Run-VortexBasicRuntimeValidation.ps1` now builds, runs a CDB/D3D12 audit, captures RenderDoc frame 5, and asserts one runtime path exercising atmosphere, main-view AP, height fog, local fog, volumetric fog, and authored SkyLight unavailable/volumetric state. Real SkyLight cubemap capture/filtering remains a separate IBL/indirect-lighting resource implementation before usable IBL output can be claimed. |
| VTX-M04D.6 | UE5.7 aerial perspective parity | `validated` | VTX-M04D.1, VTX-M04D.2 | Main-view camera aerial-perspective volume generation/sampling, main-pass application, height-fog coupling boundary, focused enabled/disabled proof, and city-scale `CityEnvironmentValidation` capture proof. Reflection/360-view AP resource behavior is explicitly deferred to the future reflection-capture resource path. |
| VTX-M04E | Async migration parity gate | `validated` | VTX-M03, VTX-M04D.5 | `Examples/Async` runs through Vortex with no long-lived compatibility clutter and has RenderDoc-backed structural/product/presentation proof. |
| VTX-M04F | Single-view composition and presentation closeout | `validated` | VTX-M04E | Single-view composition, resolve, handoff, post-process, and presentation proof with RenderDoc-backed Stage 22 copy, overlay blend, and final present evidence. |
| VTX-M05A | Diagnostics product service | `validated` | VTX-M04D.1, VTX-M04F | Concrete diagnostics product surface; not confused with proof tooling. |
| VTX-M05B | Occlusion consumer closeout | `validated` | VTX-M02 | HZB occlusion consumer over the landed generic Screen HZB with visibility publication, readback latency, conservative fallbacks, base-pass consumers, diagnostics, and proof aligned to the UE5.7 occlusion/HZB source review recorded in the milestone plan. |
| VTX-M05C | Translucency stage | `validated` | VTX-M03, VTX-M04D.5, VTX-M05A, VTX-M05B | Stage 18 standard forward-lit translucency consuming prepared transparent draws plus lighting/shadow/environment publications. Focused build/tests, senior-review regression remediation, CDB/D3D12 audit, RenderDoc proof, UE5.7 re-check, and user visual confirmation passed. |
| VTX-M05D | Conventional shadow parity and local-light expansion | `validated` | VTX-M03, VTX-M05A, VTX-M05B, VTX-M05C | Directional CSM UE5.7 audit/remediation and local-scale `VsmTwoCubes` validation are closed with Release smoke, recook, RenderDoc capture, and sequential shadow/base-pass analysis proof. Spot-light conventional shadows are validated with focused tests, shader validation, CDB/debug-layer audit, RenderDoc probe, and user visual confirmation. Point-light conventional shadows are validated with cube-array six-face Stage 8 rendering, Stage 12 point-shadow sampling, point proxy winding regression coverage, CDB/debug-layer audit, RenderDoc point-shadow probe, and user visual confirmation. Stage 18 translucent local-light shadow consumption, layered one-pass point cubemap rendering, and VSM remain deferred. |
| VTX-M06A | Multi-view proof closeout | `validated` | VTX-M05A, VTX-M05B, VTX-M05C, VTX-M05D | B1 per-view plan/state-handle substrate, B2 classification payloads, C `PerViewScope` serialized view-family loop, D scene-texture lease pool, E data-driven surface composition, F auxiliary dependency graph, G overlay lanes/view extensions, and H runtime proof tooling are implemented and validated. Standard and auxiliary MultiView proof layouts pass focused tests, CDB/debug-layer audit, RenderDoc scripted analysis, and 60-frame allocation-churn proof, including runtime/capture proof that an auxiliary producer output is extracted and consumed by a dependent view. |
| VTX-M06B | Offscreen proof closeout | `validated` | VTX-M05A, VTX-M05C | `ForOffscreenScene` deferred/forward validation and preview/capture scenarios are implemented and validated. Closure proof covers focused tests, CDB/debug-layer audit, RenderDoc scripted analysis, non-empty offscreen product proof, downstream texture composition, 60-frame allocation-churn proof, and user visual confirmation. |
| VTX-M06C | Feature-gated runtime variants | `validated` | VTX-M06A, VTX-M06B | Depth-only, shadow-only, no-environment, no-shadowing, no-volumetrics, diagnostics-only variants with focused tests, CDB/debug-layer proof, RenderDoc scripted analysis, allocation-churn proof, and user visual confirmation. |
| VTX-M07 | Production readiness and legacy retirement | `validated` | VTX-M06C | Static legacy seam guard, stale demo/doc cleanup, required demo refresh/testing, production proof-suite consolidation, binary dependency audit, current-path doc routing, CDB/RenderDoc closure proof, and safe legacy retirement are validated. |
| VTX-M08 | Skybox and static specified-cubemap SkyLight | `planned` | VTX-M07, VTX-M04D environment publication truth, VTX-M05D shadows, VTX-M06C feature gates | Visual cubemap skybox background plus static specified-cubemap SkyLight diffuse lighting through a reviewed environment-product boundary. Required LLDs must be reviewed before implementation. Captured-scene SkyLight, real-time capture, cubemap blending, SkyLight occlusion, baked/static-lightmap integration, and broader reflection probes remain deferred. |
| VTX-FUTURE | Reserved post-baseline families | `future` | VTX-M08 or explicit reprioritization | Geometry virtualization, material composition, broader indirect lighting/GI/reflections, VSM, clouds, heterogeneous volumes, water, hair, distortion, light shafts. |

## 6. Environment Milestone Decomposition

Environment is the current high-risk lane. Treat it as one family with several
separable closure gates.

### 6.1 VTX-M04D.1 — Publication And Contract Truth

Required work:

- Preserve stable atmosphere/sky and below-horizon behavior.
- Ensure environment frame/view/static/product bindings report only real
  resources or explicit invalid states.
- Replace revision-only IBL semantics with truthful SkyLight/IBL publication.
- Expose Stage 14 local-fog preparation and future volumetric execution state
  at the renderer boundary.
- Add test coverage for enabled, disabled, unavailable, and stale-publication
  states.

Required UE5.7 grounding:

- `SkyAtmosphereRendering`
- `SkyAtmosphereComponent`
- `ExponentialHeightFogComponent`
- `FogRendering`
- `LocalFogVolumeRendering`
- `VolumetricFog`
- environment map / SkyLight capture and filtering paths relevant to the
  selected Oxygen implementation shape

### 6.2 VTX-M04D.2 — Exponential Height Fog

Required work:

- Implement UE5.7-grade authored parameter coverage for primary and secondary
  fog layers.
- Match the relevant `FogRendering` / `HeightFogCommon` algorithms for
  density, falloff, directional inscattering, deferred cubemap inscattering
  state, start/cut
  distance, max opacity, and sky-atmosphere coupling.
- Preserve disabled/no-fog fast paths without turning them into fake parity.
- Add shader tests or capture analysis that prove height fog modifies the
  expected pixels and respects sky/scene depth semantics.

Exit gate:

- `validated` on 2026-04-26.
- UE5.7 grounding covered `FogRendering.cpp`, `HeightFogCommon.ush`, and
  `ExponentialHeightFogComponent.cpp`.
- Focused EnvironmentLightingService and SceneRendererPublication tests pass.
- VortexBasic enabled and disabled RenderDoc proof validates Stage-15 fog
  presence/removal, captured fog static data, SceneColor contribution, and
  disabled fast path.
- City-scale RenderScene capture proof validates the `CityEnvironmentValidation`
  Stage-15 height-fog payload and SceneColor contribution.
- Cubemap inscattering resource binding/sampling remains explicitly deferred
  and is not part of the validated claim.

### 6.3 VTX-M04D.3 — Local Fog Volumes

Required work:

- Implement UE5.7-grade local fog authoring and runtime component coverage.
- Preserve Stage 14 tiled culling over generic Screen HZB products.
- Implement GPU packing, sorting/capping policy, analytical media integral,
  splat/compose behavior, and far-depth/sky exclusion.
- Prove sub-viewport correctness and no-HZB fallback behavior.
- Publish inspectable counters for culled volumes, tiles, draw/dispatch counts,
  skipped states, and resource validity.

Exit gate:

- Local fog volume behavior is validated independently and through VortexBasic
  runtime/capture proof.
- Local-fog injection into volumetric fog is explicitly deferred to VTX-M04D.4.

### 6.4 VTX-M04D.4 — Volumetric Fog

Required work:

- Preserve the first Vortex-native integrated-light-scattering runtime path:
  authored/requested volumetric fog allocates a 3D product, dispatches a
  Stage-14 compute pass, publishes SRV/static binding validity, and composes it
  in Stage 15 fog.
- Implement a UE5.7-informed froxel/grid representation.
- Inject participating media from height fog and local fog where applicable.
- Inject direct light, shadowed light, ambient/SkyLight, and atmosphere
  participation according to the selected parity contract.
- Treat conventional directional CSM projection proof as a closed prerequisite,
  not as remaining volumetric-fog parity. VTX-M04D.4 shadowed-light work must
  consume the validated CSM product; the primary directional volumetric CSM
  sampling path now has focused term proof and city-scale capture proof.
- Integrate scattering and transmittance into a published
  integrated-light-scattering product.
- Add temporal reprojection/history, jitter policy, reset conditions, and
  per-view resource ownership. VTX-M04D.4 implements Halton jitter, reset,
  history-miss supersampling, and reprojection for Oxygen's single
  integrated-scattering product; UE conservative-depth history fixup,
  pre-exposure transfer, and separate `LightScattering` history are documented
  accepted divergences.
- Keep CPU/HLSL ABI and publication contracts lockstep.

Exit gate:

- Authored volumetric fog changes runtime output, not just model revisions.
- Integrated light scattering is published truthfully.
- Tests and capture analysis cover enabled, disabled, history-reset, and
  lighting-participation cases.
- City-scale RenderScene proof covers `CityEnvironmentValidation` with local
  fog, directional volumetric shadows, SkyLight, temporal reprojection, and a
  debugger-backed D3D12 debug-layer audit.

Status: `validated` on 2026-04-26. Evidence is recorded in
`IMPLEMENTATION_STATUS.md`.

### 6.5 VTX-M04D.5 — Runtime Proof

Required work:

- Add one repeatable runtime path that exercises atmosphere, height fog, local
  fog, volumetric fog, aerial perspective, and SkyLight together.
- Prefer `Examples/Async` when the runtime seam is ready, because the PRD names
  it as the canonical runtime validation example.
- Add analyzer/probe coverage for environment bindings, relevant resources,
  pass names, dispatch/draw counts, and expected outputs.

Status: `validated` on 2026-04-26. Evidence is recorded in
`IMPLEMENTATION_STATUS.md`.

Exit gate:

- The runtime proof command, analyzer output, and assertion result are recorded
  in `IMPLEMENTATION_STATUS.md`.
- This milestone must not claim atmosphere runtime closure until VTX-M04D.6
  records aerial-perspective parity evidence. VTX-M04D.6 has validated
  main-view AP proof; reflection/360 AP remains deferred.

### 6.6 VTX-M04D.6 — Aerial Perspective Parity

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

Current evidence:

- VTX-M04D.6 has validated main-view camera AP proof recorded in
  `IMPLEMENTATION_STATUS.md`, including focused VortexBasic enabled/disabled
  captures and a city-scale `CityEnvironmentValidation` capture.
- RenderScene visual validation for the main scene AP path was confirmed on
  2026-04-25 after DemoShell exposed effective AP controls and the compose pass
  used the main AP helper/gate.
- Reflection/360-view AP behavior is explicitly deferred and is not claimed by
  the validated main-view AP package.

Exit gate:

- Aerial perspective has a source-to-target mapping to the relevant UE5.7
  files, implementation deltas are resolved or explicitly accepted as Vortex
  differences, any impact on VTX-M04D.1 and VTX-M04D.2 is implemented and
  revalidated, focused tests pass, shader bake/catalog validation runs where
  shader behavior changes, and runtime/capture proof records that camera
  aerial perspective affects the expected pixels without replacing height fog,
  local fog, volumetric fog, or SkyLight proof.

## 7. Remaining Non-Environment Milestones

### 7.1 VTX-M05A — Diagnostics Product Service

Scope:

- Concrete diagnostics service/files, debug-mode vocabulary, GPU timeline
  publishing, UI/panel/overlay hooks, and failure surfaces.
- Diagnostics may inspect environment, occlusion, lighting, and scene texture
  state through published contracts only.
- Detailed implementation plan:
  [`plan/VTX-M05A-diagnostics-product-service.md`](plan/VTX-M05A-diagnostics-product-service.md).

Out of scope:

- Treating RenderDoc, logs, analyzer scripts, or tests as diagnostics delivery.
  Those remain verification tools unless DiagnosticsService-owned runtime
  behavior changes.

### 7.2 VTX-M05B — Occlusion Consumer Closeout

Scope:

- Occlusion consumer over the already-landed generic Stage 5 Screen HZB:
  visibility-state publication, HZB-test batching, readback latency,
  conservative fallbacks, and consumers in base pass and other compatible draw
  builders where proven, with UE5.7 source mapping recorded in the detailed
  milestone plan.

Dependency note:

- Environment local fog depends on generic Screen HZB publication, not on this
  full occlusion closure.

### 7.3 VTX-M05C — Translucency

Scope:

- Stage 18 forward-lit translucency consuming published lighting, shadow, and
  environment bindings.
- Correct blending over deferred scene color/depth.
- Shader families and material-output contracts for translucent passes.
- Detailed implementation plan:
  [`plan/VTX-M05C-translucency-stage.md`](plan/VTX-M05C-translucency-stage.md).
- M05C is limited to standard alpha-blended translucency. Separate
  translucency, post-DOF, holdout/modulate, OIT, distortion/refraction, and
  translucent shadows are future scope.

### 7.4 VTX-M05D — Conventional Shadow Parity And Local-Light Shadows

Scope:

- Audit directional CSM against UE5.7 before local-light work starts.
- Reproduce, root-cause, and fix city-scale camera-movement instability.
- Extend ShadowService beyond the directional-first baseline.
- Deliver spot-light conventional shadows after CSM stability proof.
- Define and implement the point-light conventional strategy explicitly.
- Keep VSM as a future ShadowService strategy, not part of this milestone.

### 7.5 VTX-M06A — Multi-View Proof

Scope:

- Heterogeneous views in one frame.
- Per-view capability flags, per-view shading mode, PiP/multi-surface routing,
  and product isolation.
- Validate service publications are per-view safe.

### 7.6 VTX-M06B — Offscreen Proof

Scope:

- `ForOffscreenScene` facade validation for scene-derived offscreen rendering.
- Deferred and solid forward-mode permutations.
- Preview/thumbnail/capture use cases.
- Detailed implementation plan:
  [`plan/VTX-M06B-offscreen-proof-closeout.md`](plan/VTX-M06B-offscreen-proof-closeout.md).

### 7.7 VTX-M06C — Feature-Gated Runtime Variants

Scope:

- Depth-only rendering.
- Shadow-only rendering.
- No-environment variant.
- No-shadowing variant.
- No-volumetrics variant.
- Diagnostics-only overlay variant.
- Detailed implementation plan:
  [`plan/VTX-M06C-feature-gated-runtime-variants.md`](plan/VTX-M06C-feature-gated-runtime-variants.md).

Exit gate:

- Each variant compiles, runs, and validates the expected absence/presence of
  subsystem products without crashes or fake publications.

## 8. Validation Policy

Every implementation milestone must define and record:

1. UE5.7 source/shader references checked.
2. Code files changed.
3. Design/status files changed.
4. Focused build commands.
5. Focused tests.
6. Runtime proof command when the milestone exposes runtime behavior.
7. RenderDoc capture/replay/analyzer evidence when GPU pass ordering,
   resources, or visual output are part of the claim.
8. Residual gaps and whether they are accepted, deferred, or blockers.

### 8.1 Minimum Verification By Milestone Class

| Milestone Class | Minimum Verification |
| --- | --- |
| Planning/docs only | Restricted stale-reference scans, status/plan consistency scan, markdown diff check. |
| CPU/service contract | Focused build target, focused unit/integration tests, ABI/static-assert checks when contracts change. |
| Shader/GPU pass | Focused build, ShaderBake/catalog validation, focused tests, capture/analyzer proof when behavior matters. |
| Runtime example | Build example, run headless or scripted entrypoint, collect logs/capture/analyzer/assert evidence. |
| Feature-gated variant | Run each variant, prove expected products are present/absent, prove no misleading publications. |

### 8.2 RenderDoc And Runtime Proof

Capture-backed proof requires:

- wrapper command succeeded
- capture was produced when required
- replay/analyzer execution succeeded
- final assertion gate passed
- reports validate the relevant stage ordering, resources, descriptor payloads,
  draw/dispatch counts, and output expectations

A capture file by itself is not proof.

## 9. Dependency Map

```text
VTX-M00
  └─► VTX-M01
       └─► VTX-M02
            ├─► VTX-M03
            └─► VTX-M04D.1
                 ├─► VTX-M04D.2
                 │    ├─► VTX-M04D.3
                 │    │    └─► VTX-M04D.4
                 │    └─► VTX-M04D.6
                 └─► VTX-M05A

VTX-M04D.2 + VTX-M04D.3 + VTX-M04D.4 + VTX-M04D.6
  └─► VTX-M04D.5

VTX-M03 + VTX-M04D.5
  └─► VTX-M04E
       └─► VTX-M04F
            ├─► VTX-M05A
            ├─► VTX-M05B
            ├─► VTX-M05C
            └─► VTX-M05D
                 └─► VTX-M06A / VTX-M06B
                      └─► VTX-M06C
                           └─► VTX-M07
                                └─► VTX-M08
```

Parallelism rules:

- VTX-M04D.2 height fog and VTX-M04D.3 local fog can overlap after contract
  truth is complete, but their final parity claims must account for coupling.
- VTX-M04D.6 aerial perspective can start after height-fog cleanup because it
  must settle whether AP contains fog contribution or only composes with the
  separate height-fog pass. Any resulting publication or height-fog contract
  change must be propagated back to VTX-M04D.1 and VTX-M04D.2 before AP parity
  can be claimed.
- VTX-M05A diagnostics can start once the environment and renderer publication
  surfaces it inspects are stable enough; it must not reach into private
  service internals.
- VTX-M05B occlusion can proceed independently of environment after VTX-M02.
- VTX-M05C translucency should wait for lighting/shadow/environment
  publications to be stable.
- Multi-view and offscreen remain separate proof milestones.

## 10. Feature Activation Matrix

| Feature Family | Activation / Milestone | Current Status | Target |
| --- | --- | --- | --- |
| Renderer Core substrate | VTX-M01 | `landed_needs_validation` | Engine-integrated frame loop, publication, upload, view lifecycle, composition substrate. |
| Non-runtime facades | VTX-M01, VTX-M06B | `landed_needs_validation` | Single-pass and render-graph harnesses plus offscreen scene facade with proof. |
| SceneTextures | VTX-M01 | `landed_needs_validation` | SceneColor, SceneDepth, PartialDepth, GBufferA-D, Stencil, Velocity, CustomDepth; future GBufferE/F. |
| SceneRenderer shell | VTX-M01 | `landed_needs_validation` | 23-stage dispatch with active and reserved owners. |
| InitViews | VTX-M02 | `landed_needs_validation` | Prepared-scene publication and visibility/culling inputs. |
| Depth prepass | VTX-M02 | `landed_needs_validation` | Stage 3 depth products and completeness tracking. |
| Generic Screen HZB | VTX-M02 | `landed_needs_validation` | Stage 5 current/previous HZB products for consumers. |
| Base pass and velocity | VTX-M02 | `landed_needs_validation` | GBuffer MRT, masked/deformed/skinned/WPO-capable velocity policy. |
| Deferred lighting | VTX-M02 / VTX-M03 | `landed_needs_validation` | Directional fullscreen plus bounded point/spot deferred lighting owned by LightingService. |
| LightingService | VTX-M03 | `landed_needs_validation` | Light selection, forward light publication, light grid, Stage 12 service ownership. |
| ShadowService directional baseline | VTX-M03 / VTX-M04D.4 prerequisite / VTX-M05D | `validated_for_static_projection_reaudit_required` | Directional conventional shadow data publication and projected receiver shadows were validated enough for M04D.4 volumetric consumption, and the city-scale loader preserves cooked manual CSM settings. M05D now owns full UE5.7 CSM parity and camera-stability remediation because `CityEnvironmentValidation` projected shadows are unstable under camera movement. |
| PostProcessService | VTX-M03 | `landed_needs_validation` | Exposure, bloom, tonemap, Stage 22 service. |
| Environment sky/atmosphere | VTX-M04D.1 / VTX-M04D.6 | `validated` for main-view AP | Advanced sky/atmosphere and below-horizon behavior is preserved while publication state is truthful; main-view aerial perspective has focused VortexBasic enabled/disabled proof and city-scale `CityEnvironmentValidation` capture proof. Reflection/360-view AP remains explicitly deferred. |
| SkyLight publication truth | VTX-M04D.1 | `validated` | Real resource publication or explicit invalid state; no revision-only closure. |
| Cubemap skybox / static specified-cubemap SkyLight | VTX-M08 | `planned` | Visual cubemap skybox background, deterministic procedural-sky versus skybox selection, directional sun interaction rules, and static SkyLight diffuse lighting. Captured-scene SkyLight, real-time capture, cubemap blending, SkyLight occlusion, baked/static-lightmap integration, and broader reflection probes remain deferred. |
| Exponential height fog | VTX-M04D.2 | `validated` | UE5.7-informed authored parameters, algorithms, shaders, disabled fast path, focused runtime/capture proof, and city-scale capture proof. Cubemap inscattering resource binding/sampling remains explicitly deferred. |
| City-scale AP/fog artifact remediation | VTX-M04D.2 / VTX-M04D.3 / VTX-M04D.4 / VTX-M04D.6 | `validated` for M04D fog/AP packages | `CityEnvironmentValidation` banding/quality fixes now have implementation/test/runtime-smoke/focused RenderDoc evidence: DemoShell override behavior, local-fog request plumbing, AP LUT resolution/depth defaults, city-scale directional CSM hydration, city-scale height-fog runtime/capture behavior, city-scale volumetric integrated-scattering runtime behavior, and city-scale main-view AP compose proof. Consolidated runtime closure is validated by VTX-M04D.5. Cubemap fog and reflection/360 AP are deferred. |
| Local fog volumes | VTX-M04D.3 | `validated` | Analytical local-fog volume path has UE5.7 source grounding, focused tests, shader validation, VortexBasic runtime/capture proof, draw-args probe evidence, SceneColor contribution proof, and far-depth no-op proof. Local-fog volumetric injection is validated separately under VTX-M04D.4. |
| Volumetric fog | VTX-M04D.4 | `validated` | Integrated-light-scattering runtime path is proven with focused RenderDoc proof for Stage-14 dispatch/product write, captured Stage-15 fog `EnvironmentStaticData` SRV/flag/grid proof, integrated-volume min/max/slice proof, term-isolated VortexBasic captures, and city-scale RenderScene capture proof. Validated terms include log-distributed froxel depth, spatial primary/secondary height-fog media density, primary directional CSM shadowed-light sampling, local-fog participating-media injection through validated Stage-14 tiled products, Oxygen distant-SkyLight volumetric ambient injection, Halton jitter, reset, history-miss supersampling, and reprojection for the Oxygen integrated-scattering product. Real SkyLight cubemap capture/filtering is deferred to the IBL/indirect-lighting resource track. |
| Async runtime migration | VTX-M04E | `validated` | Canonical Async runtime proof path with no compatibility clutter, Stage 3/8/12/15/22 RenderDoc evidence, final present output, and overlay composition proof. |
| Single-view composition/presentation | VTX-M04F | `validated` | Async RenderDoc proof validates Stage 22 tonemap output, exactly one composition copy from `Async.SceneColor` after Stage 22, exactly one overlay blend after the scene copy, final present output, and focused `RendererCompositionQueue` tests. |
| DiagnosticsService | VTX-M05A | `validated` | Product diagnostics surface, overlays/panels/timeline/debug bindings. |
| OcclusionModule | VTX-M05B | `validated` | HZB occlusion consumer and visibility publisher over Screen HZB. |
| TranslucencyModule | VTX-M05C | `validated` | Stage 18 standard forward-lit translucent rendering has focused build/test/CDB/RenderDoc proof and user visual confirmation. |
| Conventional/local-light shadows | VTX-M05D | `validated` | Directional CSM parity/stability audit/remediation is validated. Spot conventional shadows are validated in `SpotShadowValidation` with focused tests, shader validation, CDB/debug-layer audit, RenderDoc probe, and user visual confirmation. Point conventional shadows are validated in `PointShadowValidation` with cube-array six-face depth rendering, Stage 12 point-shadow sampling, focused tests, shader validation, CDB/debug-layer audit, RenderDoc probe, and user visual confirmation. |
| Multi-view | VTX-M06A | `validated` | B1 per-view plan/state-handle substrate, B2 classification payloads, C serialized view-family loop, D scene-texture lease pool, E data-driven surface composition, F auxiliary dependency graph, G overlay lanes/view extensions, and H proof-layout/tooling are implemented and validated with focused tests, CDB/debug-layer audit, RenderDoc scripted analysis, 60-frame allocation-churn proof, and runtime/capture proof that an auxiliary producer output is extracted and consumed by a dependent view. |
| Offscreen rendering | VTX-M06B | `validated` | Scene-derived offscreen facade with deferred/forward coverage, product final-state proof, deferred preview plus solid forward capture runtime proof, RenderDoc scripted analysis, non-empty product proof, CDB/debug-layer audit, downstream composition proof, 60-frame allocation-churn proof, and user visual confirmation. |
| Feature-gated variants | VTX-M06C | `validated` | Feature-profile vocabulary/carry, depth-only/shadow-only stage gates, no-environment/no-shadowing/no-volumetrics gates, diagnostics-only ledger/no-scene-product gates, MultiView runtime proof layout, CDB/debug-layer audit, RenderDoc scripted analysis, 60-frame allocation-churn proof, and user visual confirmation are recorded. |
| Legacy renderer retirement | VTX-M07 | `validated` | Static source seam guard `tools/vortex/Assert-VortexLegacySeams.ps1` passed with tooling over 581 current source/build/tooling files. Stale uncompiled `Examples/MultiView/ImGuiView.*` was removed, DemoShell/TexturedCube/Physics legacy namespace compatibility seams were removed, Async README/tooling now documents current Vortex capture/product proof, current-path docs under `src/Oxygen` were routed to Vortex/Graphics, the full registered example build matrix passed, and short D3D12/debug-layer smokes passed for Async, InputSystem, LightBench, TexturedCube, Physics, RenderScene, VortexBasic, and MultiView. Closure validation also passed the focused Vortex/example build, focused CTest 14/14, current-path doc/source audit with zero matches, `dumpbin /dependents` binary dependency audit with zero legacy renderer dependency matches, and VortexBasic, Async, MultiView standard/auxiliary, Offscreen, and Feature-variant CDB/RenderDoc proof wrappers under `out\build-ninja\analysis\vortex\m07-closeout`. ShaderBake/catalog validation was not run because no shader source, shader ABI, root-binding, or catalog data changed. |
| Geometry virtualization | VTX-FUTURE | `future` | Nanite-class geometry virtualization. |
| Material composition | VTX-FUTURE | `future` | DBuffer/deferred decal/material classification family. |
| Indirect lighting / GI / reflections | VTX-FUTURE | `future` | Canonical indirect environment evaluation and ambient-bridge retirement. |
| VSM | VTX-FUTURE | `future` | ShadowService internal strategy upgrade. |
| Volumetric clouds | VTX-FUTURE | `future` | Environment-owned cloud rendering. |
| Heterogeneous volumes | VTX-FUTURE | `future` | Environment-owned heterogeneous volume family. |
| Water, hair, distortion, light shafts | VTX-FUTURE | `future` | Reserved post-baseline families. |
| Mobile renderer | none | `future` / excluded | Out of scope per PRD. |

## 11. Planning Checklist For New Detailed Milestone Plans

Before starting a detailed implementation plan for any milestone:

- Confirm the milestone ID and scope from this document.
- Read the owning LLD and confirm it is ready for implementation.
- Check `IMPLEMENTATION_STATUS.md` for current status and residual gaps.
- Search current source before assuming work is absent or complete.
- Identify UE5.7 reference files and shader families before writing code.
- Define the exact proof commands and expected artifacts.
- Update docs first if source reality or parity scope contradicts this plan.

The standard workflow for creating those detailed plans is
[plan/milestone-planning-workflow.md](./plan/milestone-planning-workflow.md).

## 12. Exit Criteria For The Production-Complete Desktop Baseline

The production-complete desktop deferred baseline is reached when:

1. VTX-M04E proves the canonical Async runtime path through Vortex.
2. VTX-M04F proves single-view composition/presentation.
3. VTX-M05A through VTX-M05D close diagnostics, occlusion consumers,
   translucency, and local-light conventional shadows.
4. VTX-M06A through VTX-M06C close multi-view, offscreen, and feature-gated
   runtime variants.
5. VTX-M07 validates production readiness and safe legacy retirement.
6. All closure claims are recorded in `IMPLEMENTATION_STATUS.md` with
   implementation files, docs/status updates, tests, runtime/capture evidence
   where applicable, and residual gaps.
