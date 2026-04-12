# Modular Renderer PRD

Status: `phase-2 implementation-ready PRD`

This document captures the product requirements for the modular renderer effort.
It describes what the renderer package must achieve and how success will be
judged. It intentionally avoids solution-level ownership tables, API minutiae,
and implementation sequencing.

Related:

- [ARCHITECTURE.md](./ARCHITECTURE.md)
- [DESIGN.md](./DESIGN.md)
- [PLAN.md](./PLAN.md)

## 1. Problem Statement

The current Oxygen renderer behaves too much like an all-or-nothing package:

- the production renderer eagerly owns and initializes a large fixed service set
- the main scene-rendering path is tightly coupled to that service set
- non-runtime scenarios exist today, but mostly through ad hoc seams
- composition currently bottlenecks through a scalar pending-submission model

This makes it harder than it should be to assemble a renderer intentionally for:

- full runtime
- single-pass testing
- render-graph testing
- offscreen scene rendering
- editor-facing view/surface scenarios
- feature-selected runtime variants

## 2. Goals

1. Preserve Oxygen's render-graph-as-coroutine model.
2. Preserve the ability to define multiple pipeline types.
3. Preserve pass-level configurability through existing config objects.
4. Make renderer capabilities composable by scenario.
5. Keep the production runtime path minimalistic and stable.
6. Keep the public API ergonomic.
7. Keep `RenderContext` as the authoritative execution context for passes.
8. Reuse Oxygen Composition where it fits naturally.
9. Support legal non-runtime setup without polluting the production path.
10. Replace the architectural single-submission bottleneck in composition.
11. Begin physically extracting capability-family services out of the
    monolithic `Renderer` class so that each family owns its own state,
    initialization, per-view management, publication, and shutdown.

## 3. Non-Goals

1. Replace the coroutine render-graph model with a generic DAG framework.
2. Turn passes or pipelines into a plugin-facing extension ecosystem.
3. Finalize every future pipeline up front.
4. Solve the full long-term multi-surface/task-target compositor in phase 1.
5. Introduce a general public inter-view dependency graph in phase 1.
6. Finalize the exact class hierarchy.
7. Finalize whether composition uses runtime registration, compile-time
   templates, or a mixed model for every layer.
8. Introduce a second competing architecture for pipeline assembly.

## 4. Stakeholders and Users

Primary internal users:

- engine architecture developers
- rendering system developers
- test and harness authors
- tools/editor developers

The design assumes passes and pipelines are engine-authored production code.

## 5. Target Scenarios

Default conceptual model:

- one renderer assembly per scenario is the preferred baseline
- shared internals across assemblies may exist later as an implementation
  optimization, but they are not the conceptual model the package is built
  around

## 5.1 Full Runtime Renderer

The default production renderer:

- scene preparation
- uploads
- lighting
- shadows
- environment
- forward scene rendering
- overlays
- composition
- presentation

## 5.2 Single Pass Harness

Minimal non-runtime harness for:

- executing one pass against a validated execution context
- focused GPU pass tests
- contract validation for one pass

This is intentionally distinct from render-graph testing and from scene
rendering. It exists to host one pass-oriented execution surface only.

## 5.3 Render Graph Harness

Low-level non-runtime harness for:

- executing one explicit caller-authored render-graph coroutine
- graph-level validation without the full runtime stack

This is intentionally distinct from both single-pass hosting and offscreen scene
rendering. It exists to run one caller-authored graph against a validated
context.

## 5.4 Offscreen Scene Renderer

Higher-level non-runtime path for:

- material preview
- thumbnail generation
- scene captures
- editor preview panels

## 5.5 Editor Multi-View and Multi-Surface Renderer

A renderer that can intentionally support:

- multiple scene views
- tool overlays
- editor viewport surfaces
- picture-in-picture
- minimap-like secondary composition

This remains an explicit target scenario in the design package, even if phase-1
implementation uses a narrower composition subset.

## 5.6 Feature-Selected Runtime Variants

Runtime assemblies where some capability families are intentionally absent, such
as:

- no environment lighting
- no shadowing
- no final output composition
- no diagnostics
- scene-prep-only
- shadow-only

## 6. Phase-1 Required Outcomes (delivered)

Phase 1 delivered these architectural outcomes:

1. `Renderer` remains the structural composition root and execution substrate.
2. `Pipeline` remains the interchangeable policy/configuration layer.
3. `RenderGraph` remains a coroutine.
4. `RenderPass` remains the reusable execution primitive.
5. The non-runtime entry points exist and are semantically distinct:
   - `ForSinglePassHarness()`
   - `ForRenderGraphHarness()`
   - `ForOffscreenScene()`
6. Non-runtime setup uses separate facades and separate staging state.
7. `RenderContext` is authoritative and only becomes executable after legal
   runtime setup or facade finalization.
8. Pipeline capability declaration supports coarse `required` and `optional`
   families.
9. Composition remains based on `CompositionSubmission` in phase 1.
10. Composition moves from a scalar pending-submission model to queued
    multi-submission, single-target execution.
11. Editor multi-view / multi-surface remains an explicit target scenario in the
    package.

See [IMPLEMENTATION-STATUS.md](./IMPLEMENTATION-STATUS.md) for proof artifacts.

## 6.1 Phase-2 Required Outcomes

