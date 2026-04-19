# Migration Playbook LLD

**Phase:** 4E - First Migration
**Deliverable:** D.13
**Status:** `ready`

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

## 1. Scope and Context

### 1.1 What This Covers

This document is the step-by-step playbook for migrating `Examples/Async` from
the legacy `Oxygen.Renderer` path to `Oxygen.Vortex`.

This is the PRD Section 6.1.1 first-success gate. The migrated example must:

- produce correct visual and behavioral output that closes the owning UE5.7
   parity gate
- run through the real Vortex runtime seams
- avoid long-lived compatibility clutter
- prove that the Phase 4 service set is sufficient for one real Oxygen runtime
  surface

### 1.2 Why `Examples/Async`

`Examples/Async` is the designated first migration target because it is a real
runtime surface, not a toy shell:

- it exercises scene setup, frame dispatch, composition, and presentation
- it carries DemoShell / ImGui integration
- it includes a spotlight and an atmosphere-enabled main composition view
- it is smaller than a full editor/runtime migration while still proving the
  Vortex path against a real app module

The first-success gate remains `Examples/Async`. `DemoShell` is part of the
example's integration surface, but it is **not** a separate co-equal Phase 4
migration gate unless product intent is explicitly widened.

### 1.3 Architectural Authority

- [ARCHITECTURE.md](../ARCHITECTURE.md) - runtime seam ownership,
  composition, resolve, extraction, and handoff boundaries
- [PLAN.md](../PLAN.md) Section 6 - Phase 4 ordering and success criteria
- [IMPLEMENTATION-STATUS.md](../IMPLEMENTATION-STATUS.md) - current truth
  ledger

## 2. UE5.7 Parity Evidence Pack

### 2.1 Frame-10 Parity Capture

Before any migration completion claim:

1. Capture `Examples/Async` on the real Vortex runtime path at frame 10.
2. Derive the comparison checklist from the relevant UE5.7 runtime and shader
   source for every in-scope feature family.
3. Save parity evidence artifacts for:
   - final back buffer / presented image
   - depth visualization
   - any visible intermediate render targets that materially affect the scene
   - observable behavior notes for the async workflow

### 2.2 Evidence Artifacts

| Artifact | Format | Purpose |
| -------- | ------ | ------- |
| `baseline_frame10.png` | screenshot | parity evidence image |
| `baseline_depth.png` | screenshot | depth parity evidence |
| `baseline_renderdoc.rdc` | RenderDoc capture | pass / resource inspection against the owning UE5.7 contract |
| `baseline_behaviors.md` | notes | observable workflow checklist for the parity gate |

### 2.3 Truthful Phase 4 Feature Baseline

The Phase 4 parity target is not "every renderer feature the app can
eventually grow." It is the smallest truthful baseline required by the live
example and the Phase 4 scope:

| Feature | Baseline Status | Why |
| ------- | --------------- | --- |
| Deferred direct lighting | required | core Phase 3 -> Phase 4 lighting path |
| Atmosphere-enabled main scene view | required | the live example sets `with_atmosphere = true` on its main composition view |
| Spotlight presence | required | the live example creates and updates a camera spotlight |
| Spotlight shadows | not required in baseline | the live example's settings default keeps `casts_shadows = false`; Phase 4C remains directional-first |
| Post-process visible output | required | the migrated surface must reach real Stage-22 sampled tonemapped output before composition |
| Directional conventional shadows | required | this is the truthful Phase 4C baseline |
| Local-light conventional shadows | not required in baseline | later `ShadowService` expansion owns them |

If the migration target is found to rely materially on spotlight shadows or any
other out-of-scope feature, the design package must be widened explicitly
instead of silently treating that dependency as optional.

## 3. Real Legacy Seam Inventory

The migration is not just an include/namespace rewrite. The current
`Examples/Async` module still depends on real legacy renderer seams.

### 3.1 Known Legacy Renderer Couplings

The live example currently uses legacy renderer families such as:

- legacy renderer includes under `Oxygen/Renderer/...`
- `renderer::ForwardPipeline`
- `renderer::CompositionView`
- legacy renderer-side camera/view routing helpers
- legacy DemoShell active-pipeline integration
- legacy renderer-owned view registration patterns

### 3.2 Required Seam Replacements

The migration must replace those seams with truthful Vortex equivalents:

| Current Seam | Migration Requirement |
| ------------ | --------------------- |
| `ForwardPipeline` ownership | replace with the Vortex scene-renderer / composition path; no parallel legacy pipeline kept alive |
| legacy `CompositionView` routing | move to the Vortex-owned composition/view contract without local shortcut paths |
| DemoShell `get_active_pipeline` dependency | replace with a Vortex-facing runtime seam that still supports the required UI/runtime behavior |
| legacy renderer-owned view registration assumptions | route through Vortex Renderer Core publication and composition planning |
| legacy renderer pass/config hooks | replace with Vortex-owned equivalents or remove them if they are legacy-only scaffolding |

### 3.3 Zero-Shim Rule

The migration must leave **zero** long-lived compatibility clutter:

- no legacy/Vortex dual runtime path inside the example
- no `#ifdef LEGACY_RENDERER` split
- no adapter layer that just re-exposes legacy renderer APIs under a new name
- no example-local bypass of the Vortex composition/publication path

## 4. Migration Steps

