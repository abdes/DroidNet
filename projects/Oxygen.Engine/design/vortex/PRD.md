# Vortex Renderer PRD

Status: `clarified working PRD`

This document captures the product requirements for the Vortex renderer. It
describes what the renderer must achieve and how success will be judged. It
intentionally avoids solution-level code layout, API minutiae, and
implementation sequencing.

Related:

- [ARCHITECTURE.md](./ARCHITECTURE.md)
- [DESIGN.md](./DESIGN.md)
- [PLAN.md](./PLAN.md)
- [PROJECT-LAYOUT.md](./PROJECT-LAYOUT.md)

## Mandatory Vortex Rule

- Every Vortex task must be designed and implemented as a new Vortex-native
   system that targets maximum parity with UE5.7, grounded in
   `F:\Epic Games\UE_5.7\Engine\Source\Runtime` and
   `F:\Epic Games\UE_5.7\Engine\Shaders`.
- No Vortex task may be marked complete until its parity gate is closed with
   explicit evidence against the relevant UE5.7 source and shader references.
- If maximum parity cannot yet be achieved, the task remains incomplete until
   explicit human approval records the accepted gap and the reason the parity
   gate cannot close.

## 1. Problem Statement

Oxygen has a new renderer— **Vortex** (`Oxygen.Vortex`) — that:

1. Adopts a **deferred-first opaque path** with GBuffer-backed base pass as the
   default desktop contract.
2. Keeps **clustered forward light data** as a shared subsystem for
   translucency, forward-only materials, water, and other special-case forward
   consumers, not as a second general desktop scene mode.
3. Uses **one desktop scene renderer** with feature- and mode-gated branches,
   not parallel ForwardPipeline / DeferredPipeline products.
4. Follows a **UE5-aligned frame structure** with reserved slots for future
   subsystems even before they are implemented.
5. Preserves and reuses Oxygen's **architecture-neutral substrate**: render-graph
   coroutines, bindless descriptor model, uploaders, binders, frame/view
   lifecycle services, and composition infrastructure.

Vortex is under development and still requires several features, enhancements
and adjustments to be developed.

## 3. Goals

1. **Deferred-first desktop renderer.** GBuffer-backed base pass is the default
   opaque scene contract. Forward shading exists only as a special-case path
   for translucency, forward-only materials, and exceptional passes, following
   the pattern proven by leading desktop engines rather than as a second
   general desktop scene mode.
2. **UE5-aligned frame structure.** Top-level orchestration mirrors UE5 desktop
   rendering: depth prepass → occlusion/HZB → light grid → shadows → base
   pass → GBuffer rebuild → deferred lighting → indirect/reflections → sky →
   fog → translucency → post-processing.
3. **Scene textures product.** Introduce a `SceneTextures` product aligned to
   UE5 desktop: SceneColor, SceneDepth, Velocity, GBufferA–F, CustomDepth,
   Stencil, SSAO, LightingChannels. The full logical family is committed at the
   PRD level even when only a phased active subset is populated initially. The
   first active subset is: `SceneColor`, `SceneDepth`, `PartialDepth`,
   `GBufferA-D`, `Stencil`, `Velocity`, and `CustomDepth`. The published
   scene-texture binding ABI reserves stable `GBufferE/F` slots now as invalid
   entries; they become real products in Phase `7E`. `SSAO` and
   `LightingChannelsTexture` remain committed logical family members, but their
   owning publication paths activate later in Phase `7B` and Phase `7E`.
4. **Shared clustered light data.** Clustered light-grid build is a shared
   service consumed by forward translucency, forward-only material paths, and
   debug consumers — not the root pipeline abstraction.
5. **Capability-family services.** Each rendering subsystem (environment,
   shadows, lighting, diagnostics, post-process) is a self-contained service
   with clean domain boundaries, following the extraction pattern established
   in modular-renderer Phase 2.
6. **Reserved future slots.** On-frame orchestration reserves clean integration
   slots for geometry virtualization, GI/reflections, heterogeneous volumes,
   and advanced lighting extensions — initially as no-ops behind feature gates.
