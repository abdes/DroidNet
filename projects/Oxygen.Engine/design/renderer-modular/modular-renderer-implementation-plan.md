# Modular Renderer Plan

**Date:** 2026-04-13
**Status:** Design / Implementation Plan

This document captures the implementation and migration plan for the modular
renderer. It assumes the stable architecture in
[ARCHITECTURE.md](modular-renderer-architecture.md) and the concrete solution shape in
[DESIGN.md](modular-renderer-design.md).

Related:

- [PRD.md](modular-renderer-prd.md)
- [ARCHITECTURE.md](modular-renderer-architecture.md)
- [DESIGN.md](modular-renderer-design.md)

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

These are not blockers for phase-2 architecture, but they are legitimate
follow-up items for later design or implementation work:

1. ~~Are there any phase-1 capability-family merges/splits still missing?~~
   Resolved: no changes needed.
2. ~~What implementation slice should replace the remaining edges of the current
   monolithic publication path?~~
   Answered: phase 2 starts with Environment Lighting publication extraction.
   Subsequent families follow the same pattern.
3. Are there any real edge cases that justify growing the capability
   descriptor beyond `required` and `optional`?
4. What implementation constraints, if any, would justify shared internals
   across separate scenario renderer instances?
5. What concrete production use case, if any, should justify inter-view
   dependencies beyond exposure sharing and composition ordering?
6. ~~What should become the first architectural target beyond phase 1?~~
   Answered: Environment Lighting service extraction.

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

Deferred beyond phase 2:

- task-targeted global compositor
- richer final-frame contracts beyond `CompositionSubmission`
- public inter-view dependency DAGs
- shared internals across scenario renderer instances as a public model
- capability-descriptor growth beyond `required` / `optional` without a real
  production blocker
- extraction of families other than Environment Lighting (Diagnostics,
  Shadowing, Lighting Data)

## 9. Phase-1 Exit Criteria (met)

Phase 1 is complete. See [IMPLEMENTATION-STATUS.md](modular-renderer-implementation-status.md)
for proof artifacts.

---

## 10. Phase-2 Delivery Strategy

Phase 2 is a focused structural refactor: extract the `Environment Lighting`
capability family from `Renderer` into a standalone
`EnvironmentLightingService`. No rendering behavior changes. The production
runtime path, all facades, and all tests remain green throughout.

Guiding principles:

- move code mechanically — cut from `Renderer`, paste into the service,
  adjust access patterns
- keep each slice independently buildable and testable
- validate after every slice: full build + targeted tests + one runtime
  example
- if a slice breaks something, fix it before moving to the next slice
- do not widen scope by removing stable renderer bridge methods unless that
  change is already required for the extraction

## 11. Phase-2 Implementation Slices

The extraction is split into six ordered slices. Each slice is a single
committable unit.

### Slice 2.1: Create the service skeleton

Create empty `EnvironmentLightingService` class files:

- `src/Oxygen/Renderer/Environment/EnvironmentLightingService.h`
- `src/Oxygen/Renderer/Environment/EnvironmentLightingService.cpp`

The class should have:

- constructor taking the dependencies it needs (see DESIGN.md §11.5)
- empty `Initialize(...)`, `OnFrameStart(...)`, `UpdatePreparedViewState(...)`,
  `PrepareCurrentView(...)`, `ExecutePerViewPasses(...)`,
  `PublishForView(...)`, `NoteViewSeen(...)`, `EvictViewProducts(ViewId)`,
  `EvictInactiveViewProducts(...)`, `Shutdown()` methods
- a destructor

Add the new files to the CMake target for `oxygen-renderer`.

Add a `unique_ptr<EnvironmentLightingService> env_lighting_service_` member
to `Renderer`. Do not instantiate it yet.

**Exit gate:** full build passes, no test regressions.

### Slice 2.2: Move owned state, initialization, and frame-start lifecycle

Move these member variables from `Renderer` to the service:

- `environment_view_data_publisher_`
- `environment_frame_bindings_publisher_`
- `brdf_lut_manager_`
- `ibl_manager_`
- `env_static_manager_`
- `sky_capture_pass_`, `sky_capture_pass_config_`
- `sky_atmo_lut_compute_pass_`, `sky_atmo_lut_compute_pass_config_`
- `ibl_compute_pass_`
- `per_view_atmo_luts_`
- `last_atmo_generation_`, `last_seen_view_frame_seq_`
- `sky_capture_requested_`
- `atmosphere_blue_noise_enabled_`, `atmosphere_debug_flags_`

