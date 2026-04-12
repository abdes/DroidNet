# Modular Renderer Plan

Status: `phase-1 execution plan`

This document captures the implementation and migration plan for the modular
renderer phase-1 architecture. It assumes the stable architecture in
[ARCHITECTURE.md](./ARCHITECTURE.md) and the concrete solution shape in
[DESIGN.md](./DESIGN.md).

Related:

- [PRD.md](./PRD.md)
- [ARCHITECTURE.md](./ARCHITECTURE.md)
- [DESIGN.md](./DESIGN.md)

## 1. Delivery Strategy

Phase 1 should be delivered as a bounded architectural migration, not as a full
renderer rewrite.

Guiding principles:

- preserve the production runtime path
- introduce new non-runtime facades without widening runtime APIs
- replace architectural bottlenecks in slices
- keep migration reversible where possible

## 1.1 Parallelization and Shippable States

This plan should not be read as a strict waterfall even though some
dependencies are real.

Practical parallelization:

- after capability vocabulary is defined, facade work and composition-queue
  work may proceed in parallel
- publication refactor and runtime ownership migration are logically distinct
  enough to advance in separate lanes once the architectural substrate is fixed

Intermediate shippable states should exist after:

- M2: harness facades
- M3: offscreen scene facade
- M5: queued composition

The package should avoid a “big bang” renderer rewrite.

## 2. Recommended Implementation Order

## 2.1 Foundation

1. Establish the phase-1 capability-family vocabulary in code.
2. Add pipeline capability declaration (`required` / `optional`) and bind-time
   validation.
3. Introduce the phase-1 renderer-side submission queue conceptually and in
   code structure.

## 2.2 Non-Runtime Facades

1. Implement `ForSinglePassHarness()`.
2. Implement `ForRenderGraphHarness()`.
3. Implement `ForOffscreenScene()`.
4. Add helper/preset layers only after the base facades are correct.
5. Define the migration relationship to existing `OffscreenFrameSession`.

## 2.3 Publication Refactor

1. Split baseline publication from optional-family publication.
2. Move toward family-owned source data + renderer-owned publication substrate.
3. Avoid rewriting every publication edge at once.

## 2.4 Runtime Ownership Migration

1. Move runtime view ownership toward renderer-owned canonical state.
2. Keep pipeline on the “intent/policy” side of the bridge.
3. Reduce reliance on pipeline-owned runtime view state as migration proceeds.

## 2.4.1 ForwardPipeline Coupling Inventory

As part of runtime ownership migration, inventory current direct
`ForwardPipeline -> Renderer` calls and classify each one as:

- acceptable permanent concrete dependency
- should move to capability-owned access
- should move to a bridged renderer API

This migration work is required to make the ownership split executable rather
than merely aspirational.

## 2.5 Composition Migration

1. Replace the scalar pending composition slot with a queued model.
2. Preserve `CompositionSubmission` as the sole final handoff.
3. Keep phase 1 to multi-submission, single-target execution.

## 3. Dependency Map

High-level dependency order:

1. capability vocabulary and validation
2. non-runtime facade legality rules
3. publication split
4. composition queue
5. runtime view ownership migration

Why:

- the facades need a stable baseline vocabulary
- publication and composition shape influence `RenderContext` materialization
- runtime ownership migration is safer once those stable substrates exist

## 3.1 Rollback and Coexistence Strategy

Because phase 1 is an architectural migration, coexistence rules must be
explicit.

Publication coexistence:

- baseline-vs-optional publication split may coexist behind adapter layers or
  guarded routing while the migration is incomplete
- the architecture should not require all publishers to move in one patch

Composition coexistence:

- preserve `CompositionSubmission`
- change renderer-side storage from scalar slot to queue without changing the
  public final handoff

Offscreen coexistence:

- existing `OffscreenFrameSession` remains valid transitional implementation
  substrate while the new facades are introduced

## 4. Milestones

## M1. Capability and Contract Baseline

Exit criteria:

- capability-family model exists in code shape
- pipelines can declare `required` / `optional`
- bind-time capability validation exists conceptually/in code

## M2. Harness Facade Baseline

Exit criteria:

- `ForSinglePassHarness()` exists
- `ForRenderGraphHarness()` exists
- both enforce separate staging + finalize semantics

## M3. Offscreen Scene Facade

Exit criteria:

- `ForOffscreenScene()` exists
- it owns scene/view/output/pipeline derivation work
- it does not collapse into harness semantics

## M4. Publication Split

Exit criteria:

- baseline publication is separated from optional family publication
- the design no longer depends architecturally on one monolithic publication hub

## M5. Queued Composition

Exit criteria:

- renderer supports queued multi-submission, single-target composition
- `CompositionSubmission` remains the only final handoff

## M6. Runtime View Ownership Alignment

Exit criteria:

- canonical runtime view state is renderer-owned by architecture and code shape
- pipeline remains on the intent/policy side of the bridge

## 5. Verification Plan

Architectural verification:

- confirm the three non-runtime facades exist and are semantically distinct
- confirm `RenderContext` is only executable after legal finalization/runtime
  setup
- confirm pipelines own pass instances/default configs
- confirm renderer owns reusable capabilities and canonical runtime state
- confirm composition uses the queued phase-1 subset

Scenario verification:

- one pass executes through `ForSinglePassHarness()`
- one caller graph executes through `ForRenderGraphHarness()`
- one scene renders through `ForOffscreenScene()`
- full runtime still supports a production-capable assembly

## 5.1 Simulation Findings Summary

The architecture simulation exposed four implementation-hostile areas that phase
1 must close explicitly:

1. publication refactor sequencing
2. queued composition submission shape
3. runtime view-lifetime ownership migration
4. facade helper strategy

These are no longer blank architectural problems, but they remain critical
implementation and migration checkpoints.

### 5.1.1 `ForSinglePassHarness()` Findings

What works:

- the facade boundary is semantically clear
- the minimal baseline (`Renderer Core`) is sufficient to host realistic
  shader-backed passes in principle
- pass-specific requirements remain correctly outside the facade

What the simulation exposed:

1. **Pass readiness is still highly uneven**
   Some passes only need output target + baseline shader inputs.
   Others additionally need `ResolvedView`, `PreparedFrame`, or specific
   renderer-owned feature families.
   This is acceptable architecturally, but it means harness ergonomics will vary
   widely by pass unless helper presets are later added.

2. **Resource-state tracking responsibility needs to stay explicit**
   Some passes assume externally seeded tracking for certain resources.
   The single-pass harness should guarantee the common baseline around the
   primary output target and context, but it should not pretend that every
   pass-specific external resource can be inferred automatically.

3. **The facade must own context freshness**
   To avoid stale pass registrations or stale view state leaking between runs,
   the harness must finalize into a fresh/clean execution context every time.

Conclusion: keep the pass harness pass-agnostic, keep baseline context validity
strict, accept that pass-specific setup remains a higher layer than the facade.

### 5.1.2 `ForRenderGraphHarness()` Findings

What works:

- it is semantically distinct from the single-pass harness
- it aligns naturally with Oxygen's "render graph is a coroutine" model

What the simulation exposed:

1. **Pass registry lifecycle must be explicit**
   Because graph execution may depend on cross-pass registration/lookup, the
   harness must own the lifecycle of registered-pass state and start each run
   from a clean state.

2. **This facade should stay low-level**
   If it starts owning scene prep, view registration, or pipeline planning, it
   collapses into either `ForOffscreenScene()` or a surrogate runtime path.

3. **Graph correctness remains graph-owned**
   The facade can validate only the baseline executable context.
   It cannot validate every semantic dependency inside an arbitrary caller graph
   without ceasing to be low-level.

Conclusion: keep graph harness low-level and explicit; treat it as "validated
context + caller-authored graph execution", not as a mini runtime.

### 5.1.3 `ForOffscreenScene()` Findings

What works:

- its semantic boundary is meaningfully higher-level than the two harness
  facades
- it is the natural place to own scene prep, view resolution, and chosen
  pipeline execution for an offscreen scene render

What the simulation exposed:

1. **Its boundary must stay scene-oriented**
   If it exposes raw prepared-frame or raw shader-input overrides at the base
   API, it stops being a scene-rendering facade and becomes another harness.