### 4.1 Step 1 - Replace Includes, Namespaces, and Public Types

Replace legacy renderer includes and namespaces only as part of the broader
seam migration:

| Legacy | Vortex |
| ------ | ------ |
| `#include "Oxygen/Renderer/..."` | `#include "Oxygen/Vortex/..."` |
| `oxygen::renderer::` | `oxygen::vortex::` |
| `OXGN_RNDR_API` | `OXGN_VRTX_API` |

This step does **not** by itself prove the migration is viable.

### 4.2 Step 2 - Replace Runtime Bootstrap and View/Composition Seams

Before treating the example as migrated:

1. verify the Vortex bootstrap path reaches the real
   `SceneRenderBuilder` / `SceneRenderer` runtime seam
2. verify Renderer Core owns:
   - current-view materialization
   - publication
   - composition planning
   - target resolution
   - composition submission / presentation handoff
3. remove any dependence on the legacy active-pipeline abstraction

### 4.3 Step 3 - Rehome Scene Setup and Lighting Hooks

Verify scene setup works through engine/Vortex public surfaces only:

- scene graph construction
- geometry/material setup
- camera setup
- spotlight creation/update
- atmosphere-enabled main composition view

If the example currently reaches renderer internals directly, those calls must
be refactored to a Vortex-facing runtime seam rather than carried forward.

### 4.4 Step 4 - Verify Frame Loop Integration

The external frame loop shape may remain compatible, but the migration is only
truthful if the work beneath it now routes through the real Vortex runtime
path.

Compatibility of the outer loop is necessary but not sufficient.

### 4.5 Step 5 - Validate Async Behavior

`Examples/Async` exercises asynchronous operations:

- async tasks complete correctly
- no deadlocks / race conditions are introduced by the Vortex migration
- observable behavior matches the baseline

### 4.6 Step 6 - Validate Non-Runtime Facades

Validate these facades against the remediated Vortex substrate:

1. `ForSinglePassHarness()`
2. `ForRenderGraphHarness()`

These remain part of the Phase 4 scope and must be proven against Vortex.
They are rerun here not because Phase 4 redefines those facade contracts, but
because the migrated runtime now activates the Phase 4 service set through the
same SceneRenderBuilder / capability-gated Vortex substrate and the facades
must remain regression-free against that expanded runtime baseline.

## 5. Visual and Behavior Parity Validation

### 5.1 RenderDoc Comparison

1. Capture `Examples/Async` on Vortex at frame 10.
2. Compare against the relevant UE5.7-derived parity checklist:
   - final back buffer / presented image
   - stage-family ordering and discoverable names
   - major artifact boundaries and intermediate correctness
   - atmosphere-visible output
   - spotlight-visible contribution

### 5.2 Acceptance Criteria

| Criterion | Metric | Threshold |
| --------- | ------ | --------- |
| Phase 4 service set live in migrated run | design/runtime inspection | `LightingService`, `PostProcessService`, `ShadowService`, and `EnvironmentLightingService` all active as the seams under test |
| Visual match | parity evidence review plus optional image diff | must satisfy the owning UE5.7 parity gate |
| Depth accuracy | depth comparison | must satisfy the owning UE5.7 parity gate |
| Async behavior | observable behavior match | all required parity-gate behaviors preserved |
| No compatibility clutter | code inspection | zero long-lived shims |

### 5.3 Known Acceptable Differences

- tone-mapping curve differences that stay within the documented threshold
- bloom-intensity tuning differences
- implementation-detail shadow filtering differences for the directional
  baseline

These do **not** justify silently widening or narrowing the feature baseline.

## 6. Runtime Proof Boundary

The truthful Phase 4 runtime proof boundary is:

1. `Examples/Async` migrated to Vortex
2. `LightingService`, `PostProcessService`, `ShadowService`, and
   `EnvironmentLightingService` are all live in the migrated run as the seams
   under test
3. real composition submission / presentation path in use
4. RenderDoc frame-10 capture from that migrated example
5. the owning UE5.7 parity gate closed with explicit visual/behavior evidence

This document does **not** require `DemoShell` to become a separate standalone
Phase 4 migration gate. DemoShell remains part of the example integration
surface that must work correctly inside `Examples/Async`.

## 7. Composition and Presentation Validation (Phase 4F)

The migration already requires the real runtime composition path to be live.
Phase 4F therefore deepens proof of the retained Stage-21 / Stage-23
artifacts rather than introducing composition for the first time.

After visual/behavior parity is confirmed:

1. validate single-view composition to screen under explicit inspection
2. validate `ResolveSceneColor` end-to-end when resolve work is actually needed
3. validate `PostRenderCleanup` extraction/handoff artifacts

## 8. Testability Approach

1. **Automated regression:** screenshot comparison at frame 10 against the
   baseline.
2. **Manual RenderDoc review:** inspect stage ordering, atmosphere output,
   spotlight contribution, and presentation handoff.
3. **Behavior validation:** run the async workflow and verify completion /
   interaction behavior.
4. **Leak / shutdown validation:** PIX or DXGI debug-layer check with clean
   shutdown expectations.

## 9. Open Questions

None for the Phase 4 migration baseline.

If the migrated example proves to require spotlight shadows or another
out-of-scope feature, the Phase 4 design package must be widened explicitly
before implementation continues.
