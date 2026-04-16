# Vortex Renderer Plan

Status: `active execution plan`

This document is the execution plan for the Vortex renderer. It defines phases,
work items, design prerequisites, dependency ordering, verification gates, and
the feature activation matrix. It assumes the stable architecture in
[ARCHITECTURE.md](./ARCHITECTURE.md) and the evolving solution designs that
will be captured in [DESIGN.md](./DESIGN.md) and phase-specific LLD documents.

The PRD ([PRD.md](./PRD.md)) is stable. The architecture
([ARCHITECTURE.md](./ARCHITECTURE.md)) is stable. The top-level design
([DESIGN.md](./DESIGN.md)) is an **early draft** that covers illustrative
shapes only — it is not a complete low-level design. Each phase below
identifies which design work must be completed before implementation begins.

Related:

- [PRD.md](./PRD.md) — stable product requirements (source of truth for
  *what*)
- [ARCHITECTURE.md](./ARCHITECTURE.md) — stable conceptual architecture
  (source of truth for *structural how*)
- [DESIGN.md](./DESIGN.md) — top-level design draft (illustrative shapes,
  not complete LLD)
- [PROJECT-LAYOUT.md](./PROJECT-LAYOUT.md) — authoritative file placement
  reference
- [IMPLEMENTATION-STATUS.md](./IMPLEMENTATION-STATUS.md) — running
  resumability ledger (source of truth for *current state*)

## 1. Planning Principles

### 1.1 Design-First Execution

Every phase that introduces new systems or contracts requires a design
deliverable before implementation begins. The deliverable may be:

- an update to [DESIGN.md](./DESIGN.md) for scoped additions
- a standalone phase LLD document (e.g., `design/vortex/lld/phase-N-topic.md`)
  for systems that need their own detailed treatment

No implementation work may begin on a system whose low-level design has not
been written and reviewed. The plan explicitly calls out required design
deliverables per phase.

### 1.2 Delivery Strategy

Vortex is delivered as a clean-slate module that coexists with the legacy
`Oxygen.Renderer`. The legacy module stays intact and functional throughout.

Guiding principles:

- copy + adapt, not git-move
- preserve the production runtime path in the legacy module throughout
- build Vortex incrementally, one phase at a time
- each phase must compile and link cleanly before the next starts
- domain systems are vertical slices added after the core shell works
- avoid a "big bang" integration step

### 1.3 Parallelization Model

The plan has four execution bands:

1. **Foundation (Phase 0–1):** strictly sequential substrate migration
2. **Core renderer (Phase 2–3):** sequential scene-renderer bring-up
3. **Migration-critical services + first migration (Phase 4):** minimum
   service set to migrate `Examples/Async` — the PRD's first meaningful
   success gate (PRD Goal 11, §8.8)
4. **Remaining services and runtime scenarios (Phase 5):** additional
   subsystem services, multi-view/offscreen validation, and feature-gated
   variant verification; partially parallelizable

### 1.4 Shader Work Is Integrated

Shader work is not a separate track. Every phase that introduces GPU-visible
rendering must include its shader deliverables: HLSL contracts, entrypoints,
catalog registration, and ShaderBake integration. The plan calls these out
explicitly per phase.

### 1.5 LLD Readiness Criteria

The plan requires LLDs before implementation (§1.1). To make readiness
objective rather than subjective, every LLD must contain at minimum:

1. **Scope and context** — what system, why needed, what it replaces or adds.
2. **Interface contracts** — public API surface: types, methods, parameters,
   return values, ownership semantics.
3. **Data flow and dependencies** — inputs consumed (products, services),
   outputs produced (products, publications), cross-system interactions.
4. **Resource management** — GPU resources created/owned, lifecycle
   (per-frame, per-view, persistent), allocation strategy.
5. **Shader contracts** (if GPU work) — HLSL interfaces, bindless bindings,
   entrypoints, catalog registrations.
6. **Stage integration** — which frame stages the system participates in,
   dispatch contract, null-safe behavior.
7. **Testability approach** — how to validate the system in isolation and
   within the frame pipeline.
8. **Open questions** — any unresolved design choices that block or constrain
   implementation.

An LLD is *ready* when all applicable sections above are filled, open
questions are resolved or explicitly deferred with rationale, and the document
has been reviewed. Mechanical phases (0, 1) are exempt from LLD requirements.

## 2. Phase 0 — Scaffold and Build Integration

**Goal:** Vortex module exists in the build graph and compiles as an empty
target.

**Design prerequisite:** none (mechanical).

### Work Items

| ID | Task | Scope |
| -- | ---- | ----- |
| 0.1 | Wire `add_subdirectory("Vortex")` into `src/Oxygen/CMakeLists.txt` | Build system |
| 0.2 | Confirm `oxygen::vortex` target alias is emitted | Build system |
| 0.3 | Verify build succeeds via normal preset | Build system |

### Exit Gate

- `cmake --build` succeeds for `oxygen::vortex` with an empty source set
- target is discoverable via `cmake --build --target Oxygen.Vortex`

### Status: `in_progress` (scaffold on disk, not wired into build)

---

## 3. Phase 1 — Substrate Migration

**Goal:** All architecture-neutral substrate code lives in the Vortex module
and compiles. No new systems. No new shader code. Purely mechanical copy +
adapt.

**Design prerequisite:** none (mechanical adaptation per DESIGN.md §8).

This phase has internal ordering. Each step depends on the previous.

### Work Items

| ID | Task | Files / Scope | Legacy Source |
| -- | ---- | ------------- | ------------- |
| 1.1 | Cross-cutting types | `Types/` — 14 headers | `Renderer/Types/` |
| 1.2 | Upload subsystem | `Upload/` — 14 files | `Renderer/Upload/` |
| 1.3 | Resources subsystem | `Resources/` — 7 files | `Renderer/Resources/` |
| 1.4 | ScenePrep subsystem | `ScenePrep/` — 15 files | `Renderer/ScenePrep/` |
| 1.5 | Internal utilities | `Internal/` — 7 files | `Renderer/Internal/` |
| 1.6 | Pass base classes | `Passes/` — 3 files | `Renderer/Passes/` |
| 1.7 | View assembly + composition infrastructure | See §3.1 below | `Renderer/Pipeline/` + `Renderer/Internal/` |
| 1.8 | Renderer orchestrator | `Renderer.h/.cpp` + deps | `Renderer/Renderer.h/.cpp` |
| 1.9 | Smoke test | `Test/Link_test.cpp` | New |

### 3.1 Step 1.7 Detail: View Assembly and Composition

This is the most complex substrate step. It eliminates the `Pipeline/`
abstraction and redistributes files per ARCHITECTURE.md and PROJECT-LAYOUT.md.

Vocabulary types → Vortex root:

- `CompositionView.h`
- `RendererCapability.h`
- `RenderMode.h`

Scene policy → `SceneRenderer/`:

- `DepthPrePassPolicy.h`

Renderer Core internal → `Internal/`:

- `CompositionPlanner.h/.cpp`
- `CompositionViewImpl.h/.cpp`
- `ViewLifecycleService.h/.cpp`
- `FrameViewPacket.h/.cpp`

