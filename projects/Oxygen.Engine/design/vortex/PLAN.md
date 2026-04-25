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
- `EnvironmentLightingService` is substantial but not fully parity-closed.
  Sky/atmosphere and below-horizon handling are advanced; height fog, local
  fog, volumetric fog, and real SkyLight capture/filtering remain open parity
  work.
- `DiagnosticsService`, `TranslucencyModule`, and the full `OcclusionModule`
  are not concrete runtime closures yet, even though supporting substrate and
  some placeholder directories exist.
- Multi-view and offscreen substrates exist, but their runtime proof gates are
  separate late milestones.

## 4. Next Milestone

### NEXT: VTX-M04D — Environment / Fog Parity Closure

**Status:** `in_progress`

**Purpose:** Close the environment family required by the migration-capable
desktop baseline: atmosphere/sky preservation, truthful SkyLight/IBL
publication, UE5.7-grade height fog, local fog volumes, volumetric fog, and
runtime proof.

**Why this is next:** The environment service is already active and now blocks
the migration-capable runtime target. Fog is explicitly not satisfactory, and
the current publication surface can still look more complete than the runtime
behavior really is. Keep the work inside Phase 4D and do not advance to Async
migration or Phase 5 until the environment/fog parity gates have evidence.

**Completed work package:** `VTX-M04D.1 — Environment Publication And Sky/Fog
Contract Truth`. This stabilized what downstream code, diagnostics, tests, and
runtime proof can observe before deeper fog parity work lands.

**Next work package:** `VTX-M04D.2 — UE5.7 Exponential Height Fog Parity`.

**Detailed plan:** [plan/VTX-M04D.2-exponential-height-fog-parity.md](./plan/VTX-M04D.2-exponential-height-fog-parity.md)

**In scope:**

- Preserve the stable sky/atmosphere and below-horizon invariants captured in
  `environment-service.md`.
- Audit `EnvironmentFrameBindings`, `EnvironmentStaticData`,
  `EnvironmentViewData`, and `EnvironmentViewProducts` so every published slot
  is either backed by a real runtime product or explicitly invalid by design.
- Make SkyLight/IBL publication truthful: enabled SkyLight must publish usable
  resources when inputs exist, or an explicitly reported invalid/unavailable
  state when it cannot.
- Expose Stage 14 environment state through the `SceneRenderer` boundary so
  local-fog and future volumetric proof is not trapped inside private service
  inspectors.
- Split environment validation into clear positive and negative tests:
  disabled, authored-but-unavailable, and fully published states.
- Update this plan and `IMPLEMENTATION_STATUS.md` with evidence.

**Out of scope:**

- UE5.7-complete height-fog shading.
- UE5.7-complete local-fog volume rendering.
- UE5.7-complete volumetric fog.
- Volumetric clouds, heterogeneous volumes, and water.
- Runtime migration proof through Async; that follows once the contract is
  truthful and fog implementations exist.

**Dependencies:**

- Current `EnvironmentLightingService` implementation.
- Generic Stage 5 Screen HZB publication for local-fog consumers.
- Refreshed `environment-service.md` LLD.
- CPU/HLSL ABI lockstep for environment binding and data structs.

**Exit gate:**

- Focused environment tests prove truthful SkyLight/IBL and environment
  publication states.
- Renderer publication tests can observe Stage 14/15 environment execution
  state at the `SceneRenderer` boundary.
- No model/product slot reports a misleading valid state.
- `IMPLEMENTATION_STATUS.md` records files changed, tests run, UE5.7 references
  checked, and any accepted residual gap.

**Recommended verification:**

```powershell
cmake --build --preset windows-debug --target Oxygen.Vortex.EnvironmentLightingService Oxygen.Vortex.SceneRendererPublication
ctest --preset test-debug -R "Oxygen.Vortex.(EnvironmentLightingService|SceneRendererPublication)" --output-on-failure
```

If the milestone changes shader contracts, also run the shader bake path used
by the Vortex shader-contract LLD and record the exact command/result.

## 5. Milestone Roadmap

