# Substrate Migration Guide

**Phase:** 1 — Substrate Migration
**Status:** `ready`

This guide covers the substrate migration from `Oxygen.Renderer` to
`Oxygen.Vortex`. Phase 1 introduces no new rendering systems and no new shader
code. The work is primarily copy + adapt, but it also includes a small set of
architecture-prescribed restructuring decisions needed to realize the Vortex
`Renderer Core` boundary without carrying forward the legacy renderer spine.

## 1. Scope and Context

### 1.1 What This Covers

All architecture-neutral substrate code must be migrated into the Vortex module
and compile independently. The substrate includes:

- cross-cutting types (frame bindings, view constants, POD structs)
- upload subsystem (ring-buffer staging, atlas, upload coordination)
- resources subsystem (geometry uploader, material/texture binders)
- scene preparation subsystem (scene traversal → render items)
- internal utilities (composition planner, view lifecycle, context pool)
- pass base classes (RenderPass, ComputeRenderPass, GraphicsRenderPass)
- view assembly and composition infrastructure
- renderer orchestrator (stripped of domain logic)

### 1.2 What This Does Not Cover

- No new GBuffer, SceneTextures, or deferred systems
- No new shader code
- No SceneRenderer layer (Phase 2)
- No subsystem services (Phase 3+)
- Legacy `Oxygen.Renderer` remains intact and functional throughout

### 1.2.1 Allowed Non-Mechanical Decisions Inside Phase 1

Phase 1 is intentionally narrow, but it is not "blind file copying." The
following bounded decisions are already made by the PRD, ARCHITECTURE, DESIGN,
and PLAN documents and are therefore allowed in this phase:

- removing the legacy `Pipeline/` abstraction instead of transplanting it
- stripping domain-specific members, orchestration, and public toggles from the
  legacy renderer so that Vortex `Renderer Core` owns substrate only
- adding empty SceneRenderer delegate hooks in `Renderer` for Phase 2 wiring
- resetting Vortex `RenderContext` pass registration to a substrate-only
  baseline instead of inheriting the legacy pass catalog wholesale
- introducing capability-family vocabulary entries already required by the
  Vortex architecture, while deferring their behavior to later phases

Anything beyond those bounded adaptations is out of scope for Phase 1 and must
be designed in the phase that owns it.

### 1.3 Source of Truth

- [PRD.md §3 Goals 10, 13, 14](../PRD.md) — preserve Oxygen substrate, strict
  legacy separation, and no compatibility obligation to legacy API/type shape
- [ARCHITECTURE.md §4.2](../ARCHITECTURE.md) — architectural axioms
- [ARCHITECTURE.md §5.1.1](../ARCHITECTURE.md) — `SceneRenderer` vs
  `Renderer Core` positioning
- [ARCHITECTURE.md §6.4](../ARCHITECTURE.md) — runtime persistence boundaries
- [ARCHITECTURE.md §7.7](../ARCHITECTURE.md) — injection boundaries and
  ownership rules
- [DESIGN.md §8](../DESIGN.md) — mechanical adaptation specification
- [PROJECT-LAYOUT.md](../PROJECT-LAYOUT.md) — authoritative file placement
- [PLAN.md §3](../PLAN.md) — Phase 1 work items and ordering

### 1.3.1 Execution and Verification Surfaces

This document is the **stable Phase 1 design contract**. It defines the allowed
forms of migration, ownership boundaries, and anti-drift rules. It does not
carry all live execution state by itself.

For actual execution and sign-off, use these documents together:

- this LLD for the stable migration contract
- `design/vortex/PLAN.md` for phase/step ownership
- `design/vortex/PROJECT-LAYOUT.md` for authoritative placements
- `design/vortex/IMPLEMENTATION-STATUS.md` for current completion state and
  evidence ledger
- `.planning/workstreams/vortex/phases/01-substrate-migration/01-RESEARCH.md`
  for exact dependency-order findings discovered from the real code graph
- `.planning/workstreams/vortex/phases/01-substrate-migration/01-VALIDATION.md`
  for the executable validation contract and phase-exit checks