SceneRenderer internal → `SceneRenderer/Internal/`:

- `FramePlanBuilder.h/.cpp`
- `ViewRenderPlan.h`

**Not** carried over: `RenderingPipeline.h`, `PipelineFeature.h`,
`PipelineSettings.h/.cpp` — per ARCHITECTURE.md §5.1.1.

### 3.2 Step 1.8 Detail: Renderer Orchestrator

This is the only non-trivial foundation step. Adaptation work:

1. Strip domain members (~60 variables → ~10 kept)
2. Strip domain orchestration from frame-loop methods
3. Strip domain public API and console bindings
4. Clean up `RenderContext` pass-type registry
5. Update `FacadePresets` include paths
6. Add scene renderer dispatch hooks (initially no-ops)

### Exit Gate (Phase 1)

- Full build passes for `oxygen::vortex`
- Smoke test instantiates `vortex::Renderer` with empty capability set
- Frame-loop methods execute without crashing
- Existing legacy tests do not regress

### Mechanical Changes Applied Throughout

Per DESIGN.md §8.1:

| Change | From | To |
| ------ | ---- | --- |
| Namespace | `oxygen::engine` / `oxygen::renderer` | `oxygen::vortex` |
| Export macros | `OXGN_RNDR_*` | `OXGN_VRTX_*` |
| Include paths | `Oxygen/Renderer/...` | `Oxygen/Vortex/...` |

---

## 4. Phase 2 — SceneTextures and SceneRenderer Shell

**Goal:** The four-part SceneTextures contract is implemented,
`SceneRenderBuilder` wires a SceneRenderer delegate into the Renderer's frame
loop, and the 23-stage dispatch skeleton exists with all stages as no-ops.

### Design Prerequisites

Before implementation begins, the following must be designed and added to
DESIGN.md (or a phase LLD):

| Design Deliverable | Covers |
| ------------------ | ------ |
| SceneTextures four-part contract | `SceneTextures` concrete class, `SceneTextureSetupMode` enum/tracker, `SceneTextureBindings` bindless routing metadata, `SceneTextureExtracts` handoff structure |
| SceneRenderBuilder bootstrap | Construction, wiring into Renderer, capability-set interpretation |
| SceneRenderer shell dispatch | 23-stage skeleton, per-view vs per-frame iteration model, null-safe dispatch |

Reference: ARCHITECTURE.md §7.3 (four-part contract), §5.1.3
(`SceneRenderBuilder`), §6.2 (23-stage contract).

### Work Items

| ID | Task | Scope |
| -- | ---- | ----- |
| 2.1 | Implement `SceneTexturesConfig`, `SceneTextures` | Allocation, resize, accessors, GBuffer index vocabulary, and first active subset ownership for `SceneColor`, `SceneDepth`, `PartialDepth`, `GBufferA-D`, `Stencil`, `Velocity`, `CustomDepth` |
| 2.2 | Implement `SceneTextureSetupMode` | Setup-state enum/tracker per ARCHITECTURE.md §7.3.3 |
| 2.3 | Implement `SceneTextureBindings` | Bindless routing metadata generation per ARCHITECTURE.md §7.3.4 |
| 2.4 | Implement `SceneTextureExtracts` | Extraction structure per ARCHITECTURE.md §7.6 |
| 2.5 | Implement `ShadingMode.h` | Enum: kDeferred, kForward |
| 2.6 | Implement `SceneRenderBuilder` | Bootstrap helper that constructs SceneRenderer from CapabilitySet |
| 2.7 | Implement `SceneRenderer` shell | 23-stage dispatch skeleton, frame-phase hooks, all pointers null |
| 2.8 | Wire SceneRenderer into Renderer | Renderer delegates to SceneRenderer at appropriate frame phases |
| 2.9 | Implement `PostRenderCleanup` | File-separated method — extraction/handoff using Renderer Core helpers |
| 2.10 | Implement `ResolveSceneColor` | File-separated method — scene color resolve |
| 2.11 | Create `SceneRenderer/Stages/` directory structure | Subdirectories per PROJECT-LAYOUT.md |
| 2.12 | Validate first active `SceneTextures` subset | Explicit allocation and access coverage for `SceneColor`, `SceneDepth`, `PartialDepth`, `GBufferA-D`, `Stencil`, `Velocity`, `CustomDepth` |

### Exit Gate

- Build passes
- SceneRenderer is instantiated via SceneRenderBuilder and dispatched each frame
- 23-stage dispatch skeleton runs with null-safe no-ops
- SceneTextures allocates products based on config
- First active `SceneTextures` subset is concretely allocated and queryable: `SceneColor`, `SceneDepth`, `PartialDepth`, `GBufferA-D`, `Stencil`, `Velocity`, `CustomDepth`
- SceneTextureSetupMode tracks which products are set up
- No visible rendering output yet

---

## 5. Phase 3 — Deferred Core (Depth → GBuffer → Lighting)

**Goal:** A minimal lit deferred scene: depth prepass writes SceneDepth, base
pass writes GBufferA–D + SceneColor emissive, the current desktop deferred
opaque-velocity policy writes truthful Stage-9 motion vectors, deferred
lighting produces lit output, and scene color resolves. This is the first
visual proof of the Vortex deferred architecture.

### Design Prerequisites

These are the most complex design deliverables in the entire plan. Each
requires its own LLD section or document.

| Design Deliverable | Covers | Suggested Location |
| ------------------ | ------ | ------------------ |
| Depth prepass LLD | DepthPrepassModule, depth-only pass, mesh processor, depth-policy integration, and the non-active depth-pass velocity option boundary | `design/vortex/lld/depth-prepass.md` or DESIGN.md update |
| Base pass LLD | BasePassModule, GBuffer MRT output, masked/opaque permutations, material shader contract (GBufferOutput), renderer-owned rigid history consumption, Stage-9 opaque velocity production, forward-mode branch | `design/vortex/lld/base-pass.md` or DESIGN.md update |
| Deferred lighting LLD | Directional fullscreen deferred lighting, one-pass bounded-volume point/spot lighting, SceneColor accumulation | `design/vortex/lld/deferred-lighting.md` or DESIGN.md update |
| Shader contracts LLD | Vortex shader directory setup, `Contracts/` and `Shared/` initial files, GBuffer encode/decode, position reconstruction, BRDF core, EngineShaderCatalog registration | `design/vortex/lld/shader-contracts.md` |
| InitViews LLD | InitViewsModule, visibility/culling orchestration, ScenePrep integration, per-view prepared-scene publication | `design/vortex/lld/init-views.md` or DESIGN.md update |
| ScenePrep refactor LLD | Authoritative ScenePrep runtime contract, traversal budget, prepared-scene publication, multi-view-safe state ownership | `design/vortex/lld/sceneprep-refactor.md` |

### Work Items

#### 3A: Shader Foundation