7. **No legacy baggage.** The monolithic renderer, its inline domain
   orchestration, and the Forward+-first frame contract are not carried forward.
   Only architecture-neutral substrate migrates.
8. **UE5-aligned module naming.** Subsystem names prefer UE5 conventions where
   the term preserves the architectural role without colliding with Oxygen
   terminology: `SceneRenderer`, `BasePassRendering`, `LightGridInjection`,
   etc. Oxygen should adapt names when a clearer Vortex-local name better
   expresses ownership boundaries, for example using a domain name such as
   `MaterialCompositionService` rather than overloading lighting or
   presentation-composition terminology.
9. **Incremental build-up.** The initial module compiles and links with zero
   domain systems. Each subsystem is added as a self-contained vertical slice.
10. **Preserve Oxygen substrate.** Render-graph-as-coroutine model, bindless
    descriptors, pass base classes, composition infrastructure, facade
    patterns, and reusable upload/bind subsystems carry over from the legacy
    renderer.
11. **Migration-capable first milestone.** The first meaningful product gate is
    not just a compiling shell; it is a Vortex runtime path strong enough to
    migrate a real existing example.
12. **Behavior-preserving migration.** The first migrated example should keep
    its important visible behavior and integrate through stable engine/renderer
    seams rather than forcing renderer-specific application rewrites.
13. **Phase-traceable feature activation.** Every major feature family must be
    traceable in the design and plan documents to a specific activation phase,
    current activation state, and intended final target state.
14. **Correctness-first deferred lighting policy.** The initial deferred
    lighting implementation follows the proven desktop-engine pattern:
    fullscreen lighting for directional lights and bounded deferred/light-volume
    passes for local lights first; tiled or clustered deferred are later
    optimization paths justified by profiling, not day-one architectural
    commitments.

## 4. Non-Goals

1. Break Oxygen defining architectural patterns or design principles in the name
   of pursuing unjustified alignment with other engines.
2. Mobile renderer. This PRD is desktop-only. Mobile is excluded from
   architecture, naming, pass selection, and reports.
3. Replace the coroutine render-graph model with a DAG framework.
4. Turn passes or pipelines into a plugin-facing extension ecosystem.
5. Build a separate desktop "ForwardPipeline" and "DeferredPipeline" as parallel
   products. One desktop scene renderer with branches.
6. Finalize every future pipeline, subsystem, or advanced feature up front.
7. Introduce long-lived temporary compatibility mess just to force early
   migration success. No duplicated scene logic, no parallel legacy/Vortex
   behavior paths inside migrated examples, and no durable bridge layer whose
   only purpose is to mask architectural indecision.
8. Treat whole-scene forward rendering as a co-equal desktop product target for
   Vortex.

## 5. Stakeholders and Users

Primary internal users:

- engine architecture developers
- rendering system developers
- test and harness authors
- tools/editor developers

The design assumes passes and pipelines are engine-authored production code.

## 6. Target Scenarios

### 6.1 Full Runtime Desktop Renderer

The target production renderer:

- deferred-first opaque path with GBuffer base pass
- shared clustered forward light data
- shadow depth rendering (conventional and VSM)
- environment lighting (atmosphere, sky, IBL)
- deferred direct and indirect lighting
- forward translucency
- fog, atmosphere, volumetrics
- post-processing and tone mapping
- composition and presentation

### 6.2 Single Pass Harness

Minimal non-runtime harness for:

- executing one pass against a validated execution context
- focused GPU pass tests
- contract validation for one pass

Carries over from the modular-renderer design. The Vortex renderer inherits
the `ForSinglePassHarness()` facade pattern.

### 6.3 Render Graph Harness

Low-level non-runtime harness for:

- executing one explicit caller-authored render-graph coroutine
- graph-level validation without the full runtime stack

Carries over from the modular-renderer design. The Vortex renderer inherits
the `ForRenderGraphHarness()` facade pattern.

### 6.4 Offscreen Scene Renderer

Higher-level non-runtime path for:

- material preview, thumbnail generation, scene captures, editor previews
- can optionally use deferred or forward path depending on scenario

