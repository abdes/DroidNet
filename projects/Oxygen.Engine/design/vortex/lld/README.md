# Vortex Low-Level Design Package

This directory contains the low-level design (LLD) documents for the Vortex
renderer. Each document covers one subsystem, stage module, or cross-cutting
concern and is written to the readiness criteria defined in
[PLAN.md §1.5](../PLAN.md).

## Mandatory Vortex Rule

- For Vortex planning and implementation, `Oxygen.Renderer` is legacy dead
  code. It is not production, not a reference implementation, not a fallback,
  and not a simplification path for any Vortex task.
- Every Vortex task must be designed and implemented as a new Vortex-native
  system that targets maximum parity with UE5.7, grounded in
  `F:\Epic Games\UE_5.7\Engine\Source\Runtime` and
  `F:\Epic Games\UE_5.7\Engine\Shaders`.
- No Vortex task may be marked complete until its parity gate is closed with
  explicit evidence against the relevant UE5.7 source and shader references.
- If maximum parity cannot yet be achieved, the task remains incomplete until
  explicit human approval records the accepted gap and the reason the parity
  gate cannot close.

## LLD Readiness Criteria (from PLAN.md §1.5)

Every LLD must contain at minimum:

1. **Scope and context** — what system, why needed, what it replaces or adds
2. **Interface contracts** — public API surface: types, methods, parameters,
   return values, ownership semantics
3. **Data flow and dependencies** — inputs consumed, outputs produced,
   cross-system interactions
4. **Resource management** — GPU resources created/owned, lifecycle, allocation
   strategy
5. **Shader contracts** (if GPU work) — HLSL interfaces, bindless bindings,
   entrypoints, catalog registrations
6. **Stage integration** — which frame stages the system participates in,
   dispatch contract, null-safe behavior
7. **Testability approach** — how to validate the system in isolation and
   within the frame pipeline
8. **Open questions** — any unresolved design choices that block or constrain
   implementation

An LLD is *ready* when all applicable sections are filled, open questions are
resolved or explicitly deferred with rationale, and the document has been
reviewed.

## Document Index

### Phase 1 — Substrate Migration

| Document | Deliverable | Covers |
| -------- | ----------- | ------ |
| [substrate-migration-guide.md](substrate-migration-guide.md) | Phase 1 guidance | Mechanical adaptation rules, namespace/macro/include conventions, per-step migration patterns, file redistribution, Renderer orchestrator stripping |

### Phase 2 — SceneTextures and SceneRenderer Shell

| Document | Deliverable | Covers |
| -------- | ----------- | ------ |
| [scene-textures.md](scene-textures.md) | D.1 | Four-part SceneTextures contract: concrete class, SetupMode, Bindings, Extracts |
| [scene-renderer-shell.md](scene-renderer-shell.md) | D.2 + D.3 | SceneRenderBuilder bootstrap, SceneRenderer shell, 23-stage dispatch skeleton, per-view vs per-frame iteration |

### Phase 3 — Deferred Core

| Document | Deliverable | Covers |
| -------- | ----------- | ------ |
| [sceneprep-refactor.md](sceneprep-refactor.md) | cross-cutting support LLD | Authoritative ScenePrep -> InitViews -> PreparedSceneFrame contract, traversal budget, publication model, multi-view-safe state ownership |
| [runtime-motion-producers.md](runtime-motion-producers.md) | cross-cutting support LLD | Runtime producer architecture for rigid, skinned, morph, WPO, and MVWO motion inputs needed for truthful Stage-9 opaque velocity parity |
| [shader-contracts.md](shader-contracts.md) | D.7 | Vortex shader directory setup, Contracts/, narrow renderer-wide Shared/, family-local deferred-light helpers, GBuffer encode/decode, position reconstruction, BRDF core, EngineShaderCatalog registration |
| [init-views.md](init-views.md) | D.8 | InitViewsModule, visibility/culling orchestration, ScenePrep integration, per-view prepared-scene publication |
| [depth-prepass.md](depth-prepass.md) | D.4 | DepthPrepassModule, depth-only pass, mesh processor, partial velocity writes, DepthPrePassPolicy |
| [base-pass.md](base-pass.md) | D.5 | BasePassModule, GBuffer MRT output, base-pass mesh processor, material shader contract, velocity completion, forward-mode branch |
| [deferred-lighting.md](deferred-lighting.md) | D.6 | Directional fullscreen deferred lighting, bounded-volume point/spot lighting, SceneColor accumulation |

### Phase 4 — Migration-Critical Services

| Document | Deliverable | Covers |
| -------- | ----------- | ------ |
| [lighting-service.md](lighting-service.md) | D.9 | Light grid build, published forward-light package, deferred-lighting ownership transfer, per-view publication |
| [post-process-service.md](post-process-service.md) | D.10 | Branched post family: tonemap, exposure, bloom, temporal AA/TSR slot, post-owned histories |
| [shadow-service.md](shadow-service.md) | D.11 | Conventional shadow map rendering, shadow data product, shadow-to-lighting wire, VSM slot reservation |
| [environment-service.md](environment-service.md) | D.12 | Sky/atmosphere/fog composition, Stage 14 local/volumetric fog ownership, environment-probe / IBL publication |
| [migration-playbook.md](migration-playbook.md) | D.13 | Examples/Async analysis, runtime seam mapping, behavior parity checklist, visual baseline capture |

### Phase 5 — Remaining Services and Scenarios

| Document | Deliverable | Covers |
| -------- | ----------- | ------ |
| [diagnostics-service.md](diagnostics-service.md) | D.14 | GPU debug overlay, ImGui panel infra, profiler port, debug-mode vocabulary |
| [translucency.md](translucency.md) | D.15 | Forward-lit translucency passes consuming published lighting/shadow/environment bindings |
| [occlusion.md](occlusion.md) | D.16 | HZB generation, occlusion queries, temporal handoff |
| [hzb.md](hzb.md) | D.16 supplement | Generic Stage-5 screen-HZB production, publication, history handoff, and consumer-facing bindings separate from occlusion policy |
| [multi-view-composition.md](multi-view-composition.md) | D.17 | Multi-view dispatch, per-view ShadingMode, multi-surface output, PiP, editor viewport |
| [offscreen-rendering.md](offscreen-rendering.md) | D.18 | ForOffscreenScene facade, deferred/forward mode selection, thumbnail/preview |
| [shadow-local-lights.md](shadow-local-lights.md) | VTX-M05D | ShadowService expansion for spot-light and point-light conventional shadows after the M05D directional CSM parity/stability gate |

### Phase 7 — Reserved Future Families

| Document | Deliverable | Covers |
| -------- | ----------- | ------ |
| [indirect-lighting-service.md](indirect-lighting-service.md) | reserved future LLD | Stage-13 owner for GI, reflections, canonical indirect environment evaluation, and retirement of the temporary Phase 4 ambient bridge |

## Relationship to Other Documents

- **[ARCHITECTURE.md](../ARCHITECTURE.md)** — stable conceptual architecture;
  LLDs refine it, never contradict it
- **[DESIGN.md](../DESIGN.md)** — entry point with cross-cutting design
  decisions and links to LLDs
- **[PLAN.md](../PLAN.md)** — execution plan; references LLDs as design
  prerequisites per phase
- **[PROJECT-LAYOUT.md](../PROJECT-LAYOUT.md)** — authoritative file placement;
  LLDs reference it for file locations
- **[IMPLEMENTATION_STATUS.md](../IMPLEMENTATION_STATUS.md)** — tracks
  deliverable completion status