| ID | Task | Scope |
| -- | ---- | ----- |
| 3A.1 | Create `Shaders/Vortex/Contracts/Definitions/` | SceneDefinitions.hlsli, LightDefinitions.hlsli |
| 3A.2 | Create `Shaders/Vortex/Contracts/` | SceneTextures.hlsli, GBufferLayout.hlsli, GBufferHelpers.hlsli, ViewFrameBindings.hlsli, SceneTextureBindings.hlsli |
| 3A.3 | Create `Shaders/Vortex/Shared/` | FullscreenTriangle.hlsli, PositionReconstruction.hlsli, PackUnpack.hlsli, BRDFCommon.hlsli, DeferredShadingCommon.hlsli |
| 3A.4 | Create `Shaders/Vortex/Materials/` | GBufferMaterialOutput.hlsli, MaterialTemplateAdapter.hlsli |
| 3A.5 | Register initial Vortex entrypoints in `EngineShaderCatalog.h` | Depth prepass, base pass, deferred light entrypoints |
| 3A.6 | Validate ShaderBake compiles all registered entrypoints | Build verification |

#### 3B: InitViews Module

| ID | Task | Scope |
| -- | ---- | ----- |
| 3B.1 | Implement `InitViewsModule` | `SceneRenderer/Stages/InitViews/` — visibility, ScenePrep dispatch, per-view prepared-scene publication, culling orchestration |
| 3B.2 | Wire into SceneRenderer stage 2 dispatch | |

#### 3C: Depth Prepass

| ID | Task | Scope |
| -- | ---- | ----- |
| 3C.1 | Implement `DepthPrepassModule` | `SceneRenderer/Stages/DepthPrepass/` — depth-only pass, mesh processor |
| 3C.2 | Implement depth prepass shader entrypoints | `Shaders/Vortex/Stages/DepthPrepass/` |
| 3C.3 | Write `SceneDepth` and `PartialDepth` to SceneTextures | |
| 3C.4 | Keep stage 3 depth-only under the active opaque-velocity policy | Stage-3 opaque velocity remains a future policy option, not the current Phase-3 desktop deferred contract |
| 3C.5 | Update `SceneTextureSetupMode` after depth prepass | |
| 3C.6 | Wire into SceneRenderer stage 3 dispatch | |

#### 3D: GBuffer Base Pass

| ID | Task | Scope |
| -- | ---- | ----- |
| 3D.1 | Implement `BasePassModule` | `SceneRenderer/Stages/BasePass/` — GBuffer MRT pass, base-pass mesh processor |
| 3D.2 | Implement base pass shader entrypoints | `Shaders/Vortex/Stages/BasePass/` |
| 3D.3 | Write GBufferA–D + SceneColor emissive | MRT output per DESIGN.md §6.1 |
| 3D.4 | Produce `SceneVelocity` in stage 9 for masked, deformed, skinned, and WPO-capable opaque geometry using current + previous transform/deformation data and previous view data | UE5.7-grade opaque velocity parity target |
| 3D.5 | Select masked vs opaque base-pass PSOs and drive `ALPHA_TEST` permutations | Required for truthful masked deferred behavior and masked velocity |
| 3D.6 | Implement the stage-9 motion-vector-world-offset auxiliary path and merge/update step before velocity publication | Required for UE5.7-grade parity when materials declare motion-vector world offset |
| 3D.7 | Implement `SceneTextures::RebuildWithGBuffers()` | Stage 10 state transition |
| 3D.8 | Update `SceneTextureSetupMode` and velocity publication after base pass from output-backed proof only | |
| 3D.9 | Wire into SceneRenderer stage 9 dispatch | |

#### 3E: Deferred Lighting

| ID | Task | Scope |
| -- | ---- | ----- |
| 3E.1 | Implement deferred lighting passes | Directional fullscreen, point bounded sphere volume, spot bounded cone volume |
| 3E.2 | Implement deferred lighting shader entrypoints | `Shaders/Vortex/Services/Lighting/` |
| 3E.3 | Accumulate into SceneColor | |
| 3E.4 | Wire lighting into SceneRenderer stage 12 dispatch | Initially inlined, moves to LightingService in Phase 4 |

#### 3F: GBuffer Debug Visualization

| ID | Task | Scope |
| -- | ---- | ----- |
| 3F.1 | Implement GBuffer debug views | Normal, base color, roughness, metallic, depth debug visualization |
| 3F.2 | Register debug shader variants in catalog | |
| 3F.3 | Route deferred debug visualization through `SceneRenderer` | Public runtime mode selection, Stage-10-fed fullscreen debug pass, and capture-backed `VortexBasic` proof |

### Exit Gate

- Lit deferred scene visible: at least 1 directional + 1 point light
- GBuffer debug views show correct normals, base color, roughness, metallic, and depth through the live deferred debug-view route
- SceneColor contains correct diffuse + specular response
- Velocity buffer has valid data for masked, deformed, skinned, and
  WPO-capable opaque geometry, including camera motion and object/deformation
  motion, and no doc/test surface overclaims unsupported producers
- Masked deferred materials alpha-clip truthfully in stage 9 and only
  surviving pixels contribute to GBuffers, SceneColor, and velocity
- SceneTextureSetupMode correctly tracks product availability through stages
- ShaderBake compiles all Vortex entrypoints without error
- Non-runtime facade `ForSinglePassHarness()` works against Vortex substrate
- Full `Oxygen.Vortex.*` suite passes
- `oxytidy` on the changed Phase 3 file scope reports no introduced warnings
- Automated source/test/log closeout proves expected stage ordering, GBuffer
  contents, SceneColor accumulation, and bounded-volume local-light behavior
- `tools/vortex/Run-VortexBasicDebugViewValidation.ps1` passes and proves the
  deferred debug-view path for base color, world normals, roughness,
  metalness, scene-depth-raw, and scene-depth-linear
- Live `VortexBasic` runtime validation is part of the Phase 3 gate:
  build the required Vortex targets, capture a fresh RenderDoc frame from
  an expanded `VortexBasic` validation scene that exercises rigid opaque,
  masked, skinned/deforming, and WPO-driven opaque velocity producers, and
  pass the one-command runtime validator
  `tools/vortex/Run-VortexBasicRuntimeValidation.ps1`

---

## 6. Phase 4 — Migration-Critical Services and First Migration

**Goal:** Activate the minimum subsystem service set required to migrate
`Examples/Async`, then perform the migration. This phase delivers the PRD's
first meaningful success gate: migration-capable runtime use (PRD Goal 11,
§8.8, §8.9).

The services in this phase are selected because `Examples/Async` cannot
produce correct, visually representative output without them. Services that
are valuable but not required for the first migration (Diagnostics,
Translucency, Occlusion) are deferred to Phase 5.

### Design Prerequisites

Each service requires its own LLD before implementation (per §1.5 readiness
criteria):

| Service | Design Deliverable | Covers |
| ------- | ----------------- | ------ |
| LightingService | `design/vortex/lld/lighting-service.md` | Light grid build, published forward-light package, deferred-lighting ownership transfer from Phase 3, per-view publication, deferred-path invariants |
| PostProcessService | `design/vortex/lld/post-process-service.md` | Tonemap pass (minimum for visible output), auto-exposure, bloom, AA/TSR slot |
| ShadowService | `design/vortex/lld/shadow-service.md` | Conventional shadow map rendering, shadow data product, shadow-to-lighting wire, VSM slot reservation |
| EnvironmentLightingService | `design/vortex/lld/environment-service.md` | Sky/atmosphere/fog composition, environment-probe / IBL publication, stage 14 reservation |
| Migration playbook | `design/vortex/lld/migration-playbook.md` | Async example analysis, integration seam mapping, behavior parity checklist, visual baseline capture, composition-to-screen validation |

