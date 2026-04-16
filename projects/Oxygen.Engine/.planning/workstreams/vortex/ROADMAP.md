# Roadmap: Oxygen.Engine Vortex Renderer

## v1.0 Vortex Initial Release

**Milestone Goal:** Deliver Vortex as a deferred-first, migration-capable
desktop renderer for Oxygen.Engine, validate runtime and non-runtime scenarios,
then retire the legacy renderer once the new path is proven.

## Overview

This roadmap mirrors `design/vortex/PLAN.md` in GSD-native form. It starts with
Phase 0 scaffold completion, moves through substrate migration and deferred-core
bring-up, reaches the required first-success gate at `Examples/Async`
migration, then validates extended runtime scenarios before legacy retirement.
Phase 7 reserves explicit post-release capability slots so future work remains
phase-traceable.

## Phases

- [x] **Phase 0: Scaffold and Build Integration** - Wire `Oxygen.Vortex` into (completed 2026-04-13)
      the build graph and verify the empty target.
- [x] **Phase 1: Substrate Migration** - Move architecture-neutral renderer (completed 2026-04-13)
      substrate into Vortex without introducing new rendering systems.
- [x] **Phase 2: SceneTextures and SceneRenderer Shell** - Establish (completed 2026-04-14)
      `SceneTextures`, `SceneRenderBuilder`, and the 23-stage `SceneRenderer`
      shell, then close remediation and human review.
- [x] **Phase 3: Deferred Core** - Bring up shader foundation, depth prepass, (completed 2026-04-15)
      base pass, and correctness-first deferred lighting.
- [ ] **Phase 4: Migration-Critical Services and First Migration** - Activate
      lighting, post-process, shadows, and environment, then migrate
      `Examples/Async`.
- [ ] **Phase 5: Remaining Services and Runtime Scenarios** - Complete
      diagnostics, translucency, occlusion, multi-view, offscreen, and
      feature-gated validation.
- [ ] **Phase 6: Legacy Deprecation** - Port remaining examples/tests and
      remove `Oxygen.Renderer` from the build.
- [ ] **Phase 7: Future Capabilities (Post-Release)** - Reserve explicit
      activation slots for advanced geometry, lighting, shadows, effects,
      extended scene textures, and advanced composition.

## Phase Details

### Phase 0: Scaffold and Build Integration
**Goal**: Vortex exists in the build graph and compiles as an empty target.
**Depends on**: Nothing (first phase)
**Requirements**: [FOUND-01]
**Success Criteria** (what must be TRUE):
  1. `Oxygen.Vortex` is wired into parent CMake and discovered by normal builds.
  2. The `oxygen::vortex` target alias resolves correctly.
  3. An empty Vortex target compiles and links without special-case handling.
**Plans**: 2 plans

Plans:
- [x] 00-01: Wire Vortex into the parent build graph
- [x] 00-02: Verify empty target build and alias resolution

### Phase 1: Substrate Migration
**Goal**: Architecture-neutral renderer substrate lives in Vortex with no new
systems and no dependency on the legacy renderer module.
**Depends on**: Phase 0
**Requirements**: [FOUND-02, FOUND-03]
**Design Docs**: [`lld/substrate-migration-guide.md`](../../design/vortex/lld/substrate-migration-guide.md)
**Success Criteria** (what must be TRUE):
  1. Cross-cutting types, upload/resources, scene prep, internals, and pass
     bases compile under `Oxygen.Vortex`.
  2. View assembly and composition infrastructure live in Vortex-owned layout.
  3. `vortex::Renderer` instantiates and frame-loop hooks execute without
     relying on `Oxygen.Renderer`.
**Plans**: 14 plans

Plans:
- [x] 01-01: Migrate cross-cutting types
- [x] 01-02: Migrate upload foundation and staging
- [x] 01-03: Finish upload orchestration and record steps 1.1-1.2
- [x] 01-04: Land the prerequisite ABI bundle required by resources
- [x] 01-05: Migrate resources and close step 1.3
- [x] 01-06: Migrate the remaining ScenePrep data and configuration
- [x] 01-07: Finish ScenePrep execution and selected internals
- [x] 01-08: Land only the public step-1.7 headers
- [x] 01-09: Rehome step-1.7 private composition infrastructure
- [x] 01-10: Migrate pass bases with the later-wave root contracts and strip the renderer orchestrator
- [x] 01-11: Run Vortex smoke plus legacy substrate regressions
- [x] 01-12: Remove the last FramePlanBuilder source seam
- [x] 01-13: Add the Vortex-local hermeticity guard and refresh proof
- [x] 01-14: Repair reopened composition/capability/boundary gaps with Vortex-only regression proof

