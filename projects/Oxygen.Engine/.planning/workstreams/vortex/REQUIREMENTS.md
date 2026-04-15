# Requirements: Oxygen.Engine Vortex Renderer

**Defined:** 2026-04-13
**Core Value:** Ship a production-credible Vortex renderer that proves itself
through real runtime migration and verified rendering behavior, not through
architecture documents alone.

## v1 Requirements

Requirements for the initial Vortex release and legacy replacement path.

### Foundation and Separation

- [ ] **FOUND-01**: Vortex exists in the build graph as a discoverable
      `Oxygen.Vortex` target that compiles and links with an empty source set.
- [x] **FOUND-02**: Architecture-neutral substrate code moves into
      `Oxygen.Vortex` while preserving buildability and frame-loop integrity.
- [x] **FOUND-03**: Vortex does not depend on `Oxygen.Renderer` through module,
      API-contract, runtime ownership, or bridge seams.

### Scene Renderer Core

- [x] **SCENE-01**: `SceneRenderer` is the top-level desktop renderer with the
      fixed UE5-aligned 23-stage frame structure.
- [x] **SCENE-02**: `SceneTextures` provides the first active subset:
      `SceneColor`, `SceneDepth`, `PartialDepth`, `GBufferA-D`, `Stencil`,
      `Velocity`, and `CustomDepth`.
- [x] **SCENE-03**: Scene texture setup-state tracking, bindless bindings, and
      extraction/handoff contracts exist as explicit renderer-owned surfaces.

### Shader and Deferred Rendering

- [ ] **SHDR-01**: Vortex shader contracts, entrypoints, and shared libraries
      compile through `ShaderBake` and engine shader catalog registration.
- [ ] **DEFR-01**: Opaque scene rendering writes GBuffers by default through the
      Vortex base pass.
- [ ] **DEFR-02**: Deferred direct lighting reads GBuffers using a
      correctness-first fullscreen/stencil-bounded path rather than tiled or
      clustered deferred optimization.

### Migration-Critical Runtime

- [ ] **LIGHT-01**: Shared clustered forward light data exists as a
      service-owned product for forward-only consumers.
- [ ] **SHDW-01**: Conventional shadow maps integrate with Vortex deferred
      lighting.
- [ ] **ENV-01**: Environment lighting provides sky, fog, and IBL as
      Vortex-owned services.
- [ ] **POST-01**: Post processing resolves HDR scene output to the final
      tone-mapped screen image.
- [ ] **MIGR-01**: `Examples/Async` runs on Vortex with near-parity visible and
      operational behavior.
- [ ] **COMP-01**: Composition, resolve, extraction, and presentation work
      end-to-end for the migrated runtime path.
- [ ] **HARN-01**: `ForSinglePassHarness()` and `ForRenderGraphHarness()` work
      against the Vortex substrate by the first migration milestone.

### Extended Runtime Scenarios

- [ ] **DIAG-01**: Diagnostics overlay, ImGui panels, and GPU profiling are
      available on Vortex.
- [ ] **TRAN-01**: Translucent rendering uses forward lighting backed by shared
      Vortex forward light data.
- [ ] **OCCL-01**: Occlusion/HZB works with temporal handoff and reduces
      unnecessary rendering work.
- [ ] **VIEW-01**: Multi-view, multi-surface, PiP, and per-view shading mode
      scenarios work correctly in one frame.
- [ ] **OFFS-01**: `ForOffscreenScene()` supports deferred and forward offscreen
      rendering scenarios.
- [ ] **VARI-01**: Feature-gated runtime assemblies work, including depth-only,
      shadow-only, no-environment, no-shadowing, no-volumetrics, and
      diagnostics-only variants.

### Legacy Retirement

- [ ] **LEG-01**: Remaining examples and tests migrate to Vortex before the
      legacy renderer is deprecated and removed from the build.

## v2 Requirements

Deferred to post-release capability activation while remaining explicitly
traceable.

### Future Capabilities