### Dependency Constraints Within Phase 4

```text
Phase 3 (deferred core)
 ├─► 4A: LightingService (absorbs Phase 3 deferred lighting)
 ├─► 4B: PostProcessService (independent)
 ├─► 4C: ShadowService (independent)
 └─► 4D: EnvironmentLightingService (independent)
      └─► all of 4A–4D complete ─► 4E: Examples/Async migration
                                  └─► 4F: Composition/presentation validation
```

4A–4D are parallelizable. 4E requires all four services. 4F validates that
the migrated example presents correctly through Vortex's composition path.

### 4A: LightingService + Forward Light Grid

| ID | Task | Scope |
| -- | ---- | ----- |
| 4A.1 | Implement `Lighting/LightingService.h/.cpp` | Service lifecycle, capability gating |
| 4A.2 | Implement light grid build (stage 6) | Clustered light-grid and published forward-light package as shared supporting data |
| 4A.3 | Transfer deferred lighting from Phase 3 inline → LightingService | Stage 12 now dispatched via service |
| 4A.3a | Replace the temporary Phase 03 procedural point/spot proxy generation with persistent `LightingService`-owned sphere/cone geometry | Remove the retained `SV_VertexID` local-light geometry shortcut from the canonical Stage 12 runtime path |
| 4A.4 | Implement forward light data publication | Per-view typed publication through `ViewFrameBindings` / `LightingFrameBindings` |
| 4A.5 | Implement lighting shader families | `Shaders/Vortex/Services/Lighting/` — light grid, deferred light, forward common |

**Exit gate:** Light grid produces valid clustered data. Deferred lighting is
dispatched through the service without redefining the canonical per-light
stage-12 path. The temporary Phase 03 procedural point/spot proxy generation
is removed and replaced by persistent `LightingService`-owned sphere/cone
geometry. Forward-light bindings are published per view.

### 4B: PostProcessService

| ID | Task | Scope |
| -- | ---- | ----- |
| 4B.1 | Implement `PostProcess/PostProcessService.h/.cpp` | Service lifecycle |
| 4B.2 | Implement tonemap pass | HDR → LDR (minimum for visible screen output) |
| 4B.3 | Implement auto-exposure | Luminance histogram or average |
| 4B.4 | Implement bloom | If straightforward to carry from legacy |
| 4B.5 | Implement post-process shader families | `Shaders/Vortex/Services/PostProcess/` |

**Exit gate:** Tonemapped output reaches the SceneRenderer-supplied post target.
Visual: HDR → LDR with correct exposure.

### 4C: ShadowService

| ID | Task | Scope |
| -- | ---- | ----- |
| 4C.1 | Implement `Shadows/ShadowService.h/.cpp` | Service lifecycle, shadow data product |
| 4C.2 | Implement shadow depth rendering (stage 8) | Conventional shadow maps (directional first) |
| 4C.3 | Wire shadow data into deferred lighting | Shadow terms in lighting stage 12 |
| 4C.4 | Implement shadow shader families | `Shaders/Vortex/Services/Shadows/` |
| 4C.5 | Reserve VSM slot | `Shadows/Vsm/` structure, no implementation |

**Exit gate:** Shadow maps rendered. Deferred lighting applies shadow terms.
Visual: correct directional shadow on ground plane. RenderDoc capture validates
shadow pass ordering.

### 4D: EnvironmentLightingService

| ID | Task | Scope |
| -- | ---- | ----- |
| 4D.1 | Implement `Environment/EnvironmentLightingService.h/.cpp` | Service lifecycle, environment-probe / IBL publication |
| 4D.2 | Implement sky/atmosphere rendering (stage 15) | Sky pass, atmosphere composition |
| 4D.3 | Implement fog | Height fog, distance fog |
| 4D.4 | Implement environment-probe / IBL publication | Per-view typed publication from environment-owned persistent state |
| 4D.5 | Reserve stage 14 volumetrics | No-op slot within the Environment family; future internal stage family, not one monolithic pass |
| 4D.6 | Implement environment shader families | `Shaders/Vortex/Services/Environment/` |

**Exit gate:** Sky renders. Fog applies. Environment-owned ambient probe data is
published and available to the appropriate consumers. If Phase 4 uses a
temporary ambient bridge before stage 13 exists, that bridge is explicitly
documented and does not violate stage order.

### 4E: First Migration — `Examples/Async`

This is the PRD's required first migration target (PRD §6.1.1). It is not a
toy validation surface; it is the first proof that Vortex can support a real
Oxygen runtime flow.

| ID | Task | Scope |
| -- | ---- | ----- |
| 4E.1 | Capture legacy `Examples/Async` visual baseline | RenderDoc frame 10 baseline |
| 4E.2 | Port `Examples/Async` from legacy Renderer to Vortex | Engine/renderer seam integration through the real SceneRenderer/composition path, no compatibility clutter |
| 4E.3 | Visual parity validation | RenderDoc capture comparison against legacy baseline |
| 4E.4 | Behavior parity validation | Workflows, async operations, observable behavior match |
| 4E.5 | Validate `ForSinglePassHarness()` against Vortex | Non-runtime facade check |
| 4E.6 | Validate `ForRenderGraphHarness()` against Vortex | Non-runtime facade check |

**Exit gate:** `Examples/Async` runs on Vortex with correct visual output
matching legacy reference through the real composition submission path and
presentation surface. Migration uses no long-lived compatibility clutter. Two
non-runtime facades verified.

### 4F: Composition and Presentation Validation

Deepens proof for the resolve / extraction / handoff artifacts that sit around
the already-live runtime composition path used by the first migration target.

| ID | Task | Scope |
| -- | ---- | ----- |
| 4F.1 | Validate single-view composition and presentation artifacts | `CompositionView` → SceneRenderer → `CompositionSubmission` → presentation remains correct under inspection |
| 4F.2 | Validate `ResolveSceneColor` artifact behavior end-to-end | Stage 21 resolve runs only when needed and produces the correct post-process input |
| 4F.3 | Validate `PostRenderCleanup` extraction/handoff artifacts | Stage 23 extraction handoff through Renderer Core helpers produces the correct outputs |

**Exit gate:** `Examples/Async` presents correct output through Vortex
composition path. No composition artifacts, no missing handoffs, no dropped
frames.

### Exit Gate (Phase 4 — Overall)

- `Examples/Async` runs on Vortex with correct visual output (PRD §6.1.1)
- All four migration-critical services active: Lighting, PostProcess, Shadows,
  Environment
- Two non-runtime facades verified against Vortex substrate
- Composition path validated end-to-end
- RenderDoc frame 10 capture shows correct active stage-family ordering,
  artifact boundaries, and visual output
- No long-lived compatibility clutter in the migrated example

---

## 7. Phase 5 — Remaining Services and Runtime Scenarios