Rule:

- do not treat the status tags in this LLD as the live execution control plane
- use this document to judge whether a migration decision is architecturally
  correct
- use the planning/validation surfaces to judge whether a migration slice is
  actually ready to execute or sign off

## 2. Mechanical Adaptation Rules

Every migrated file must apply these transformations consistently.

### 2.1 Namespace Changes

```cpp
// BEFORE (legacy)
namespace oxygen::engine { ... }
namespace oxygen::renderer { ... }

// AFTER (Vortex)
namespace oxygen::vortex { ... }
```

All using-declarations, forward declarations, and ADL-visible names must also
be updated. Nested namespaces like `oxygen::renderer::detail` become
`oxygen::vortex::detail`.

### 2.2 Export Macro Changes

| Legacy Macro | Vortex Macro | Purpose |
| ------------ | ------------ | ------- |
| `OXGN_RNDR_API` | `OXGN_VRTX_API` | DLL export/import |
| `OXGN_RNDR_NDAPI` | `OXGN_VRTX_NDAPI` | `[[nodiscard]]` + DLL export/import |

Per repo style: never export an entire class. Export individual methods only.
Use `OXGN_VRTX_NDAPI` for `[[nodiscard]]` methods because it already includes
the attribute.

### 2.3 Include Path Changes

```cpp
// BEFORE
#include <Oxygen/Renderer/Types/ViewConstants.h>
#include <Oxygen/Renderer/Passes/RenderPass.h>

// AFTER
#include <Oxygen/Vortex/Types/ViewConstants.h>
#include <Oxygen/Vortex/Passes/RenderPass.h>
```

Internal cross-references within the migrated module must all point to
`Oxygen/Vortex/` paths.

### 2.4 Include Guards

All files use `#pragma once`. No changes needed for guard macros.

### 2.5 CMake Integration

All migrated files are added to the appropriate CMake source list variables in
`src/Oxygen/Vortex/CMakeLists.txt`:

- `OXYGEN_VORTEX_HEADERS` — public headers
- `OXYGEN_VORTEX_PRIVATE_SOURCES` — implementation + private headers

Group files by directory in the same order as PROJECT-LAYOUT.md.

## 3. Migration Steps (In Order)

Each step depends on the previous. The ordering ensures each step compiles
before the next begins.

### Step 1.1 — Cross-Cutting Types (`Types/`)

**Source:** `Renderer/Types/` — 14 headers
**Target:** `Vortex/Types/`
**Status:** `done`

Files:

- `CompositingTask.h`
- `DebugFrameBindings.h`
- `DrawFrameBindings.h`
- `EnvironmentFrameBindings.h`
- `EnvironmentViewData.h`
- `LightCullingConfig.h`
- `LightingFrameBindings.h`
- `ShadowFrameBindings.h`
- `SyntheticSunData.h`
- `ViewColorData.h`
- `ViewConstants.h` / `ViewConstants.cpp`
- `ViewFrameBindings.h`
- `VsmFrameBindings.h`

**Adaptation:** Namespace, export macros, include paths only. No logic changes.
These are POD structs with no domain-specific behavior.

**Verification:** Build succeeds for `oxygen::vortex`. No include of
`Oxygen/Renderer/` in any migrated file.

### Step 1.2 — Upload Subsystem (`Upload/`)

**Source:** `Renderer/Upload/` — 14 files
**Target:** `Vortex/Upload/`
**Status:** `done`

Files: `AtlasBuffer`, `Errors`, `InlineTransfersCoordinator`,
`RingBufferStaging`, `StagingProvider`, `TransientStructuredBuffer`, `Types`,
`UploaderTag`, `UploadCoordinator`, `UploadHelpers`, `UploadPlanner`,
`UploadPolicy`, `UploadTracker` (each `.h` + `.cpp` where applicable).

**Adaptation:** Namespace, export macros, include paths. Internal
cross-references must point to `Oxygen/Vortex/Upload/`.

**Verification:** Build succeeds. DLL has zero `Oxygen.Renderer` references.