- **FUT-01**: Geometry virtualization activates as a named future service.
- **FUT-02**: Material composition / deferred decal stages activate under an
  explicit owner.
- **FUT-03**: Indirect lighting, reflections, and SSAO activate under a future
  indirect-lighting service.
- **FUT-04**: Virtual shadow maps activate as a shadow-service strategy upgrade.
- **FUT-05**: Volumetric fog, clouds, and heterogeneous volumes activate in the
  reserved stage-14 family.
- **FUT-06**: Single-layer water activates as a dedicated water service.
- **FUT-07**: Distortion and light shaft bloom activate as reserved late-stage
  families.
- **FUT-08**: `GBufferE`, `GBufferF`, and `LightingChannelsTexture` activate as
  extended scene-texture resources.
- **FUT-09**: Advanced composition grows to include task-targeted global
  compositor behavior and public inter-view dependency DAGs.
- **FUT-10**: MegaLights-class many-light extensions activate under the
  lighting-service family.
- **FUT-11**: Hair-strand rendering activates against an explicit future slot.

## Out of Scope

| Feature | Reason |
|---------|--------|
| Mobile renderer path | PRD is desktop-only |
| Plugin-facing rendering extension ecosystem | Passes and pipelines remain engine-authored production code |
| Full-scene forward rendering as a co-equal desktop product | Vortex is deferred-first by design |
| Legacy renderer API compatibility | Vortex has no source-compatibility obligation |
| Durable renderer-to-renderer bridge layer | Hermetic separation is a hard requirement |

## Traceability

| Requirement | Phase | Status |
|-------------|-------|--------|
| FOUND-01 | Phase 0 | Complete |
| FOUND-02 | Phase 1 | Complete |
| FOUND-03 | Phase 1 | Complete |
| SCENE-01 | Phase 2 | Complete |
| SCENE-02 | Phase 2 | Complete |
| SCENE-03 | Phase 2 | Complete |
| SHDR-01 | Phase 3 | Pending |
| DEFR-01 | Phase 3 | Pending |
| DEFR-02 | Phase 3 | Pending |
| LIGHT-01 | Phase 4 | Pending |
| SHDW-01 | Phase 4 | Pending |
| ENV-01 | Phase 4 | Pending |
| POST-01 | Phase 4 | Pending |
| MIGR-01 | Phase 4 | Pending |
| COMP-01 | Phase 4 | Pending |
| HARN-01 | Phase 4 | Pending |
| DIAG-01 | Phase 5 | Pending |
| TRAN-01 | Phase 5 | Pending |
| OCCL-01 | Phase 5 | Pending |
| VIEW-01 | Phase 5 | Pending |
| OFFS-01 | Phase 5 | Pending |
| VARI-01 | Phase 5 | Pending |
| LEG-01 | Phase 6 | Pending |
| FUT-01 | Phase 7 | Pending |
| FUT-02 | Phase 7 | Pending |
| FUT-03 | Phase 7 | Pending |
| FUT-04 | Phase 7 | Pending |
| FUT-05 | Phase 7 | Pending |
| FUT-06 | Phase 7 | Pending |
| FUT-07 | Phase 7 | Pending |
| FUT-08 | Phase 7 | Pending |
| FUT-09 | Phase 7 | Pending |
| FUT-10 | Phase 7 | Pending |
| FUT-11 | Phase 7 | Pending |

**Coverage:**
- v1 requirements: 23 total
- Mapped to phases: 23
- Unmapped: 0 ✓
- v2 requirements: 11 tracked for Phase 7

**Progress note:** Phases `03-01` and `03-02` now cover the shared
shader-contract layer plus the first registered Vortex depth/base entrypoints
with `EngineShaderCatalog.h` and ShaderBake proof, but `SHDR-01` stays pending
until later Phase 03 plans add the remaining Vortex shader families required by
the deferred-core phase.

---
*Requirements defined: 2026-04-13*
*Last updated: 2026-04-15 after Phase 03 plan 03-02 completion review*