The roadmap is ordered by dependency and proof value. Milestone IDs are stable
planning handles; do not renumber them when scopes are refined.

| ID | Milestone | Status | Depends On | Primary Outcome |
| --- | --- | --- | --- | --- |
| VTX-M00 | Planning and status truth surface | `validated` | current docs/source | `PLAN.md` and `IMPLEMENTATION_STATUS.md` are the canonical planning/status pair. |
| VTX-M01 | Renderer Core and SceneRenderer baseline | `landed_needs_validation` | none | Vortex module, Renderer Core substrate, SceneRenderer shell, SceneTextures, core facades, resolve/cleanup. |
| VTX-M02 | Deferred core visual path | `landed_needs_validation` | VTX-M01 | InitViews, depth prepass, generic HZB, base pass/GBuffer/velocity, Stage 10 publication, deferred lighting, debug views. |
| VTX-M03 | Migration-critical non-environment services | `landed_needs_validation` | VTX-M02 | LightingService, ShadowService directional baseline, PostProcessService, and their publication seams. |
| VTX-M04D | Environment / fog parity closure | `in_progress` | VTX-M02, VTX-M03 validation context | Parent milestone for environment publication truth, aerial perspective, height fog, local fog, volumetric fog, and runtime proof. |
| VTX-M04D.1 | Environment publication and sky/fog contract truth | `validated` | VTX-M02, current environment code | Truthful environment binding/publication state, SkyLight/IBL truth, Stage 14 observability. |
| VTX-M04D.2 | UE5.7 exponential height fog parity | `in_progress` | VTX-M04D.1 | Height fog authored parameters, algorithms, shaders, sky/lighting coupling, tests/proof. |
| VTX-M04D.3 | UE5.7 local fog volume parity | `in_progress` | VTX-M04D.1, VTX-M04D.2, Stage 5 HZB | Local fog authored components, tiled culling, HZB use, splat/compose, sky-depth exclusion, tests/proof. |
| VTX-M04D.4 | UE5.7 volumetric fog parity | `in_progress` | VTX-M04D.1, VTX-M04D.2, VTX-M04D.3 | Froxel grid, media injection, lighting/shadowing, history/reprojection, integrated scattering product. |
| VTX-M04D.5 | Environment runtime proof and Async preparation | `planned` | VTX-M04D.2, VTX-M04D.3, VTX-M04D.4, VTX-M04D.6 | One runtime proof path exercises atmosphere, aerial perspective, height fog, local fog, volumetric fog, and SkyLight. |
| VTX-M04D.6 | UE5.7 aerial perspective parity | `planned` | VTX-M04D.1, VTX-M04D.2 | Camera aerial-perspective volume generation/sampling, main-pass application, height-fog coupling, and capture/test proof. |
| VTX-M04E | Async migration parity gate | `planned` | VTX-M03, VTX-M04D.5 | `Examples/Async` runs through Vortex with no long-lived compatibility clutter and captures proof. |
| VTX-M04F | Single-view composition and presentation closeout | `planned` | VTX-M04E | Single-view composition, resolve, handoff, post-process, and presentation proof. |
| VTX-M05A | Diagnostics product service | `planned` | VTX-M04D.1, VTX-M04F | Concrete diagnostics product surface; not confused with proof tooling. |
| VTX-M05B | Occlusion consumer closeout | `planned` | VTX-M02 | Full occlusion/query/visibility policy over the landed generic Screen HZB. |
| VTX-M05C | Translucency stage | `planned` | VTX-M03, VTX-M04D.5 | Stage 18 forward-lit translucency consuming lighting/shadow/environment publications. |
| VTX-M05D | Local-light conventional shadow expansion | `planned` | VTX-M03 | Spot-light conventional shadows, then explicit point-light strategy under ShadowService. |
| VTX-M06A | Multi-view proof closeout | `planned` | VTX-M05A, VTX-M05B, VTX-M05C | Heterogeneous per-view rendering, PiP/multi-surface routing, per-view capability gating. |
| VTX-M06B | Offscreen proof closeout | `planned` | VTX-M05A, VTX-M05C | `ForOffscreenScene` deferred/forward validation and preview/capture scenarios. |
| VTX-M06C | Feature-gated runtime variants | `planned` | VTX-M06A, VTX-M06B | Depth-only, shadow-only, no-environment, no-shadowing, no-volumetrics, diagnostics-only variants. |
| VTX-M07 | Production readiness and legacy retirement | `planned` | VTX-M06C | All required examples/tests ported; legacy renderer removal path is safe and documented. |
| VTX-FUTURE | Reserved post-baseline families | `future` | VTX-M07 or explicit reprioritization | Geometry virtualization, material composition, indirect lighting/GI/reflections, VSM, clouds, heterogeneous volumes, water, hair, distortion, light shafts. |

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
  density, falloff, directional inscattering, cubemap inscattering, start/cut
  distance, max opacity, and sky-atmosphere coupling.