### Step 1.3 — Resources Subsystem (`Resources/`)

**Source:** `Renderer/Resources/` — 7 files
**Target:** `Vortex/Resources/`
**Status:** `done in current repo state; keep the design contract here`

Files:

- `GeometryUploader.h/.cpp`
- `MaterialBinder.h/.cpp`
- `TextureBinder.h/.cpp`
- `Types.h` (if separate from main Types/)

**Adaptation:** Namespace, export macros, include paths. `GeometryUploader`
depends on `Upload/` types — point to Vortex Upload, not legacy.

**Key constraint:** Resources depends on Types and Upload, both already
migrated. No reverse dependency allowed.

**Execution-critical dependency truth:** the resources slice also depends on a
smaller prerequisite ABI bundle that must exist in Vortex before `Resources/*`
is migrated. This bundle is part of the substrate contract and must not be
rediscovered ad hoc in every execution attempt.

Required prerequisite ABI bundle before `Resources/*`:

- `Types/PassMask.h`
- `Types/DrawMetadata.h`
- `Types/MaterialShadingConstants.h`
- `Types/ProceduralGridMaterialConstants.h`
- `Types/ConventionalShadowDrawRecord.h`
- `ScenePrep/Handles.h`
- `ScenePrep/GeometryRef.h`
- `ScenePrep/MaterialRef.h`
- `ScenePrep/RenderItemData.h`
- `PreparedSceneFrame.h/.cpp`

Dependency-selection rule:

- move the exact Vortex-local ABI surfaces that a migrated public header or
  translation unit names in signatures or real symbol references
- do not widen the prerequisite bundle just because a stale include exists
- do not introduce wrapper headers, duplicate types, or legacy-backed bridge
  seams to avoid this ordering

The planning research may refine the exact ordering, but any future execution
surface must preserve this dependency pattern.

### Step 1.4 — ScenePrep Subsystem (`ScenePrep/`)

**Source:** `Renderer/ScenePrep/` — 15 files
**Target:** `Vortex/ScenePrep/`
**Status:** `in progress in current repo state; keep the design contract here`

Files:

- `CollectionConfig.h`
- `Concepts.h`
- `Extractors.h`
- `FinalizationConfig.h`
- `Finalizers.h`
- `GeometryRef.h` (already in Vortex)
- `Handles.h` (already in Vortex)
- `MaterialRef.h` (already in Vortex)
- `RenderItemData.h` (already in Vortex)
- `RenderItemProto.h`
- `ScenePrepContext.h`
- `ScenePrepPipeline.h/.cpp`
- `ScenePrepState.h`
- `Types.h`

**Adaptation:** Namespace, export macros, include paths. The ScenePrepPipeline
is template-based with pluggable extractors — no logic changes needed.

**Key constraint:** ScenePrep depends on Types. Some files
(`GeometryRef.h`, `Handles.h`, `MaterialRef.h`, `RenderItemData.h`) are
already migrated as prerequisite ABI bundles.

**Boundary-preserving adaptation rule for split execution:** when a header/data
slice is intentionally migrated before its later execution slice, it is allowed
to:

- remove stale includes that are not backed by real symbol use
- replace hard includes on later-phase execution headers with forward
  declarations when that preserves the current contract shape and ownership
  boundary
- replace direct legacy domain includes with forward declarations or Vortex
  local includes when the goal is to keep the migrated slice substrate-only

It is not allowed to:

- create placeholder bridge types that will need later cleanup
- duplicate execution types locally just to satisfy include order
- silently widen a header-only migration into later-domain ownership
- preserve a legacy include path merely to keep the old header shape intact

### Step 1.5 — Internal Utilities (`Internal/`)

**Source:** `Renderer/Internal/` + selected `Renderer/` root files — ~7 files
**Target:** `Vortex/Internal/`
**Status:** `planned`

Files:

- `BlueNoiseData.h`
- `PerViewStructuredPublisher.h`
- `RenderContextMaterializer.h`
- `RenderContextPool.h`
- `RenderScope.h`