### Phase 2: SceneTextures and SceneRenderer Shell
**Goal**: Implement the `SceneTextures` four-part contract and the
`SceneRenderer` shell with fixed 23-stage ordering.
**Depends on**: Phase 1
**Requirements**: [SCENE-01, SCENE-02, SCENE-03]
**Design Docs**: [`lld/scene-textures.md`](../../design/vortex/lld/scene-textures.md), [`lld/scene-renderer-shell.md`](../../design/vortex/lld/scene-renderer-shell.md)
**Success Criteria** (what must be TRUE):
  1. `SceneRenderer` is bootstrapped through `SceneRenderBuilder`.
  2. The first active `SceneTextures` subset exists:
     `SceneColor`, `SceneDepth`, `PartialDepth`, `GBufferA-D`, `Stencil`,
     `Velocity`, and `CustomDepth`.
  3. The 23-stage dispatch skeleton runs as null-safe no-op stages.
  4. Remediation closes architecture and LLD review findings before the phase
     is marked complete.
**Plans**: 8 plans

Plans:
- [x] 02-01: Seed Wave 0 proof surfaces and close the shell vocabulary seams
- [x] 02-02: Implement the full `SceneTextures` contract and routing slot
- [x] 02-03: Wire the `SceneRenderer` shell and lock the bootstrap extent contract
- [x] 02-04: Land renderer-owned publication plus thin resolve/cleanup hooks
- [x] 02-05: Run the explicit human review and approval gate
- [x] 02-06: Repair the Phase 2 scene-texture contract ownership and naming seams
- [x] 02-07: Repair descriptor-backed routing and explicit handoff artifacts
- [x] 02-08: Repair the multi-view shell seam, publication probe, stage directories, and ledger sync

### Phase 3: Deferred Core
**Goal**: Deliver the first lit deferred Vortex frame with shader contracts,
depth prepass, base pass, and correctness-first deferred lighting.
**Depends on**: Phase 2
**Requirements**: [SHDR-01, DEFR-01, DEFR-02]
**Design Docs**: [`lld/shader-contracts.md`](../../design/vortex/lld/shader-contracts.md), [`lld/init-views.md`](../../design/vortex/lld/init-views.md), [`lld/depth-prepass.md`](../../design/vortex/lld/depth-prepass.md), [`lld/base-pass.md`](../../design/vortex/lld/base-pass.md), [`lld/deferred-lighting.md`](../../design/vortex/lld/deferred-lighting.md)
**Success Criteria** (what must be TRUE):
  1. Vortex shader contracts and entrypoints compile through `ShaderBake` and
     the engine shader catalog.
  2. Opaque rendering writes GBuffers by default through the Vortex base pass.
  3. Deferred direct lighting reads GBuffers and produces a visually correct lit
     scene using fullscreen/stencil-bounded techniques.
**Plans**: 17 plans

Plans:
- [x] 03-01: Seed shared scene/view shader contracts
- [x] 03-02: Seed depth/base shader files and initial ShaderBake registrations
- [x] 03-03: Implement InitViews module shell integration
- [x] 03-04: Prove InitViews publication and active-view rebinding
- [x] 03-05: Implement depth-prepass stage shell integration
- [x] 03-06: Finish depth-prepass draw processing and publication proof
- [x] 03-07: Implement base-pass stage shell integration
- [x] 03-08: Implement base-pass shader and mesh-processing surface
- [x] 03-09: Prove Stage 10 promotion and GBuffer publication timing
- [x] 03-10: Complete velocity production and proof for dynamic geometry
- [x] 03-11: Add GBuffer debug visualization and debug-shader proof
- [x] 03-12: Add deferred-light shader family and final lighting registrations
- [x] 03-13: Implement Stage 12 CPU deferred-light path and stencil-local-light proof
- [x] 03-14: Tighten the automated Phase 3 proof sweep
- [x] 03-15: Add automated RenderDoc capture analysis and Phase 3 ledger closeout
- [x] 03-16: Publish draw-frame bindings and execute a real Stage 9 base pass
- [x] 03-17: Execute real Stage 12 deferred lighting and repair closeout truthfulness

Execution note:
Phase 03 remediation landed on 2026-04-15. The renderer now records real
Stage 3, Stage 9, and Stage 12 work, and the repo-owned deferred-core closeout
runner passes again with Phase 04 RenderDoc runtime validation still deferred.

### Phase 4: Migration-Critical Services and First Migration
**Goal**: Activate the minimum service set required for real runtime migration,
then port `Examples/Async` and validate presentation end to end.
**Depends on**: Phase 3
**Requirements**: [LIGHT-01, SHDW-01, ENV-01, POST-01, MIGR-01, COMP-01, HARN-01]
**Design Docs**: [`lld/lighting-service.md`](../../design/vortex/lld/lighting-service.md), [`lld/post-process-service.md`](../../design/vortex/lld/post-process-service.md), [`lld/shadow-service.md`](../../design/vortex/lld/shadow-service.md), [`lld/environment-service.md`](../../design/vortex/lld/environment-service.md), [`lld/migration-playbook.md`](../../design/vortex/lld/migration-playbook.md)
**Success Criteria** (what must be TRUE):
  1. Lighting, shadows, environment, and post-process are active in Vortex.
  2. `Examples/Async` runs on Vortex with near-parity visual and behavioral
     output and no long-lived compatibility clutter.
  3. Composition/presentation, `ForSinglePassHarness()`, and
     `ForRenderGraphHarness()` are validated against the migrated runtime.