2. **Pipeline selection can remain optional**
   A default offscreen-capable pipeline is reasonable, but the architecture must
   allow explicit pipeline selection when needed.

3. **Final composition must remain scenario-dependent**
   Offscreen scene rendering should not be forced through full runtime
   presentation/composition machinery when the output target is already the
   intended final destination.

Conclusion: `ForOffscreenScene()` should own derivation work, stay higher-level
than the harness facades, and not inherit low-level override vocabulary.

### 5.1.4 Full Runtime Findings

What works:

- the ownership split now clearly says pipelines own policy and pass sets, while
  renderer owns reusable services and canonical runtime state

What the simulation exposed:

1. **Current implementation still leaks view lifetime into the pipeline layer**
   The architecture says views are bridged, with renderer owning canonical
   runtime state. The current `ViewLifecycleService` ownership shape is an
   implementation gap against the intended architecture.

2. **Current scalar composition storage is architecturally insufficient**
   Phase 1 now expects queued multi-submission, single-target submission.
   The current single pending submission/surface slot is a real bottleneck.

3. **The monolithic publication path is the most obvious implementation debt**
   The architecture prefers baseline publication separated from optional
   feature-family publication, but the implementation still centralizes too much
   in one renderer path.

Conclusion: the runtime path is architecturally coherent; the remaining problems
are implementation-shape debt, not missing architectural direction.

### 5.1.5 Summary Outcomes

- pass-harness readiness varies significantly by pass; baseline legality does
  not imply every pass is runnable without additional staged data or helpers
- graph-harness correctness beyond baseline context validity remains graph-author
  owned
- offscreen scene must stay higher-level than the harness facades or it will
  collapse into another low-level API
- runtime still carries implementation debt around view-lifetime ownership and
  composition queuing even though the architecture is now shaped

## 6. Risk Areas

1. Publication refactor sequencing
2. queued composition migration
3. runtime view ownership migration
4. helper ergonomics drifting into core facade scope
5. duplicated GPU-heavy internals across separate scenario renderer instances

## 6.1 Remaining Follow-Up Questions

These are not blockers for the phase-1 architecture, but they are legitimate
follow-up items for later design or implementation work:

1. Are there any phase-1 capability-family merges/splits still missing after the
   current consolidated model?
2. What implementation slice should replace the remaining edges of the current
   monolithic publication path?
3. Are there any real edge cases that justify growing the phase-1 capability
   descriptor beyond `required` and `optional`?
4. What implementation constraints, if any, would justify shared internals
   across separate scenario renderer instances?
5. What concrete production use case, if any, should justify inter-view
   dependencies beyond exposure sharing and composition ordering?
6. What should become the first architectural target beyond phase 1?

## 7. Mitigations

1. Refactor publication in two slices only:
   baseline then optional families.
2. Preserve `CompositionSubmission` while changing renderer-side queue shape.
3. Treat pipeline-owned runtime view state as transitional implementation debt.
4. Keep helpers outside the base facades.
5. Keep `ForOffscreenScene()` semantically higher-level than the harness
   facades.
6. Keep the runtime/editor multi-view target visible even if phase-1
   implementation uses a narrower composition subset.
7. Treat “one renderer instance per scenario” as the conceptual model while
   allowing internal backend/resource sharing if GPU cost proves too high.

## 8. Explicit Deferrals

Deferred beyond phase 1:

- task-targeted global compositor
- richer final-frame contracts beyond `CompositionSubmission`
- public inter-view dependency DAGs
- shared internals across scenario renderer instances as a public model
- capability-descriptor growth beyond `required` / `optional` without a real
  production blocker

## 9. Phase-1 Exit Criteria

Phase 1 is complete when:

1. the architecture in [ARCHITECTURE.md](./ARCHITECTURE.md) is reflected in the
   code shape well enough to prevent ownership drift
2. the concrete design contracts in [DESIGN.md](./DESIGN.md) are present for
   the three non-runtime facades
3. the runtime path remains narrow and production-shaped
4. composition no longer has a scalar architectural bottleneck
5. the remaining open work is clearly phase-2+ evolution, not unresolved
   phase-1 architecture