**Adaptation:** Namespace, export macros, include paths.
`PerViewStructuredPublisher` is a template — mechanical changes only.

**Key constraint:** These files are `Internal/` — they must never appear in
public headers. CI can enforce this by checking `#include` directives.

### Step 1.6 — Pass Base Classes (`Passes/`)

**Source:** `Renderer/Passes/` — 3 file pairs
**Target:** `Vortex/Passes/`
**Status:** `planned`

Files:

- `RenderPass.h/.cpp` — base: lifecycle, bindings, draw helpers
- `ComputeRenderPass.h/.cpp` — base: compute PSO management
- `GraphicsRenderPass.h/.cpp` — base: graphics PSO, render targets

**Adaptation:** Namespace, export macros, include paths. These define the
coroutine-based pass execution model (`PrepareResources` → `Execute`). No
logic changes.

**Key constraint:** Pass base classes depend on Types, Internal, and Graphics
layer. No domain-specific logic allowed in base classes.

### Step 1.7 — View Assembly and Composition Infrastructure

**Source:** `Renderer/Pipeline/` + `Renderer/Internal/` — redistributed
**Target:** Multiple Vortex directories (see below)
**Status:** `planned`

This is the most complex substrate step. It eliminates the `Pipeline/`
abstraction and redistributes files per ARCHITECTURE.md and PROJECT-LAYOUT.md.

**Vocabulary types → Vortex root:**

- `CompositionView.h` — per-view composition descriptor
- `RendererCapability.h` — capability-family bitmask vocabulary
- `RenderMode.h` — wireframe / solid / debug enum

**Scene policy → `SceneRenderer/`:**

- `DepthPrePassPolicy.h` — depth pre-pass mode enum

**Renderer Core internal → `Internal/`:**

- `CompositionPlanner.h/.cpp` — composition planning infrastructure
- `CompositionViewImpl.h/.cpp` — composition view realization
- `ViewLifecycleService.h/.cpp` — view lifecycle management
- `FrameViewPacket.h/.cpp` — per-frame view packets

**SceneRenderer internal → `SceneRenderer/Internal/`:**

- `FramePlanBuilder.h/.cpp` — frame planning for desktop stages
- `ViewRenderPlan.h` — per-view render plan

**NOT carried over:**

- `RenderingPipeline.h` — replaced by SceneRenderer architecture
- `PipelineFeature.h` — replaced by capability-family model
- `PipelineSettings.h/.cpp` — replaced by SceneRenderer configuration

**Adaptation:** Beyond mechanical namespace/macro/include changes:

- `CompositionView` preserves its required semantic role as per-view intent and
  routing input. Preserve behavior and ownership boundaries, not legacy API
  shape by default. Existing names and factories may be kept where they remain
  the clearest fit, but Vortex has no obligation to preserve the full legacy
  public surface or legacy type compatibility.
- `RendererCapability` adds `kDeferredShading` to the enum
- Internal files adapt to point at Vortex types and paths

**Guardrail for `kDeferredShading`:**

- this is vocabulary only in Phase 1; it does not activate deferred systems,
  allocate scene textures, or create a parallel pipeline path
- it exists so Phase 2 `SceneRenderBuilder` and `SceneRenderer` can reason
  about deferred-first desktop capability without retrofitting the substrate
  later
- it must not be used to reintroduce `ForwardPipeline`/`DeferredPipeline`
  product thinking into `Renderer Core`
- absence of this flag must not create a legacy-renderer bridge or fallback
  dependency on `Oxygen.Renderer`

### Step 1.8 — Renderer Orchestrator

**Source:** `Renderer/Renderer.h/.cpp` — monolithic (~4800 lines, ~65 members)
**Target:** `Vortex/Renderer.h/.cpp` — stripped substrate (~1500 lines, ~10
members)
**Status:** `planned`

This is the only substantially restructuring step. The monolithic legacy
renderer must be stripped down to the Renderer Core substrate. That
restructuring is still Phase-1-legal because the target ownership model is
already fixed by PRD, ARCHITECTURE, DESIGN, and PLAN; this step realizes that
model rather than inventing a new one.