Carries over from the modular-renderer design. The Vortex renderer inherits
the `ForOffscreenScene()` facade pattern.

### 6.5 Editor Multi-View and Multi-Surface Renderer

A renderer that can support:

- multiple scene views, tool overlays, editor viewport surfaces
- picture-in-picture, minimap-like secondary composition
- per-view pipeline mode selection (deferred vs forward)

### 6.6 Feature-Gated Runtime Variants

Runtime assemblies where some subsystems are intentionally absent:

- no environment lighting
- no shadowing
- no volumetrics
- depth-only rendering
- shadow-only rendering
- diagnostics-only overlays

## 7. Required Outcomes

The Vortex renderer must deliver these architectural outcomes:

1. **One desktop scene renderer.** `SceneRenderer` is the top-level desktop
   orchestrator, not ForwardPipeline.
2. **GBuffer-backed base pass.** Opaque scene materials write to GBufferA–F
   by default.
3. **Scene textures product.** `SceneTextures` provides mandatory desktop
   texture products (SceneColor, SceneDepth, Velocity, GBufferA–F, etc.).
4. **Deferred lighting stage.** Direct lighting reads from GBuffers, not from
   forward shading.
5. **Shared forward light data.** Clustered light-grid build exists as a shared
   service, not as the frame architecture.
6. **Forward special-case path only.** Translucent objects and other
   exceptional forward consumers use forward shading with access to clustered
   light data, but Vortex does not promise full-scene forward rendering as a
   co-equal desktop product.
7. **UE5-aligned frame ordering.** The 23-stage frame structure from the parity
   analysis is the target, with feature-gated stubs where Oxygen does not yet
   implement a subsystem.
8. **Reserved future slots.** Geometry virtualization, GI/reflections,
   heterogeneous volumes, and advanced lighting extensions have clean
   integration slots even as no-ops.
9. **Capability-family services.** Subsystems are self-contained services with
   own Internal/Passes/Types directories.
10. **Non-runtime facades.** ForSinglePassHarness, ForRenderGraphHarness,
    ForOffscreenScene.
11. **Substrate preservation.** Render-graph coroutines, bindless descriptors,
    pass base classes, upload/bind infrastructure, composition queue, facade
    patterns migrate from legacy.
12. **No Forward+-first constraints.** Frame ordering, pass ownership, and
    shader module boundaries are not constrained by Forward+ assumptions.
13. **Real migration proof.** `Examples/Async` runs on Vortex as the first
    migrated runtime example.
14. **Migration without compatibility clutter.** The Async migration does not
    depend on long-lived dual-path hacks, duplicated scene logic, or a
    temporary bridge architecture that would need later cleanup before Vortex
    can be treated as the real successor.
15. **Phase-traceable activation.** Design and planning artifacts explicitly map
    each major feature family to a phase, activation status, and final intended
    target state.
16. **Hermetic renderer separation.** Vortex has no dependency on
    `Oxygen.Renderer` at module, API-contract, runtime-ownership, or migration
    bridge level.
17. **Phase-1 active scene-texture subset.** The first active `SceneTextures`
    set includes `SceneColor`, `SceneDepth`, `PartialDepth`, `GBufferA-D`,
    `Stencil`, `Velocity`, and `CustomDepth`. `SSAO`,
    `LightingChannelsTexture`, and `GBufferE-F` remain part of the committed
    logical family but activate in later documented phases. The published
    `SceneTextureBindings` ABI already reserves stable invalid slots for
    `GBufferE/F` so Phase `7E` can activate them without reshaping the binding
    contract.
18. **Staged deferred-lighting implementation.** The initial deferred-lighting
    path uses a fullscreen deferred pass for directional lights and one-pass
    bounded-volume deferred lighting for point and spot lights; tiled or
    clustered deferred remain explicitly later optimization paths.

## 8. Constraints and Assumptions

1. Desktop-only. Mobile is excluded.
2. Passes are engine-authored and engine-delivered.
3. Pipelines are engine-authored production code.
4. Prefer UE5 module names, pass names, and data-product names when practical.
5. Do not collapse multiple UE5 desktop stages into one Oxygen pass for
   neatness.