**Goal:** Activate remaining subsystem services, validate multi-view and
offscreen scenarios required by the PRD, and verify feature-gated runtime
variants.

### Design Prerequisites

| Deliverable | Required For | Covers |
| ----------- | ----------- | ------ |
| `design/vortex/lld/diagnostics-service.md` | 5A | GPU debug overlay, ImGui panel infra, profiler port, debug-mode vocabulary |
| `design/vortex/lld/translucency.md` | 5B | Forward-lit translucency passes, LightingService forward data consumption |
| `design/vortex/lld/occlusion.md` | 5C | HZB generation, occlusion queries, temporal handoff |
| `design/vortex/lld/multi-view-composition.md` | 5D | Multi-view dispatch, per-view ShadingMode selection, multi-surface output, PiP, editor viewport model |
| `design/vortex/lld/offscreen-rendering.md` | 5E | ForOffscreenScene facade, deferred/forward mode selection per scenario, thumbnail/preview use cases |
| `design/vortex/lld/shadow-local-lights.md` | 5G | ShadowService expansion for spot-light and point-light conventional shadows after the directional-first Phase 4C baseline |

### Dependency Constraints

```text
Phase 4 (migration complete)
 ├─► 5A: DiagnosticsService (independent)
 ├─► 5B: TranslucencyModule (needs LightingService from 4A)
 ├─► 5C: OcclusionModule (needs SceneDepth from Phase 3)
 ├─► 5D: Multi-view and per-view mode validation (needs composition from 4F)
 ├─► 5E: Offscreen and facade validation (independent)
 ├─► 5F: Feature-gated runtime variants (needs all services)
 └─► 5G: Local-light conventional shadow expansion (needs 4C baseline)
```

5A–5E and 5G are parallelizable. 5F requires all services to be active.

### 5A: DiagnosticsService

| ID | Task | Scope |
| -- | ---- | ----- |
| 5A.1 | Implement `Diagnostics/DiagnosticsService.h/.cpp` | Service lifecycle, debug-mode vocabulary |
| 5A.2 | Implement GPU debug overlay | Debug visualization passes |
| 5A.3 | Implement ImGui panel infrastructure | Panel registration, rendering |
| 5A.4 | Port GPU timeline profiler | Coarse telemetry, Tracy correlation |
| 5A.5 | Implement diagnostics shader families | `Shaders/Vortex/Services/Diagnostics/` |

**Exit gate:** Diagnostics overlay renders. ImGui panels functional. GPU
profiling scopes visible.

### 5B: TranslucencyModule

| ID | Task | Scope |
| -- | ---- | ----- |
| 5B.1 | Implement `TranslucencyModule` | `SceneRenderer/Stages/Translucency/` |
| 5B.2 | Consume `LightingService` forward light data | Forward-lit translucency |
| 5B.3 | Wire into stage 18 dispatch | |
| 5B.4 | Implement translucency shader families | `Shaders/Vortex/Stages/Translucency/` |

**Exit gate:** Translucent objects render with forward lighting. Visual:
correct lighting + blending over deferred opaque background.

### 5C: OcclusionModule

| ID | Task | Scope |
| -- | ---- | ----- |
| 5C.1 | Implement `OcclusionModule` | `SceneRenderer/Stages/Occlusion/` — HZB generation, occlusion queries |
| 5C.2 | Implement temporal HZB handoff | Per-view history state |
| 5C.3 | Wire into stage 5 dispatch | |
| 5C.4 | Implement occlusion shader families | `Shaders/Vortex/Stages/Occlusion/` |

**Exit gate:** Occlusion culling reduces draw calls in complex scenes.

### 5D: Multi-View, Multi-Surface, and Per-View Mode Validation

Validates the PRD's editor and multi-view scenarios (PRD §6.5, §6.6).

| ID | Task | Scope |
| -- | ---- | ----- |
| 5D.1 | Multi-view dispatch validation | Two+ `CompositionView` descriptors rendered in one frame, each producing correct output |
| 5D.2 | Per-view `ShadingMode` selection | One view deferred, another forward; both produce correct output in the same frame |
| 5D.3 | Multi-surface composition output | Different `CompositionView` targets composited to different output surfaces |
| 5D.4 | PiP / secondary composition | Picture-in-picture or minimap-like secondary view composited over primary |
| 5D.5 | Capability-gated per-view service presence | Different service sets active for different views via `CapabilitySet` |

**Exit gate:** Multi-view frame renders correctly with heterogeneous views.
Per-view ShadingMode selection produces correct results. PiP composites
correctly.

### 5E: Offscreen Rendering and Facade Validation

Validates the PRD's offscreen scenarios (PRD §6.3, §6.4).

| ID | Task | Scope |
| -- | ---- | ----- |
| 5E.1 | Validate `ForOffscreenScene()` against Vortex | Offscreen facade produces correct rendered output to texture |
| 5E.2 | Validate offscreen deferred mode | `ForOffscreenScene()` with deferred SceneRenderer produces correct GBuffer + lit output |
| 5E.3 | Validate offscreen forward mode | `ForOffscreenScene()` with forward ShadingMode produces correct output |
| 5E.4 | Validate thumbnail / material preview scenario | Lightweight offscreen render without full service set |

**Exit gate:** All three non-runtime facades work against Vortex substrate.
Offscreen rendering produces correct output in both deferred and forward modes.

### 5F: Feature-Gated Runtime Variant Validation

Validates the PRD's feature-gated variant scenarios (PRD §6.6).

| ID | Task | Scope |
| -- | ---- | ----- |
| 5F.1 | Validate depth-only rendering | `CapabilitySet` with no lighting/shadows/environment produces valid depth |
| 5F.2 | Validate shadow-only rendering | `CapabilitySet` with shadows but no environment/post-process |
| 5F.3 | Validate no-environment variant | Full renderer minus environment lighting service |
| 5F.4 | Validate diagnostics-only overlay | `CapabilitySet` with diagnostics only, no domain rendering |
| 5F.5 | Validate no-shadowing variant | Lighting, environment, and post-process remain active while `ShadowService` is disabled |
| 5F.6 | Validate no-volumetrics variant | Environment lighting remains active while the reserved stage-14 volumetric family is explicitly inactive |

**Exit gate:** Each PRD §6.6 variant compiles, runs, and produces expected
output, including depth-only, shadow-only, no-environment, no-shadowing,
no-volumetrics, and diagnostics-only assemblies. Capability gating correctly
enables/disables subsystems without crashes.

### 5G: ShadowService Local-Light Conventional Expansion

Completes the conventional-shadow baseline that Phase 4C intentionally scoped
to directional-first delivery.

| ID | Task | Scope |
| -- | ---- | ----- |
| 5G.1 | Expand `ShadowService` conventional local-light support | Spot-light conventional shadows as the first required local-light path |
| 5G.2 | Implement point-light conventional shadow storage strategy | Preferred baseline: one-pass cubemap depth targets unless later evidence justifies another approved conventional path |
| 5G.3 | Publish per-view local-light shadow bindings | Extend `ShadowFrameBindings` without freezing a VSM ABI |
| 5G.4 | Validate local-light shadow consumption | Deferred lighting and translucency consume the published bindings correctly |