**What to keep (Renderer Core responsibilities):**

1. Engine module lifecycle (`Attach`, `Start`, `RenderFrame`, `Compositing`,
   `FrameEnd`, `Detach`)
2. `RenderContext` allocation and materialization
3. View registration and canonical runtime state
4. Upload/staging service coordination
5. Publication substrate (ViewConstants, ViewFrameBindings, baseline
   publication)
6. Composition planning, queueing, target resolution, compositing execution
7. Non-runtime facade surfaces (SinglePassHarness, RenderGraphHarness,
   OffscreenScene)
8. Scene renderer dispatch hooks (initially no-ops)

**What to strip:**

1. Domain member variables (~60 → ~10 kept):
   - Remove: `LightManager`, `ShadowManager`, environment managers, shadow
     pass instances, lighting pass instances, diagnostic managers, ImGui state,
     domain-specific GPU resources
   - Keep: `RenderContext` pool, upload coordinator, staging provider, inline
     transfers, scene prep state, view lifecycle service, composition planner
2. Domain orchestration from frame-loop methods:
   - Remove: light culling dispatch, shadow dispatch, environment dispatch,
     diagnostic dispatch, all pass-specific orchestration
   - Keep: context allocation, view publication, upload staging, composition
     execution
3. Domain public API and console bindings:
   - Remove: shadow toggles, VSM settings, environment toggles, diagnostic
     commands
   - Keep: capability queries, view registration, facade entry points
4. `RenderContext` pass-type registry:
   - Clean: `KnownPassTypes` starts empty in Vortex. Subsystem services
     extend it when registered in later phases
5. FacadePresets include paths → update to Vortex paths

**Scene renderer dispatch hooks:**

Add delegate hooks that the SceneRenderer (Phase 2) will fill:

```cpp
// In Renderer's frame-loop methods:
void Renderer::OnPreRender(const FrameContext& frame) {
  // ... substrate work ...
  if (scene_renderer_) scene_renderer_->OnPreRender(frame);
}

void Renderer::OnRender(RenderContext& ctx) {
  // ... substrate work ...
  if (scene_renderer_) scene_renderer_->OnRender(ctx);
}
```

Initially these are null/no-ops and do nothing.

**Key files also needed alongside Renderer.h/.cpp:**

- `RenderContext.h` — the execution context (adapt, keep)
- `PreparedSceneFrame.h/.cpp` — frame-prepared scene payload (already migrated)
- `SceneCameraViewResolver.h/.cpp` — scene camera → view resolution
- `Errors.h` — public error surface
- `FacadePresets.h` — non-runtime helper presets
- `RendererTag.h` — module identity (already migrated)

### Step 1.9 — Smoke Test

**Source:** New
**Target:** `Vortex/Test/Link_test.cpp`
**Status:** `planned`

Create a minimal test that:

1. Instantiates `vortex::Renderer` with an empty capability set
2. Verifies frame-loop methods execute without crashing
3. Confirms no dependency on `Oxygen.Renderer`

```cpp
TEST(VortexLinkTest, RendererInstantiatesWithEmptyCapabilities) {
  // Construct with empty CapabilitySet
  // Call frame lifecycle methods
  // Verify no crash
}
```

## 4. Validation

### 4.1 Per-Step Dependency and Separation Checks

After each step, verify:

1. **Build:** `cmake --build --target Oxygen.Vortex` succeeds
2. **No legacy leakage:** `rg -g "*.h" -g "*.cpp" "Oxygen/Renderer/"
   src/Oxygen/Vortex/` returns zero hits (excluding comments/docs)
3. **No reverse dependency:** Legacy `Oxygen.Renderer` does not include any
   `Oxygen/Vortex/` header
4. **DLL independence:** The built `Oxygen.Vortex` DLL has no runtime
   dependency on `Oxygen.Renderer` symbols
5. **No target-link edge:** `oxygen-vortex` must not link against
   `oxygen-renderer` directly or transitively