**Plans**: 6 plans

Plans:
- [ ] 04-01: Activate LightingService and forward light data
- [ ] 04-02: Activate PostProcessService for visible output
- [ ] 04-03: Activate ShadowService and integrate shadow terms
- [ ] 04-04: Activate EnvironmentLightingService
- [ ] 04-05: Migrate and validate `Examples/Async`
- [ ] 04-06: Validate composition and presentation end to end

### Phase 5: Remaining Services and Runtime Scenarios
**Goal**: Finish the remaining service set and validate the broader runtime and
non-runtime scenarios committed by the PRD.
**Depends on**: Phase 4
**Requirements**: [DIAG-01, TRAN-01, OCCL-01, VIEW-01, OFFS-01, VARI-01]
**Design Docs**: [`lld/diagnostics-service.md`](../../design/vortex/lld/diagnostics-service.md), [`lld/translucency.md`](../../design/vortex/lld/translucency.md), [`lld/occlusion.md`](../../design/vortex/lld/occlusion.md), [`lld/multi-view-composition.md`](../../design/vortex/lld/multi-view-composition.md), [`lld/offscreen-rendering.md`](../../design/vortex/lld/offscreen-rendering.md)
**Success Criteria** (what must be TRUE):
  1. Diagnostics, translucency, and occlusion work on top of the deferred core.
  2. Multi-view, multi-surface, PiP, and per-view shading mode scenarios work.
  3. Offscreen and feature-gated runtime variants behave correctly without
     crashes or hidden coupling.
**Plans**: 6 plans

Plans:
- [ ] 05-01: Add diagnostics service and tooling surfaces
- [ ] 05-02: Add forward-lit translucency
- [ ] 05-03: Add occlusion and HZB
- [ ] 05-04: Validate multi-view and per-view mode scenarios
- [ ] 05-05: Validate offscreen rendering and facades
- [ ] 05-06: Validate feature-gated runtime variants

### Phase 6: Legacy Deprecation
**Goal**: Complete the broader migration and remove the legacy renderer only
after Vortex is the proven path.
**Depends on**: Phase 5
**Requirements**: [LEG-01]
**Success Criteria** (what must be TRUE):
  1. Remaining examples and tests run successfully on Vortex.
  2. `Oxygen.Renderer` is marked deprecated and then removed from the build.
  3. The repo has no remaining required dependency on the legacy renderer path.
**Plans**: 2 plans

Plans:
- [ ] 06-01: Port remaining examples and tests
- [ ] 06-02: Deprecate and remove `Oxygen.Renderer`

### Phase 7: Future Capabilities (Post-Release)
**Goal**: Keep advanced feature families explicitly phase-mapped so future work
can proceed without re-opening architecture-level planning.
**Depends on**: Phase 6
**Requirements**: [FUT-01, FUT-02, FUT-03, FUT-04, FUT-05, FUT-06, FUT-07, FUT-08, FUT-09, FUT-10, FUT-11]
**Success Criteria** (what must be TRUE):
  1. Every deferred post-release family has a named owner and stage slot.
  2. Future work can plan against concrete requirement IDs without renumbering
     the release roadmap.
  3. Post-release capability activation preserves the fixed 23-stage
     architecture.
**Plans**: 6 plans

Plans:
- [ ] 07-01: Advanced geometry and material composition
- [ ] 07-02: Advanced lighting, GI, and many-light extensions
- [ ] 07-03: Advanced shadowing
- [ ] 07-04: Advanced effects and specialized rendering families
- [ ] 07-05: Extended scene texture resources
- [ ] 07-06: Advanced composition and dependency routing

## Progress

**Execution Order:**
Phases execute in numeric order: 0 → 1 → 2 → 3 → 4 → 5 → 6 → 7

| Phase | Plans Complete | Status | Completed |
|-------|----------------|--------|-----------|
| 0. Scaffold and Build Integration | 2/2 | Complete | 2026-04-13 |
| 1. Substrate Migration | 14/14 | Complete | 2026-04-13 |
| 2. SceneTextures and SceneRenderer Shell | 8/8 | Complete | 2026-04-14 |
| 3. Deferred Core | 15/17 | Blocked | - |
| 4. Migration-Critical Services and First Migration | 0/6 | Not started | - |
| 5. Remaining Services and Runtime Scenarios | 0/6 | Not started | - |
| 6. Legacy Deprecation | 0/2 | Not started | - |
| 7. Future Capabilities (Post-Release) | 0/6 | Not started | - |