**Exit gate:** Spot-light conventional shadows are functional. Point-light
conventional shadows have an explicit implemented strategy or a documented
product-level deferral decision backed by the Phase 5G design. Shadow
publication remains per-view and future-safe for VSM.

### Exit Gate (Phase 5 — Overall)

- All subsystem services active (Lighting, PostProcess, Shadows, Environment,
  Diagnostics)
- TranslucencyModule and OcclusionModule functional
- Local-light conventional shadows validated or explicitly deferred by the
  Phase 5G design decision
- Multi-view, PiP, and per-view ShadingMode validated
- All three non-runtime facades verified against Vortex
- Offscreen deferred and forward modes validated
- Feature-gated runtime variants validated
- All Phase 5 services validated against `Examples/Async`

---

## 8. Phase 6 — Legacy Deprecation

**Goal:** Port all remaining examples and tests, then deprecate and remove the
legacy renderer.

### Work Items

| ID | Task | Scope |
| -- | ---- | ----- |
| 6.1 | Port remaining examples to Vortex | Incremental — after Async proves viability in Phase 4 |
| 6.2 | Port remaining tests to Vortex | Test suite migration |
| 6.3 | Mark `Oxygen.Renderer` deprecated | |
| 6.4 | Remove `Oxygen.Renderer` from build | Only after all examples/tests pass |

### Exit Gate

- All examples and tests run on Vortex
- `Oxygen.Renderer` removed from build
- No `Oxygen.Renderer` includes anywhere in the codebase

---

## 9. Phase 7 — Future Capabilities (Post-Release)

**Goal:** Activate reserved stage slots and advanced subsystem capabilities
beyond the initial Vortex release. This phase exists to satisfy PRD Goal 15's
requirement that every major feature family maps to a specific activation
phase, even if that phase is future.

These items are intentionally deferred beyond the initial release. Each
requires its own LLD and concrete scoping before implementation begins.

### 7A: Advanced Geometry

| Family | Stage | Scope |
| ------ | ----- | ----- |
| GeometryVirtualizationService | Stage 4 | Nanite-equivalent mesh virtualization |
| MaterialCompositionService | Stages 7, 11 | DBuffer decals, deferred decals, material classification |

### 7B: Advanced Lighting and GI

| Family | Stage | Scope |
| ------ | ----- | ----- |
| IndirectLightingService | Stage 13 | Canonical indirect environment evaluation, SSAO, reflections, GI (Lumen-equivalent later) |
| SSAO / ScreenSpaceAO | — | IndirectLightingService ambient occlusion product |
| MegaLights-class lighting extensions | Stage 12 | LightingService direct-lighting extension family for advanced many-light / area-light style evaluation |
| Tiled/clustered deferred | Stage 12 | Optimization path for deferred lighting; profiling-justified |

Design prerequisite for the first activation of stage 13:
- `design/vortex/lld/indirect-lighting-service.md` — future `IndirectLightingService`
  contract, including retirement of the temporary Phase 4 environment-ambient
  bridge

### 7C: Advanced Shadows

| Family | Stage | Scope |
| ------ | ----- | ----- |
| Virtual shadow maps (VSM) | Stage 8 | ShadowService internal strategy upgrade |

### 7D: Advanced Effects

| Family | Stage | Scope |
| ------ | ----- | ----- |
| Volumetric fog / clouds | Stage 14 | EnvironmentLightingService internal activation |
| Heterogeneous volumes | Stage 14 | EnvironmentLightingService volumetric rendering family aligned with the reserved stage-14 slot |
| Single layer water (WaterService) | Stage 16 | Standalone service |
| Hair strands | Stage 17 | Specialized strand-rendering family initially planned against the reserved post-opaque extension slot until a dedicated owner is justified |
| DistortionModule | Stage 19 | Distortion / translucent velocities |
| LightShaftBloomModule | Stage 20 | Light shaft bloom, translucency upscale |

### 7E: Extended Scene Texture Resources

| Family | Scope |
| ------ | ----- |
| GBufferE / GBufferF | Reserved SceneTextures slots for advanced material data |
| LightingChannelsTexture | Per-object lighting channel mask |

### 7F: Advanced Composition

| Family | Scope |
| ------ | ----- |
| Task-targeted global compositor | Global composition orchestration |
| Public inter-view dependency DAGs | Cross-view dependency declaration |

---

## 10. Dependency Map

### 10.1 Overall Flow

```text
Phase 0 (scaffold)
 └─► Phase 1 (substrate — sequential steps 1.1 → 1.9)
      └─► Phase 2 (SceneTextures + SceneRenderer shell)
           └─► Phase 3 (deferred core: shaders → InitViews → depth → base → lighting)
                └─► Phase 4 (migration-critical services → Examples/Async migration)
                     └─► Phase 5 (remaining services + runtime scenarios)
                          └─► Phase 6 (legacy deprecation)
                               └─► Phase 7 (future capabilities — post-release)
```

### 10.2 Phase 3 Internal Order

```text
3A (shader foundation)
 └─► 3B (InitViews) ──────────────────────┐
 └─► 3C (depth prepass) ─► 3D (base pass) ─► 3E (deferred lighting)
                                           └─► 3F (GBuffer debug viz)
```

3A is prerequisite for all others. 3B can proceed in parallel with 3C once
3A is done. 3C → 3D → 3E is strictly sequential. 3F can follow 3D.

### 10.3 Phase 4 Internal Order

```text
Phase 3 complete
 ├─► 4A (lighting service)    ──┐
 ├─► 4B (post-process service)  ├─► 4E (Examples/Async migration)
 ├─► 4C (shadow service)        │    └─► 4F (composition validation)
 └─► 4D (environment service) ──┘
```

4A–4D are parallelizable. 4E requires all four. 4F follows 4E.

### 10.4 Phase 5 Parallelism

```text
Phase 4 complete
 ├─► 5A (diagnostics service)
 ├─► 5B (translucency module)
 ├─► 5C (occlusion module)
 ├─► 5D (multi-view validation)
 ├─► 5E (offscreen / facade validation)
 ├─► 5F (feature-gated variants — needs all services)
 └─► 5G (local-light conventional shadow expansion)
```

5A–5E and 5G are parallelizable. 5F requires all services to be active.

## 11. Milestones

### M1. Empty Shell (Phase 0 + Phase 1)

Exit criteria:

- Vortex module compiles and links
- Renderer instantiates with empty capability set
- Smoke test passes
- Legacy tests unaffected

### M2. SceneRenderer Shell (Phase 2)

Exit criteria:

- SceneTextures four-part contract implemented
- SceneRenderBuilder bootstraps SceneRenderer as Renderer delegate
- 23-stage dispatch skeleton runs

### M3. Deferred Visual Proof (Phase 3)

Exit criteria:

- Lit deferred scene with at least 1 directional + 1 point light
- GBuffer debug visualization functional
- Velocity buffer valid for masked, deformed, skinned, and WPO-capable opaque geometry
- ShaderBake compiles all Vortex shaders
- RenderDoc capture validates pass ordering

### M4. Migration-Capable Runtime (Phase 4) — PRD First Success Gate

This is the PRD's required first meaningful success gate (PRD Goal 11, §8.8).