6. **No public API backflow:** Public Vortex headers must not expose legacy
   renderer namespaces, legacy renderer types, or bridge/adaptor types whose
   purpose is to proxy into `Oxygen.Renderer`
7. **No bridge architecture:** No new file in Vortex may exist solely to adapt
   calls, ownership, or data flow back into the legacy renderer module
8. **No shared ownership seam:** Vortex code may copy/adapt legacy
   implementation, but ownership at build/runtime/API level must terminate
   inside Vortex; no mixed Vortex/legacy control path is allowed

These checks are required to satisfy the PRD's hermetic-separation rule. "No
legacy leakage" is not just include hygiene; it covers build graph, runtime
linkage, public contract shape, and ownership flow.

### 4.2 Phase 1 Exit Verification Contract

This section defines the minimum evidence needed for architectural sign-off of
Phase 1. It complements, but does not replace, the executable commands in the
Phase 1 validation pack.

Phase 1 is not complete unless all of the following are true:

1. the migrated substrate lives under Vortex-owned placements defined by
   `PROJECT-LAYOUT.md`
2. `Renderer Core` responsibilities are preserved while scene/domain ownership
   remains stripped from `Renderer`
3. no `Pipeline` abstraction, renderer-to-renderer bridge seam, or legacy
   dependency edge remains in Vortex
4. the Vortex smoke path constructs `oxygen::vortex::Renderer` and exercises
   the stripped frame hooks
5. the relevant legacy substrate regressions still pass
6. `IMPLEMENTATION-STATUS.md` records the evidence literally and does not claim
   completion on missing validation

The authoritative executable command set, sampling cadence, and per-task proof
map live in:

- `.planning/workstreams/vortex/phases/01-substrate-migration/01-VALIDATION.md`

Architectural sign-off should read that validation pack together with this LLD,
not instead of it.

## 5. Common Pitfalls

1. **Forgetting internal cross-references.** When migrating `Upload/`, all
   internal includes between Upload files must point to `Oxygen/Vortex/Upload/`,
   not `Oxygen/Renderer/Upload/`.

2. **Leaving forward declarations pointing to legacy namespaces.** Search for
   `namespace oxygen::renderer` and `namespace oxygen::engine` after each step.

3. **Breaking the legacy module.** The legacy module must continue to compile
   and pass its own tests after each Vortex migration step. Do not modify
   legacy files.

4. **Prematurely adding domain code.** Phase 1 is mechanical. Do not add
   GBuffer types, SceneTextures, or deferred-path logic. Those belong to
   Phase 2+.

5. **Skipping the pass-type registry cleanup.** The Vortex `RenderContext`
   must start with an empty `KnownPassTypes`. Copy the legacy list of 25+
   pass types and you will create false dependencies.

6. **Accidentally preserving legacy API shape as a requirement.** Vortex may
   reuse legacy names where they still fit, but preserving the legacy public
   API or type compatibility is not a success criterion for Phase 1.

7. **Treating `kDeferredShading` as behavior instead of vocabulary.** Adding
   the enum value in Phase 1 does not permit deferred-path logic, scene-texture
   allocation, or a second pipeline abstraction to appear in substrate code.

8. **Forcing mechanical copies across a split phase boundary.** If a header
   slice is deliberately migrated before later execution files, keeping the old
   include graph unchanged can create false dependencies on later-domain code.
   In that case, remove stale includes or use forward declarations where the
   public contract remains intact and the ownership boundary becomes cleaner.

9. **Using this LLD as live status instead of stable design.** Current
   execution state must come from `IMPLEMENTATION-STATUS.md` and the active
   Phase 1 planning pack. This LLD defines what is allowed and what must be
   true, not the minute-by-minute execution checkpoint.

## 6. Open Questions

None currently. The allowed restructuring decisions for Phase 1 are already
constrained by the PRD, ARCHITECTURE, DESIGN, and PLAN documents cited in §1.3.
If a migration step appears to require a new ownership model, new compatibility
bridge, or new renderer behavior rather than substrate adaptation, stop and
write the owning phase design instead of extending Phase 1 ad hoc.