6. Any deviation from UE5 structure must be called out explicitly and clear a
   high bar: unavoidable, or clearly better for Oxygen.
7. Phase-1 architecture should favor bounded, production-shaped progress over
   maximal generality.
8. The first meaningful success gate is migration-capable runtime use, not just
   a compiling architectural shell.
9. The first migration target is `Examples/Async`.
10. The first migration must optimize for behavior stability and integration
    stability over example-local convenience rewrites.
11. Legacy renderer support continues for a while after the first migrated
    example; first migration is a proof point, not an immediate removal gate.
12. The PRD commits to the full logical desktop `SceneTextures` family early,
    while design/plan artifacts define the phased active subset.
13. Design and plan documents must maintain explicit phase-by-phase traceability
    for feature activation and final target state.
14. Vortex has no compatibility obligation to legacy renderer API or type
    shapes.
15. Vortex must not depend on the legacy renderer module in any form.
16. Legacy knowledge may inform Vortex decisions, but Vortex should be authored
    as an independent renderer architecture rather than a transplanted legacy
    code shape.
17. The first active `SceneTextures` subset is
    `SceneColor`, `SceneDepth`, `PartialDepth`, `GBufferA-D`, `Stencil`,
    `Velocity`, and `CustomDepth`.
18. `SSAO`, `LightingChannelsTexture`, and `GBufferE-F` are not required in the
    first active subset, but their later activation must remain explicitly
    planned and traceable. The planned owning phases are:
    `SSAO` → `7B`, `LightingChannelsTexture` → `7E`, `GBufferE/F` → `7E`.
19. The initial deferred-lighting path prioritizes correctness, debuggability,
    and migration safety over first-pass optimization complexity.
20. Tiled or clustered deferred are optimization candidates only after the
    baseline deferred path is proven and profiled.

## 9. Success Criteria

The Vortex renderer is successful when:

1. The top-level renderer is `SceneRenderer`, not ForwardPipeline.
2. Opaque scene materials write to GBuffers by default.
3. Direct lighting runs as a deferred stage reading from GBuffers.
4. Clustered forward light data exists as a shared subsystem, not the main
   frame architecture.
5. Forward translucency is a downstream consumer of shared light data.
6. The frame structure follows the UE5-aligned 23-stage ordering with
   feature-gated stubs.
7. `SceneTextures` provides the mandatory desktop texture products.
8. Each subsystem is a self-contained capability-family service.
9. The initial module compiles and links with zero domain systems.
10. All three non-runtime facades work against the Vortex substrate.
11. `Examples/Async` runs on Vortex after a clean migration.
12. That migration preserves the important visible behavior of the example and
    occurs through stable engine/renderer seams.
13. The migration closes the owning UE5.7 parity gate for the important
   visual, workflow, and operational behavior of the example.
14. The migration does not introduce long-lived compatibility clutter.
15. Legacy `Oxygen.Renderer` code may remain in the tree temporarily during
   broader rollout, but it is not a supported parallel production path and it
   does not relax any Vortex parity gate.
16. The design and plan package explicitly shows which `SceneTextures`
    attachments and feature families activate in which phase, with the end
    target remaining the full intended desktop feature set.
17. Vortex remains hermetically independent from the legacy renderer module.
18. The first active `SceneTextures` subset provides
    `SceneColor`, `SceneDepth`, `PartialDepth`, `GBufferA-D`, `Stencil`,
    `Velocity`, and `CustomDepth`.
19. The first deferred-lighting implementation is a correctness-first
    fullscreen-plus-bounded-volume path rather than a tiled or clustered deferred
    optimization path.

## 10. Deferred to Later Phases

The following are intentionally deferred:

- volumetric clouds
- geometry virtualization (Nanite-equivalent)
- GI / reflections (Lumen-equivalent)
- hair strands
- heterogeneous volumes
- MegaLights-class lighting extensions
- single layer water
- full task-targeted global compositor
- richer inter-view dependency mechanisms beyond composition ordering and
  exposure sharing
- mobile renderer path