Exit criteria:

- `Examples/Async` runs on Vortex with visual parity to legacy reference
- Four migration-critical services active (Lighting, PostProcess, Shadows,
  Environment)
- Composition path validated end-to-end
- Two non-runtime facades verified (ForSinglePassHarness, ForRenderGraphHarness)
- Migration uses no long-lived compatibility clutter
- RenderDoc A/B capture validates visual parity at frame 10

### M5. Full Feature Set (Phase 5)

Exit criteria:

- All subsystem services active (adds Diagnostics to Phase 4 set)
- TranslucencyModule and OcclusionModule functional
- Local-light conventional shadows validated or explicitly deferred by the
  Phase 5G design decision
- Multi-view, PiP, per-view ShadingMode validated (PRD §6.5)
- All three non-runtime facades verified (adds ForOffscreenScene)
- Offscreen deferred and forward modes validated (PRD §6.4)
- Feature-gated runtime variants validated (PRD §6.6)

### M6. Production Ready (Phase 6)

Exit criteria:

- All examples and tests ported to Vortex
- `Oxygen.Renderer` removed from build

## 12. Feature Activation Matrix

This table satisfies PRD Goal 15 (phase-traceable feature activation). Every
major feature family is mapped to its activation phase, current status, and
final target — including future-phase items per PRD §8.13.

| Feature Family | Activation Phase | Current Status | Final Target | Notes |
| -------------- | --------------- | -------------- | ------------ | ----- |
| **SceneTextures (core)** | Phase 2 | not started | SceneColor, SceneDepth, PartialDepth, GBufferA–D, Stencil, Velocity, CustomDepth | Phase-1 active subset per PRD §7.17 |
| **Stencil** | Phase 2 | not started | Active scene stencil support within the first `SceneTextures` subset | Explicitly allocated and validated in 2.12 |
| **CustomDepth** | Phase 2 | not started | Separate custom-depth path within the first `SceneTextures` subset | Explicitly allocated and validated in 2.12 |
| **SceneTextureSetupMode** | Phase 2 | not started | Setup-state tracking per ARCH §7.3.3 | |
| **SceneTextureBindings** | Phase 2 | not started | Bindless routing metadata | |
| **SceneTextureExtracts** | Phase 2 | not started | Extraction/handoff contract | |
| **SceneRenderBuilder** | Phase 2 | not started | Bootstrap helper per ARCH §5.1.3 | |
| **SceneRenderer (shell)** | Phase 2 | not started | 23-stage dispatch skeleton | |
| **InitViewsModule** | Phase 3 | not started | Visibility, culling, command gen | Stage 2 |
| **DepthPrepassModule** | Phase 3 | not started | Depth-only under the active desktop deferred opaque-velocity policy | Stage 3 |
| **BasePassModule** | Phase 3 | not started | GBuffer MRT + masked alpha-clip + active opaque velocity production | Stage 9 |
| **Deferred lighting** | Phase 3 → 4A | not started | Directional fullscreen + bounded-volume point/spot deferred lighting; transfers to LightingService in 4A | Stage 12 |
| **GBuffer debug viz** | Phase 3 | done | Runtime-facing deferred debug views for base color, world normals, roughness, metalness, and scene depth | `tools/vortex/Run-VortexBasicDebugViewValidation.ps1` |
| **Shader contracts** | Phase 3 | not started | Contracts/, Shared/, Materials/ per ARCH §10 | |
| **LightingService** | Phase 4A | not started | Light grid + deferred lighting + forward data | Stages 6, 12 |
| **PostProcessService** | Phase 4B | not started | Tonemap, exposure, bloom | Stage 22 |
| **ShadowService** | Phase 4C | not started | Conventional shadows; VSM reserved for Phase 7C | Stage 8 |
| **EnvironmentLightingService** | Phase 4D | not started | Sky, fog, environment-probe / IBL publication; volumetrics reserved for Phase 7D | Stages 14 (reserved), 15 |
| **Examples/Async migration** | Phase 4E | not started | Full Vortex runtime | PRD §6.1.1 — first success gate |
| **Composition/presentation** | Phase 4F | not started | Single-view composition to screen, resolve, handoff | Validated end-to-end during first migration |
| **ResolveSceneColor** | Phase 2 (stub) → Phase 4F (real) | not started | Scene color resolve | Stage 21 |
| **PostRenderCleanup** | Phase 2 (stub) → Phase 4F (real) | not started | Extraction/handoff | Stage 23 |
| **DiagnosticsService** | Phase 5A | not started | GPU debug, ImGui, profiler | |
| **TranslucencyModule** | Phase 5B | not started | Forward-lit translucency | Stage 18 |
| **OcclusionModule** | Phase 5C | not started | HZB, occlusion queries | Stage 5 |
| **Multi-view / multi-surface** | Phase 5D | not started | Multi-view dispatch, PiP, per-view ShadingMode | PRD §6.5 |
| **Offscreen rendering** | Phase 5E | not started | ForOffscreenScene, deferred/forward mode, previews | PRD §6.4 |
| **Feature-gated variants** | Phase 5F | not started | Depth-only, shadow-only, no-environment, no-shadowing, no-volumetrics, diagnostics-only | PRD §6.6 full scenario set |
| **Local-light conventional shadows** | Phase 5G | not started | Spot-light conventional shadows and explicit point-light conventional strategy under ShadowService | Extends the directional-first Phase 4C baseline |
| **Non-runtime facades** | Phase 1 (substrate) → Phase 4E–5E (validation) | not started | ForSinglePassHarness (4E), ForRenderGraphHarness (4E), ForOffscreenScene (5E) | |
| **GeometryVirtualizationService** | Phase 7A | not started | Nanite-equivalent mesh virtualization | Stage 4 |
| **MaterialCompositionService** | Phase 7A | not started | DBuffer decals, deferred decals, material classification | Stages 7, 11 |
| **IndirectLightingService** | Phase 7B | not started | GI, reflections, canonical indirect environment evaluation, ambient-bridge retirement | Stage 13 |
| **SSAO / ScreenSpaceAO** | Phase 7B | not started | IndirectLightingService ambient occlusion product | |
| **MegaLights-class lighting extensions** | Phase 7B | not started | Advanced direct-lighting extension family under LightingService | Stage 12 |
| **Tiled/clustered deferred** | Phase 7B | not started | Deferred lighting optimization path | Profiling-justified |
| **Virtual shadow maps (VSM)** | Phase 7C | not started | ShadowService internal strategy upgrade | Stage 8 |
| **Volumetric fog / clouds** | Phase 7D | not started | EnvironmentLightingService internal activation | Stage 14 |
| **Heterogeneous volumes** | Phase 7D | not started | EnvironmentLightingService volumetric rendering family | Stage 14 |
| **WaterService** | Phase 7D | not started | Single layer water | Stage 16 |
| **Hair strands** | Phase 7D | not started | Specialized strand-rendering family against the reserved post-opaque extension slot | Stage 17 |
| **DistortionModule** | Phase 7D | not started | Distortion / translucent velocities | Stage 19 |
| **LightShaftBloomModule** | Phase 7D | not started | Light shaft bloom, translucency upscale | Stage 20 |
| **GBufferE / GBufferF** | Phase 7E | not started | Reserved SceneTextures slots for advanced material data | |
| **LightingChannelsTexture** | Phase 7E | not started | Per-object lighting channel mask in SceneTextures | |
| **Task-targeted global compositor** | Phase 7F | not started | Global composition orchestration | |
| **Public inter-view dependency DAGs** | Phase 7F | not started | Cross-view dependency declaration | |
| *Mobile renderer* | *excluded* | — | Out of scope per PRD | |