Do **not** move `last_scene_identity_`. It currently resets shadow cache state
and is not part of the environment family.

Move the initialization code from `Renderer::OnAttached` into
`EnvironmentLightingService::Initialize(...)`. In `Renderer::OnAttached`,
replace that code with:

- if `HasCapability(kEnvironmentLighting)`: create
  `EnvironmentLightingService` and call `Initialize(...)`

Move the environment-specific frame-start work from `BeginFrameServices`
into `EnvironmentLightingService::OnFrameStart(...)`. This includes:

- `environment_view_data_publisher_->OnFrameStart(...)`
- `environment_frame_bindings_publisher_->OnFrameStart(...)`
- `env_static_manager_->OnFrameStart(...)`
- blue-noise toggle application to `env_static_manager_`

Move the shutdown code from `Renderer::OnShutdown` into
`EnvironmentLightingService::Shutdown()`. In `Renderer::OnShutdown`, call
`env_lighting_service_->Shutdown()` before resetting the pointer.

Keep the existing public environment bridge methods on `Renderer`, but switch
them to forwards into the service as soon as the owned state moves:

- `GetEnvironmentStaticDataManager()`
- `GetIblManager()`
- `GetIblComputePass()`
- `GetSkyAtmosphereLutManagerForView(...)`
- `RequestSkyCapture()`
- `RequestIblRegeneration()`
- `SetAtmosphereBlueNoiseEnabled(...)`

This prevents pass/pipeline churn while the implementation ownership moves.

**Exit gate:** full build passes, no test regressions.

### Slice 2.3: Move scene-prep and current-view environment wiring

Move the environment-specific block from `RunScenePrep(...)` into
`EnvironmentLightingService::UpdatePreparedViewState(...)`. This block:

- populates `runtime_state.environment_view`
- creates/updates the per-view atmosphere LUT manager
- updates LUT sun state and atmosphere parameters from scene environment

Move the environment-specific block from
`PrepareAndWireViewConstantsForView(...)` into
`EnvironmentLightingService::PrepareCurrentView(...)`. This block:

- sets `render_context.current_view.atmo_lut_manager`
- synchronizes `EnvironmentStaticDataManager` for the active view
- erases environment static state when atmosphere is disabled for the view

These moves are mandatory before the renderer can be considered free of
environment helper logic.

**Exit gate:** full build passes,
`Oxygen.Renderer.OffscreenSceneFacade.Tests` passes.

### Slice 2.4: Move per-view execution orchestration

Move the environment pass **execution orchestration** from
`Renderer::OnRender` STEP 2. This block sequences atmosphere LUT compute →
sky capture → IBL compute with intermediate `EnvironmentStaticData` updates.
Move this into `EnvironmentLightingService::ExecutePerViewPasses(ViewId,
RenderContext&, CommandRecorder&)`. In `Renderer::OnRender`, replace
STEP 2 with:

```cpp
if (env_lighting_service_) {
  env_lighting_service_->ExecutePerViewPasses(view_id, render_context, recorder);
}
```

This slice also moves the environment-specific view-seen bookkeeping call
currently performed in `OnRender`:

- `last_seen_view_frame_seq_[view_id] = context->GetFrameSequenceNumber();`

**Exit gate:** full build passes, `RenderScene` example runs.

### Slice 2.5: Move publication and eviction logic

In `Renderer::PublishOptionalFamilyViewBindings`, find the block that
publishes `EnvironmentViewData` and `EnvironmentFrameBindings`. Move that
code into `EnvironmentLightingService::PublishForView(...)`. Replace the
original block with a delegation call:

```cpp
if (env_lighting_service_) {
  env_lighting_service_->PublishForView(view_id, ...);
}
```

The publisher references (`environment_view_data_publisher_`,
`environment_frame_bindings_publisher_`) move into the service in slice 2.2,
so publication delegation must use the service-owned publishers.

Move these methods from `Renderer` into the service:

- `GetOrCreateSkyAtmosphereLutManagerForView(ViewId)` — becomes a service
  internal method
- the environment portion of `EvictPerViewCachedProducts(ViewId)` — becomes
  `EnvironmentLightingService::EvictViewProducts(ViewId)`
- the environment-only inactivity window logic in `EvictInactivePerViewState`
  — becomes `EnvironmentLightingService::EvictInactiveViewProducts(...)`

In `Renderer::EvictPerViewCachedProducts`, replace the environment eviction
code with a delegation call to `env_lighting_service_->EvictViewProducts(...)`.

In the inactive-view cleanup path, replace the environment-specific TTL walk
with a delegation call to the service.

