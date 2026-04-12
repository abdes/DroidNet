# Modular Renderer PRD

Status: `phase-1 working PRD`

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

## 6. Phase-1 Required Outcomes

Phase 1 must deliver these architectural outcomes:

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

Phase 1 is successful when all of the following are true:

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

## 9. Deferred Beyond Phase 1

The following are intentionally deferred:

- full task-targeted global compositor
- richer final-frame contracts beyond `CompositionSubmission`
- richer inter-view dependency mechanisms
- generalized public graph-based view dependency authoring
- broad plugin-style capability ecosystems

## 10. Open Product Questions

Product-level questions still worth revisiting later:

1. What should become the first architectural target beyond phase 1?
2. Which future scenario should most strongly drive phase-2 investment:
   multi-surface composition, richer inter-view dependency, or broader runtime
   profile variation?