## 13. Verification Plan

### 13.1 Per-Phase Verification

Every phase must pass:

1. Full build succeeds (CMake + compile + link)
2. Full `Oxygen.Vortex.*` suite passes unless a narrower scope is explicitly justified and documented
3. `oxytidy` on the changed-file scope is clean, or every remaining warning is explicitly documented as pre-existing / deferred
4. Existing tests do not regress
5. Phase-specific exit gate met
6. `IMPLEMENTATION-STATUS.md` updated with evidence

### 13.2 Visual Verification Checkpoints

| Checkpoint | Phase | What to verify | Method |
| ---------- | ----- | -------------- | ------ |
| GBuffer debug views | 3 (after 3D) | Normals, base color, roughness, metallic, depth | Debug shader variants + `Run-VortexBasicDebugViewValidation.ps1` |
| Lit deferred scene | 3 (after 3E) | Correct diffuse + specular from deferred lighting | Visual + RenderDoc |
| Shadow terms | 4C | Directional shadow on ground plane | Visual + RenderDoc |
| Sky + ambient | 4D | Atmosphere renders, IBL ambient visible | Visual |
| Tonemapped output | 4B | HDR → LDR with correct exposure | Visual |
| **Migration parity** | **4E** | **Ported example matches legacy reference** | **RenderDoc A/B comparison at frame 10** |
| Composition to screen | 4F | Correct presentation through composition path | Visual |
| Translucency | 5B | Forward-lit blending over deferred background | Visual |
| Occlusion | 5C | Draw call reduction in complex scenes | Profiling counters |
| Multi-view | 5D | Heterogeneous views in one frame | Visual + RenderDoc |
| Offscreen | 5E | Correct offscreen render to texture | Visual |

### 13.3 Architectural Verification

At each milestone:

- Stage dispatch order matches ARCHITECTURE.md §6.2
- Subsystem services do not hold back-pointers to SceneRenderer
- SceneTextures products valid when consumed (SceneTextureSetupMode enforced)
- First active `SceneTextures` subset matches the PRD exactly, including explicit `Stencil` and `CustomDepth` allocation/access coverage
- Non-runtime facades work against Vortex substrate (M3+)
- Capability gating correctly enables/disables subsystems
- No `Oxygen.Renderer` includes in Vortex sources
- Composition path validated for each view type (single, multi, offscreen)

### 13.4 RenderDoc Capture Validation

Per ARCHITECTURE.md §11.3.1:

- Use frame 10 as baseline capture point
- Validate pass naming and scope labels are discoverable in captures
- Capture-driven validation required for: shadows, depth prepass, base pass,
  deferred lighting, GBuffer debug, scene-texture rebuild boundary, and
  **migration visual parity**
- Phase 3 now owns live runtime capture validation through `VortexBasic`
- The supported Phase 3 runtime entrypoint is
  `tools/vortex/Run-VortexBasicRuntimeValidation.ps1`
- The old frame-10 closeout pack is historical only and is not the current
  Phase 3 closure authority

## 14. Risk Areas

| Risk | Severity | Phase | Mitigation |
| ---- | -------- | ----- | ---------- |
| Renderer orchestrator stripping complexity | High | 1.8 | Start from a member/method inventory diff. Build incrementally. |
| New shader code paths (no legacy deferred) | High | 3 | GBuffer debug viz in 3F provides intermediate validation before full lighting |
| GBuffer encode/decode correctness | Medium | 3 | Use known-good reference materials for initial validation |
| Cross-subsystem data flow binding errors | Medium | 3–4 | RenderDoc capture validation at each visual checkpoint |
| SceneTextures four-part contract complexity | Medium | 2 | LLD design deliverable required before implementation (per §1.5 criteria) |
| Forward light data shape tuning | Low | 4A | Start with simplest scenario (1 directional), add complexity incrementally |
| InitViews module scope (~6.5k UE5 lines) | Medium | 3B | May need to be phased internally; LLD design will determine scope |
| Migration visual parity gap | Medium | 4E | Capture legacy baseline first; validate incrementally during port |
| Multi-view interaction complexity | Medium | 5D | Start with two identical views; add heterogeneity incrementally |
| Offscreen deferred/forward mode switching | Low | 5E | Reuse per-view ShadingMode validated in 5D |

## 15. Explicit Deferrals

Deferred to Phase 7 (post-release — see §9 and Feature Activation Matrix §12
for the complete assignment):

- Virtual shadow maps — Phase 7C (ShadowService internal strategy)
- Local-light conventional shadow expansion beyond the Phase 4C directional-first
  baseline — Phase 5G (ShadowService internal strategy update)
- Geometry virtualization — Phase 7A (Nanite-equivalent, stage 4)
- GI / reflections — Phase 7B (Lumen-equivalent, stage 13)
- MegaLights-class lighting extensions — Phase 7B (LightingService stage-12 extension family)
- Volumetric fog / clouds — Phase 7D (stage 14)
- Heterogeneous volumes — Phase 7D (stage 14)
- Light shaft bloom / translucency upscale — Phase 7D (stage 20)
- Hair strands — Phase 7D (reserved post-opaque extension slot, stage 17)
- Single layer water — Phase 7D (stage 16)
- MaterialCompositionService — Phase 7A (stages 7, 11)
- Distortion / translucent velocities — Phase 7D (stage 19)
- Tiled or clustered deferred optimization — Phase 7B
- GBufferE / GBufferF — Phase 7E
- SSAO / LightingChannelsTexture — Phase 7B / 7E
- Task-targeted global compositor — Phase 7F
- Public inter-view dependency DAGs — Phase 7F
- Mobile renderer path — excluded (PRD non-goal)

## 16. Exit Criteria

The Vortex renderer plan is complete when:

1. The architecture in ARCHITECTURE.md is reflected in code: SceneRenderer,
   SceneTextures (four-part contract), subsystem services, 23-stage frame
   ordering.
2. The design contracts in DESIGN.md plus phase LLDs are implemented: GBuffer
   base pass, deferred lighting, subsystem service interfaces, shared forward
   light data, shader contracts.
3. `Examples/Async` runs on Vortex with correct visual output matching legacy
   reference (PRD §6.1.1) — achieved at M4.
4. Non-runtime facades work against Vortex substrate.
5. Multi-view, offscreen, and feature-gated runtime scenarios validated
   (PRD §6.3–§6.6).
6. The legacy `Oxygen.Renderer` is deprecated and removed.
7. Reserved future slots exist for all 23 stages.
8. Feature Activation Matrix (§12) is fully populated with activation phase,
   current status, and final target for every major feature family including
   post-release items (PRD Goal 15).