**Exit gate:** full build, `Oxygen.Renderer.RendererPublicationSplit.Tests`
passes, `MultiView` example runs.

### Slice 2.6: Cleanup, documentation, and focused proof

There are currently no environment-specific console command registrations in
`Renderer::RegisterConsoleBindings`. Do not add new console surface just to
match the extraction pattern. Keep any future environment console/debug
bindings service-local if they are introduced later.

Update the public `Renderer` API:

- keep the existing environment bridge methods as one-line forwards
- optionally add `GetEnvironmentLightingService()` if it helps future
  extractions, but it is not required to complete phase 2

Add focused proof for the extraction. Minimum requirement:

- extend `RendererPublicationSplit.Tests` with capability-present environment
  delegation/publication coverage
- keep `OffscreenSceneFacade.Tests`
  `ExecuteDefaultForwardPipelineDegradesCoherentlyWithoutOptionalFamilies`
  green as the absent-capability gate
- cover environment cache eviction through either a focused renderer test or a
  new dedicated extraction suite

Verify that no environment member variables or private helper methods remain
in `Renderer.h` or `Renderer.cpp`. Search for:

- `environment_view_data_publisher`, `environment_frame_bindings_publisher`,
  `brdf_lut`, `ibl_`, `sky_capture`, `sky_atmo`, `atmo_lut`,
  `env_static`, `atmosphere_`, `last_atmo`, `last_seen_view_frame_seq`

If any remain, move them or confirm they are pure service forwards.

Update [IMPLEMENTATION-STATUS.md](modular-renderer-implementation-status.md) with:

- slice-by-slice completion record
- file list
- test results

Verify the absent-capability gate: run an offscreen harness or facade test
that does not include `kEnvironmentLighting` and confirm it still works.

**Exit gate:** full build, focused extraction tests, runtime smokes,
absent-capability confirmation, IMPLEMENTATION-STATUS.md updated.

## 12. Phase-2 Dependency Map

Slices are strictly sequential:

1. **Slice 2.1** (skeleton) has no dependencies
2. **Slice 2.2** (owned state + init + frame-start + forwards) depends on 2.1
3. **Slice 2.3** (scene-prep + current-view wiring) depends on 2.2
4. **Slice 2.4** (per-view execution orchestration) depends on 2.2
5. **Slice 2.5** (publication + eviction) depends on 2.2 and 2.3
6. **Slice 2.6** (cleanup + proof) depends on all previous slices

## 13. Phase-2 Milestones

### M7. Service Skeleton

Exit criteria:

- `EnvironmentLightingService` class exists with lifecycle methods
- `Renderer` holds a `unique_ptr` to it
- full build passes

### M8. State and Lifecycle Extraction

Exit criteria:

- all environment member variables live in the service
- initialization, frame-start lifecycle, and shutdown are delegated
- scene-prep/current-view environment hooks are delegated
- all environment publication and eviction are delegated
- production runtime examples run without regression

### M9. Public API and Cleanup

Exit criteria:

- `Renderer.h` has no environment-specific member variables
- any retained environment bridge methods are thin forwards only
- absent-capability scenarios confirmed working
- IMPLEMENTATION-STATUS.md updated

## 14. Phase-2 Verification Plan

Architectural verification:

- confirm `EnvironmentLightingService` owns all environment state
- confirm `Renderer` holds only a `unique_ptr` for the family
- confirm the service does not hold a back-pointer to `Renderer`
- confirm frame-start lifecycle for environment publishers/static data is
  service-managed
- confirm scene-prep/current-view environment logic is service-managed
- confirm publication delegates cleanly
- confirm per-view state is service-managed

Behavioral verification:

- full build passes
- focused extraction tests pass
- `Oxygen.Examples.RenderScene.exe` runs and renders environment
  (atmosphere, sky, IBL) correctly
- `Oxygen.Examples.MultiView.exe` runs with two views and per-view
  atmosphere LUT management
- `Oxygen.Examples.DemoShell.exe` runs if applicable
- an absent-capability scenario (e.g., offscreen harness without
  `kEnvironmentLighting`) runs without crash

## 15. Phase-2 Exit Criteria

Phase 2 is complete when:

1. `EnvironmentLightingService` exists and owns all environment-lighting
   state, initialization, per-view management, publication, and shutdown.
2. `Renderer` delegates all environment work to the service.
3. The service is instantiated only when `kEnvironmentLighting` is present.
4. All tests and runtime examples pass.
5. The extraction pattern is documented in DESIGN.md §12.
6. IMPLEMENTATION-STATUS.md records the phase-2 slices, files changed, and
   test results.