- Preserve disabled/no-fog fast paths without turning them into fake parity.
- Add shader tests or capture analysis that prove height fog modifies the
  expected pixels and respects sky/scene depth semantics.

Exit gate:

- Height fog is visually and structurally validated against UE5.7 references.
- Tests cover authored parameters, disabled state, sky-depth interaction, and
  at least one scene-depth path.

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

- Local fog volume behavior is validated independently and through the
  SceneRenderer boundary.
- The plan does not claim local fog complete if height fog or volumetric fog
  coupling is still missing where the UE5.7 contract requires it.

### 6.4 VTX-M04D.4 — Volumetric Fog

Required work:

- Implement a UE5.7-informed froxel/grid representation.
- Inject participating media from height fog and local fog where applicable.
- Inject direct light, shadowed light, ambient/SkyLight, and atmosphere
  participation according to the selected parity contract.
- Integrate scattering and transmittance into a published
  integrated-light-scattering product.
- Add temporal reprojection/history, jitter policy, reset conditions, and
  per-view resource ownership.
- Keep CPU/HLSL ABI and publication contracts lockstep.

Exit gate:

- Authored volumetric fog changes runtime output, not just model revisions.
- Integrated light scattering is published truthfully.
- Tests and capture analysis cover enabled, disabled, history-reset, and
  lighting-participation cases.

### 6.5 VTX-M04D.5 — Runtime Proof

Required work:

- Add one repeatable runtime path that exercises atmosphere, height fog, local
  fog, volumetric fog, aerial perspective, and SkyLight together.
- Prefer `Examples/Async` when the runtime seam is ready, because the PRD names
  it as the canonical runtime validation example.
- Add analyzer/probe coverage for environment bindings, relevant resources,
  pass names, dispatch/draw counts, and expected outputs.

Exit gate:

- The runtime proof command, analyzer output, and assertion result are recorded
  in `IMPLEMENTATION_STATUS.md`.
- This milestone must not claim atmosphere runtime closure until VTX-M04D.6
  records aerial-perspective parity evidence.

### 6.6 VTX-M04D.6 — Aerial Perspective Parity

Required work:

- Audit Vortex camera aerial-perspective generation, sampling, and main-pass
  application against UE5.7 `SkyAtmosphere` and base-pass usage.
- Replace stale comments or implementation shortcuts that still describe the
  removed midpoint Beer-Lambert approximation or imply unverified parity.
- Verify camera-volume depth mapping, start-depth behavior, slice-center
  sampling, transmittance storage, exposure handling, orthographic behavior,
  reflection-capture or 360-view behavior, and per-view resource validity.
- Define and implement the height-fog coupling contract used when aerial
  perspective is composed with exponential height fog.
- Add focused tests and shader/capture evidence for enabled, disabled,
  authored parameter changes, resource-unavailable states, and main-pass
  application.

Exit gate:

- Aerial perspective has a source-to-target mapping to the relevant UE5.7
  files, implementation deltas are resolved or explicitly accepted as Vortex
  differences, focused tests pass, shader bake/catalog validation runs where
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

Out of scope:

- Treating RenderDoc, logs, analyzer scripts, or tests as diagnostics delivery.
  Those remain verification tools unless DiagnosticsService-owned runtime
  behavior changes.

### 7.2 VTX-M05B — Occlusion Consumer Closeout

Scope:

- Full occlusion policy over the already-landed generic Stage 5 Screen HZB:
  query/consumer behavior, visibility-state publication, batching, temporal
  handoff, and consumers in base pass and lighting where applicable.

Dependency note:

- Environment local fog depends on generic Screen HZB publication, not on this
  full occlusion closure.

### 7.3 VTX-M05C — Translucency

Scope:

- Stage 18 forward-lit translucency consuming published lighting, shadow, and
  environment bindings.
- Correct blending over deferred scene color/depth.
- Shader families and material-output contracts for translucent passes.

### 7.4 VTX-M05D — Local-Light Conventional Shadows

Scope:

- Extend ShadowService beyond the directional-first baseline.
- Deliver spot-light conventional shadows first.
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
- Deferred and forward-mode permutations.
- Preview/thumbnail/capture use cases.

### 7.7 VTX-M06C — Feature-Gated Runtime Variants

Scope:

- Depth-only rendering.
- Shadow-only rendering.
- No-environment variant.
- No-shadowing variant.
- No-volumetrics variant.
- Diagnostics-only overlay variant.

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
```

Parallelism rules:

- VTX-M04D.2 height fog and VTX-M04D.3 local fog can overlap after contract
  truth is complete, but their final parity claims must account for coupling.
- VTX-M04D.6 aerial perspective can start after height-fog cleanup because it
  must settle whether AP contains fog contribution or only composes with the
  separate height-fog pass.
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
| ShadowService directional baseline | VTX-M03 | `landed_needs_validation` | Directional conventional shadow data publication. |
| PostProcessService | VTX-M03 | `landed_needs_validation` | Exposure, bloom, tonemap, Stage 22 service. |
| Environment sky/atmosphere | VTX-M04D.1 / VTX-M04D.6 | `in_progress` | Advanced sky/atmosphere and below-horizon behavior is preserved while publication state is truthful; aerial perspective parity remains a dedicated proof gap. |
| SkyLight / IBL | VTX-M04D.1 | `validated` | Real resource publication or explicit invalid state; no revision-only closure. Real capture/filtering remains a later implementation gap. |
| Exponential height fog | VTX-M04D.2 | `in_progress` | UE5.7-grade authored parameters, algorithms, shaders, and coupling. |
| Local fog volumes | VTX-M04D.3 | `in_progress` | UE5.7-grade authoring, culling, splat/compose, HZB, sky exclusion, proof. |
| Volumetric fog | VTX-M04D.4 | `in_progress` | UE5.7-grade froxel, injection, lighting, history, integrated scattering. |
| Async runtime migration | VTX-M04E | `planned` | Canonical runtime proof path with no compatibility clutter. |
| DiagnosticsService | VTX-M05A | `planned` | Product diagnostics surface, overlays/panels/timeline/debug bindings. |
| OcclusionModule | VTX-M05B | `planned` | Full occlusion query/consumer policy over Screen HZB. |
| TranslucencyModule | VTX-M05C | `planned` | Stage 18 forward-lit translucent rendering. |
| Local-light shadows | VTX-M05D | `planned` | Spot and point conventional shadows under ShadowService. |
| Multi-view | VTX-M06A | `planned` | Multi-view, PiP, per-view shading/capability proof. |
| Offscreen rendering | VTX-M06B | `planned` | Scene-derived offscreen facade with deferred/forward coverage. |
| Feature-gated variants | VTX-M06C | `planned` | PRD runtime variant matrix. |
| Legacy renderer retirement | VTX-M07 | `planned` | Vortex is the sole supported renderer path; legacy removal is safe. |
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
5. All closure claims are recorded in `IMPLEMENTATION_STATUS.md` with
   implementation files, docs/status updates, tests, runtime/capture evidence
   where applicable, and residual gaps.