Phase 2 must deliver these outcomes:

1. **Environment Lighting extraction.** All environment-lighting state,
   initialization, per-view management, publication, and shutdown move from
   `Renderer` into a dedicated `EnvironmentLightingService` class.
2. **Renderer delegates, not orchestrates.** After extraction, `Renderer` holds
   a capability-gated `unique_ptr<EnvironmentLightingService>` and delegates
   environment work to it. `Renderer` no longer contains any
   environment-specific member variables, helper methods, or publication logic.
3. **Capability gating preserved.** The service is instantiated only when
   `kEnvironmentLighting` is present in the renderer assembly. Pipelines that
   do not require environment lighting continue to work unchanged.
4. **Publication ownership transfer.** The two optional-family publishers
   (`EnvironmentViewData`, `EnvironmentFrameBindings`) and their frame-start
   lifecycle move into the service. The renderer continues to own the generic
   publication model, but the service owns the concrete publisher instances and
   the environment publish calls.
5. **Per-view state ownership transfer.** The per-view atmosphere LUT map
   (`per_view_atmo_luts_`), atmosphere generation tracking, inactive-view
   eviction tracking, and environment-specific current-view wiring move into
   the service. The shared renderer runtime-state container may remain
   renderer-owned, but the environment contribution to it becomes
   service-owned logic.
6. **Renderer-owned pass inversion resolved.** The three renderer-owned
   environment passes (`sky_capture_pass_`, `sky_atmo_lut_compute_pass_`,
   `ibl_compute_pass_`) move into the service as service-owned passes.
7. **Debug/runtime toggles move with the service.** Environment runtime toggles
   such as blue-noise enablement move into the service. There are currently no
   environment-specific console bindings in `Renderer::RegisterConsoleBindings`,
   so phase 2 does not invent new console scope just to satisfy the extraction.
8. **Production runtime and all existing tests remain green.** The extraction
   is a structural refactor with no behavioral change.
9. **Extraction pattern documented.** The service extraction establishes a
   reusable pattern for subsequent family extractions (Diagnostics, Shadowing,
   etc.).
10. **`Renderer` stops owning environment implementation.**
    Environment-specific public bridge methods may remain on `Renderer` in
    phase 2, but only as thin delegations into the service. `Renderer` no
    longer owns environment members, helper logic, or
    publication/orchestration blocks.

## 7. Constraints and Assumptions

1. Passes are engine-authored and engine-delivered.
2. Pipelines are engine-authored production code.
3. Production-path simplicity is preferred over abstraction for abstraction's
   sake.
4. Composition is a suitable tool for coarse capability assembly, but not for
   arbitrary repeated pass instances.
5. Phase-1 architecture should favor bounded, production-shaped progress over
   maximal generality.

## 8. Success Criteria

### Phase-1 success criteria (met)

1. The three non-runtime facades exist with semantically distinct authority.
2. `RenderContext` remains the authoritative execution context.
3. Renderer/pipeline ownership follows the split defined in
   [ARCHITECTURE.md](./ARCHITECTURE.md).
4. The phase-1 capability-family model defined in
   [ARCHITECTURE.md](./ARCHITECTURE.md) is the architectural basis of the
   system.
5. Pipeline capability declaration remains coarse and bind-time validated.
6. Phase-1 composition uses queued multi-submission, single-target submission.
7. The design does not require a second pipeline implementation, a public
   dependency DAG, or a richer final-frame handoff to be considered complete.
8. The package preserves editor multi-view / multi-surface rendering as an
   explicit target scenario, even when phase-1 implementation uses a narrower
   composition subset.

### Phase-2 success criteria

1. `EnvironmentLightingService` exists as a standalone class that owns all
   environment-lighting state.
2. `Renderer` holds a single `unique_ptr<EnvironmentLightingService>` for the
   entire environment-lighting surface. No environment member variables,
   orchestration blocks, or per-view helper logic remain directly in
   `Renderer`. Any retained public bridge methods are pure forwards.
3. The service is instantiated only when `kEnvironmentLighting` is present.
   Absent-capability scenarios still work.
4. All existing tests pass. The `DemoShell` / `RenderScene` / `MultiView`
   examples run without regression.
5. A short "Capability-Family Service Extraction Guide" section exists in
   [DESIGN.md](./DESIGN.md) that a developer can follow to extract the next
   family.
6. Focused verification exists for both capability-present and
   capability-absent environment paths, using the active renderer proof
   surface.

## 9. Deferred Beyond Phase 2

The following are intentionally deferred:

- full task-targeted global compositor
- richer final-frame contracts beyond `CompositionSubmission`
- richer inter-view dependency mechanisms
- generalized public graph-based view dependency authoring
- broad plugin-style capability ecosystems
- extraction of remaining capability families (Diagnostics, Shadowing, Lighting
  Data) beyond the phase-2 Environment Lighting proof

## 10. Open Product Questions

Product-level questions still worth revisiting after phase 2:

1. Which capability family should be extracted next after Environment Lighting?
   (Diagnostics is the most self-contained candidate; Shadowing has the highest
   complexity due to depth-chain coupling.)
2. Should per-view state management become a shared framework across extracted
   services, or should each service manage its own per-view map independently?
3. Which future scenario should most strongly drive phase-3 investment:
   multi-surface composition, additional family extraction, or broader runtime
   profile variation?
