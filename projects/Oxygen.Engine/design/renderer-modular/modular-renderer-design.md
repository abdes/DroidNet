# Modular Renderer Design

**Date:** 2026-04-13
**Status:** Design / Specification

This document captures the concrete chosen solution shapes:
facades, contracts, validation rules, capability declarations, composition
behavior, migration-oriented design decisions, and the phase-2
capability-family service extraction design. It assumes the stable
conceptual model defined in [ARCHITECTURE.md](modular-renderer-architecture.md).

Related:

- [PRD.md](modular-renderer-prd.md)
- [ARCHITECTURE.md](modular-renderer-architecture.md)
- [PLAN.md](modular-renderer-implementation-plan.md)

## 1. Phase-1 Design Summary

Phase 1 organizes the modular renderer around:

- baseline renderer substrate (`Renderer Core`)
- scenario-specific renderer assemblies
- pipeline-owned pass sets and graph construction
- renderer-owned reusable capabilities
- three non-runtime facades with distinct semantic boundaries

## 1.1 Scenario Factory Mental Model

The design should still be read through the scenario-factory mental model that
shaped the original architecture work:

- scenario factories assemble renderers structurally
- pipelines are selected/configured on top of that structural assembly
- render graphs and passes then execute against the resulting substrate

For phase 1, the meaningful scenario-facing factories are:

- full runtime renderer assembly
- `ForSinglePassHarness()`
- `ForRenderGraphHarness()`
- `ForOffscreenScene()`
- feature-selected runtime variants

This mental model is important because it explains why:

- capability families are runtime-primary at the coarse level
- facades are scenario-specific rather than universal
- pipelines are consumers of capabilities, not owners of renderer substrate

Important design note:

- the facade contracts below are **first-cut phase-1 design shapes**
- their semantic boundaries are authoritative
- their exact staged method sets are still allowed to evolve during phase-1
  implementation as long as those semantic boundaries remain intact

## 2. Pipeline Capability Declaration

Phase-1 pipeline capability declaration stays intentionally small:

```cpp
enum class RendererCapabilityFamily : std::uint32_t {
  kScenePreparation,
  kGpuUploadAndAssetBinding,
  kLightingData,
  kShadowing,
  kEnvironmentLighting,
  kFinalOutputComposition,
  kDiagnosticsAndProfiling,
};

struct PipelineCapabilityRequirements {
  CapabilitySet required;
  CapabilitySet optional;
};

class RenderingPipeline {
public:
  [[nodiscard]] virtual auto GetCapabilityRequirements() const
    -> PipelineCapabilityRequirements = 0;
};
```

Rules:

- `Renderer Core` is baseline and is not part of the descriptor
- capability validation happens at assembly/bind time
- missing required families are hard errors
- missing optional families must degrade coherently

## 2.1 Existing Pipeline Feature Vocabulary

`RendererCapabilityFamily` does not replace the existing `PipelineFeature`
vocabulary.

They operate at different levels:

- `RendererCapabilityFamily`
  structural/runtime availability of coarse renderer families
- `PipelineFeature`
  pipeline discovery/advertised rendering feature flags

Phase-1 rule:

- `GetCapabilityRequirements()` and `GetSupportedFeatures()` coexist
- the former answers "can this pipeline legally run on this renderer assembly?"
- the latter answers "what rendering features does this pipeline advertise?"

## 2.2 Minimum Alternate-Pipeline Contract

An alternate pipeline is considered valid only if it satisfies this minimum
contract:

1. **Capability declaration**
   It declares required and optional capability families.
2. **Policy ownership**
   It owns its pass set, pass instances, default pass configs, staged settings,
   and graph-construction logic.
3. **Deterministic phase behavior**
   It applies staged settings deterministically at intended phase boundaries.
4. **View intent production**
   If the scenario is view-based, it can describe or publish desired views
   through bridged renderer-owned APIs.
5. **Graph registration**
   It can register the graph work it wants executed, or explicitly choose not
   to.
6. **Final handoff**
   If the scenario uses composition, it can produce a valid
   `CompositionSubmission`-style payload.
7. **Boundary discipline**
   It does not directly own canonical renderer runtime state such as
   `RenderContext` lifecycle, runtime `ViewId` storage, or late composition
   execution.
8. **Optional capability tolerance**
   It degrades coherently when optional capability families are absent.

What this does not require:

- that every pipeline use scene preparation
- that every pipeline produce composition work
- that every pipeline support every scenario
- that every pipeline hide direct dependency on concrete renderer-owned services

## 3. RenderContext Materialization Strategy

The package must be explicit about the hardest technical point:
`RenderContext` is the authoritative execution context, but in the current code
it is still an open public struct.

Phase-1 interpretation:

- phase 1 does **not** make the raw `RenderContext` type physically sealed or
  immutable
- phase 1 does require that only **legal producers** create contexts that are
  considered executable
- those legal producers are:
  - the normal production runtime path
  - non-runtime facades after successful `Finalize()`

So "authoritative" and "valid" mean:

- passes execute only against a context produced by a legal path
- not that the raw struct is impossible to mutate in the implementation

## 3.1 RenderContext Materializer

Facade finalization needs a concrete mechanism.

Phase-1 design therefore assumes a renderer-owned materialization service.

Conceptual shape:

```cpp
class RenderContextMaterializer {
public:
  auto MaterializeFromSinglePass(const SinglePassHarnessStaging&)
    -> Result<ValidatedHarnessContext, ValidationReport>;
  auto MaterializeFromRenderGraph(const RenderGraphHarnessStaging&)
    -> Result<ValidatedRenderGraphHarness, ValidationReport>;
  auto MaterializeFromOffscreenScene(const OffscreenSceneStaging&)
    -> Result<ValidatedOffscreenSceneSession, ValidationReport>;
};
```

Responsibilities:

- perform the `WireContext`-equivalent population
- set renderer and graphics handles legally
- establish frame/session identity
- derive or apply baseline shader-input publication
- start from clean pass-registry/view-state conditions
- return executable artifacts only after validation succeeds

This is the phase-1 answer to the "how does `Finalize()` actually create a
usable `RenderContext`?" problem.

## 4. Phase-1 Non-Runtime Facades

## 4.1 `ForSinglePassHarness()`

Purpose:

- host exactly one pass-oriented execution surface
- remain pass-agnostic

Illustrative facade shape:

```cpp
class SinglePassHarnessFacade {
public:
  auto SetFrameSession(FrameSessionInput session) -> SinglePassHarnessFacade&;
  auto SetOutputTarget(OutputTargetInput target) -> SinglePassHarnessFacade&;
  auto SetResolvedView(ResolvedViewInput view) -> SinglePassHarnessFacade&;
  auto SetPreparedFrame(PreparedFrameInput frame) -> SinglePassHarnessFacade&;
  auto SetCoreShaderInputs(CoreShaderInputsInput inputs)
    -> SinglePassHarnessFacade&;

  [[nodiscard]] auto CanFinalize() const -> bool;
  auto Validate() const -> ValidationReport;
  auto Finalize() -> ValidatedHarnessContext;
};
```

Rules:

- separate staging state
- incremental setup
- explicit validate/finalize
- finalized context only
- `Finalize()` derives core shader inputs when possible
- explicit shader-input override exists for tests
- pass-specific legality remains pass-owned

Method intent:

- `SetFrameSession(...)`
  Supplies frame slot, frame sequence, delta time, and the minimal
  execution-lifetime data required to make pass execution legal.
- `SetOutputTarget(...)`
  Supplies the framebuffer/render target the pass will execute against.
- `SetResolvedView(...)`
  Supplies active view/camera state when the pass needs view-dependent shader
  execution.
- `SetPreparedFrame(...)`
  Supplies finalized per-view prepared data when the pass depends on draw
  metadata, transforms, or other prepared-frame products.
- `SetCoreShaderInputs(...)`
  Supplies explicit overrides for the core shader-visible routing when a test or
  harness needs direct deterministic control of those values.

Baseline requirements:

- frame session
- output target
- satisfiable core shader inputs

## 4.2 `ForRenderGraphHarness()`

Purpose:

- host exactly one caller-authored render-graph coroutine
- stay lower-level than scene rendering

Illustrative facade shape:

```cpp
class RenderGraphHarnessFacade {
public:
  auto SetFrameSession(FrameSessionInput session) -> RenderGraphHarnessFacade&;
  auto SetOutputTarget(OutputTargetInput target) -> RenderGraphHarnessFacade&;
  auto SetResolvedView(ResolvedViewInput view) -> RenderGraphHarnessFacade&;
  auto SetPreparedFrame(PreparedFrameInput frame) -> RenderGraphHarnessFacade&;
  auto SetCoreShaderInputs(CoreShaderInputsInput inputs)
    -> RenderGraphHarnessFacade&;
  auto SetRenderGraph(RenderGraphHarnessInput graph)
    -> RenderGraphHarnessFacade&;

  [[nodiscard]] auto CanFinalize() const -> bool;
  auto Validate() const -> ValidationReport;
  auto Finalize() -> ValidatedRenderGraphHarness;
};
```

Rules:

- validated graph-executable context
- explicit caller-supplied graph
- no scene prep ownership
- no pipeline planning ownership
- no runtime orchestration ownership
- it validates only baseline executable-context legality
- it does **not** validate arbitrary graph semantics
- correctness of the supplied graph remains graph-author owned

Method intent:

- `SetFrameSession(...)`
  Supplies frame/session identity and execution-lifetime data.
- `SetOutputTarget(...)`
  Supplies the render target the graph will execute against.
- `SetResolvedView(...)`
  Supplies view-dependent state when the graph or its passes need it.
- `SetPreparedFrame(...)`
  Supplies prepared per-view render data when the graph or its passes need it.
- `SetCoreShaderInputs(...)`
  Supplies explicit overrides when the graph harness should not rely on
  automatically derived core shader inputs.
- `SetRenderGraph(...)`
  Supplies the caller-authored render-graph coroutine that this harness exists
  to execute.

## 4.3 `ForOffscreenScene()`

Purpose:

- render scene-derived content offscreen
- own the higher-level derivation work that harnesses do not own

Illustrative facade shape:

```cpp
class OffscreenSceneFacade {
public:
  auto SetFrameSession(FrameSessionInput session) -> OffscreenSceneFacade&;
  auto SetSceneSource(SceneSourceInput scene) -> OffscreenSceneFacade&;
  auto SetViewIntent(OffscreenSceneViewInput view) -> OffscreenSceneFacade&;
  auto SetOutputTarget(OutputTargetInput target) -> OffscreenSceneFacade&;
  auto SetPipeline(OffscreenPipelineInput pipeline) -> OffscreenSceneFacade&;

  [[nodiscard]] auto CanFinalize() const -> bool;
  auto Validate() const -> ValidationReport;
  auto Finalize() -> ValidatedOffscreenSceneSession;
};
```

Rules:

- owns view resolution
- owns scene preparation use
- owns prepared-frame derivation
- owns derivation/publication of core shader inputs
- does not expose raw prepared-frame or raw shader-input overrides at the base
  semantic level

Method intent:

- `SetFrameSession(...)`
  Supplies frame slot, sequence number, delta time, and related session
  identity.
- `SetSceneSource(...)`
  Supplies the scene authority that the offscreen renderer will prepare and
  render.
- `SetViewIntent(...)`
  Supplies the high-level view request for the offscreen scene.
- `SetOutputTarget(...)`
  Supplies the render target for the offscreen result.
- `SetPipeline(...)`
  Selects the pipeline/policy for the offscreen scene render, defaulting to a
  reasonable offscreen-capable pipeline when omitted.

Phase-1 default:

- if omitted, `ForOffscreenScene()` defaults to `ForwardPipeline`
  with an offscreen-capable policy profile
- phase 1 does not require a second dedicated offscreen pipeline to exist

## 4.4 Shared Facade Legality Rules

All phase-1 facades share these rules:

1. staging state is separate from authoritative `RenderContext`
2. `Validate()` / `Finalize()` are the only legality gates
3. helpers/presets never bypass legality
4. only finalized output is executable
5. semantic boundaries remain strict
6. facade legality is baseline execution-context legality, not arbitrary
   pass/graph semantic correctness

## 4.4.1 Non-Runtime Injection Without Runtime Pollution

The non-runtime design should preserve a strong separation from the production
runtime path:

- `RenderContext` remains authoritative
- staging state is separate from `RenderContext`
- runtime is still the narrow/default path by which `RenderContext` becomes
  valid in production
- non-runtime facades provide legal alternative setup paths without widening the
  production surface

This is why:

- helpers stay outside the base facades
- no generic typed-slot injection facade is introduced
- no facade may execute directly against in-progress staging state

## 4.4.2 Facade Error Model

Phase-1 facades need an explicit error model.

Recommended rule:

- `CanFinalize()`
  cheap readiness hint only
- `Validate()`
  returns a structured `ValidationReport`
- `Finalize()`
  returns `Result<T, ValidationReport>` / equivalent result type for expected
  validation failures rather than throwing

Working semantics:

- `CanFinalize()` may return true and `Finalize()` may still fail if final
  resource/materialization validation fails
- expected validation failures should not be modeled as exceptions

Minimal shape:

```cpp
struct ValidationIssue {
  std::string code;
  std::string message;
};

struct ValidationReport {
  std::vector<ValidationIssue> issues;
  [[nodiscard]] auto Ok() const -> bool { return issues.empty(); }
};
```

## 4.4.3 Shared Internal Base and Reuse

The three facades are semantically distinct in public API, but that does not
mean they must be implemented from scratch independently.

Phase-1 rule:

- semantic distinction is architectural
- shared internal staging/materialization helpers are allowed and recommended
- public facade names and authority boundaries remain separate even if
  implementation shares code

## 4.4.4 Relationship to Existing `OffscreenFrameSession`

The design must explicitly account for the already-existing offscreen execution
mechanism in the codebase.

Phase-1 rule:

- `Renderer::BeginOffscreenFrame()` / `OffscreenFrameSession` is transitional
  implementation substrate, not ignored legacy
- the new non-runtime facades should either wrap it or replace it in a staged
  migration
- current tests using `BeginOffscreenFrame()` must not be stranded by the new
  design

## 4.5 Phase-1 Scenario-to-Family Footprint

This is the phase-1 scenario-to-capability commitment that was established in
the working architecture draft and must be preserved here.

| Scenario facade | Required phase-1 families | Optional/additional families |
| --- | --- | --- |
| `ForSinglePassHarness()` | `Renderer Core` | depends on the pass under test; may need none beyond baseline |
| `ForRenderGraphHarness()` | `Renderer Core` | depends on the supplied graph; no extra family is inherently mandatory |
| `ForOffscreenScene()` | `Renderer Core`, `Scene Preparation`, `GPU Upload and Asset Binding` | `Lighting Data`, `Shadowing`, `Environment Lighting`, `Final Output Composition`, `Diagnostics and Profiling` |
| Full runtime | `Renderer Core`, `View Registration and Lifetime`, `GPU Upload and Asset Binding`, `Scene Preparation`, `Final Output Composition` | `Lighting Data`, `Shadowing`, `Environment Lighting`, `Diagnostics and Profiling` |
| Editor multi-view / multi-surface runtime | `Renderer Core`, `View Registration and Lifetime`, `GPU Upload and Asset Binding`, `Scene Preparation`, `Final Output Composition` | `Lighting Data`, `Shadowing`, `Environment Lighting`, `Diagnostics and Profiling` |

This table is architectural rather than exhaustive: it states what each
scenario fundamentally requires, not every pass-local detail.

Important clarification:

- `ForOffscreenScene()` does **not** require `Final Output Composition`
  universally
- but `Final Output Composition` remains architecturally optional for that
  facade when the scenario needs it
- phase 1 should not narrow the facade so far that offscreen/editor preview
  flows cannot participate in composition when that is the correct scenario

## 4.5.1 Input Type Mapping

The facade input types are intentionally named and should map closely to current
engine concepts rather than remaining abstract placeholders forever.

Recommended phase-1 mapping:

- `FrameSessionInput`
  -> frame slot, frame sequence, delta-time aggregate
- `OutputTargetInput`
  -> target framebuffer plus optional associated surface context where relevant
- `ResolvedViewInput`
  -> current `ResolvedView` plus view identity/routing metadata as needed
- `PreparedFrameInput`
  -> current `PreparedSceneFrame`
- `CoreShaderInputsInput`
  -> explicit override bundle for baseline shader-execution inputs
- `RenderGraphHarnessInput`
  -> caller-authored graph coroutine matching the render-graph execution
     signature
- `SceneSourceInput`
  -> scene authority/reference used by offscreen scene rendering
- `OffscreenSceneViewInput`
  -> offscreen-scene-level view intent, likely close to `CompositionView` but
     constrained for this scenario
- `OffscreenPipelineInput`
  -> selected pipeline instance or pipeline-construction descriptor

## 5. Publication Design

Publication is split in phase 1 into two layers:

## 5.1 Baseline Publication

Owned by `Renderer Core`.

Responsibilities:

- `ViewConstants`
- core view-frame routing
- core draw-frame routing
- baseline view-color publication needed for normal shader execution

## 5.2 Optional Family Publication

Owned as:

- source data by the producing capability family
- publication substrate by the renderer
- consumption policy by the pipeline/graph

Families:

- `Lighting Data`
- `Shadowing`
- `Environment Lighting`
- `Diagnostics and Profiling`

This is the concrete first replacement slice for the current monolithic
publication path.

## 6. Queued Composition Model

Phase-1 composition design:

- `CompositionSubmission` remains the sole final handoff
- renderer queues multiple submissions per frame
- each submission remains single-target
- renderer drains the queue deterministically during late composition execution
- no cross-submission global z-order merge in phase 1
- no task-level `CompositionTarget` routing in phase 1

Conceptual renderer-side shape:

```cpp
struct PendingComposition {
  CompositionSubmission submission;
  std::shared_ptr<graphics::Surface> target_surface;
  std::uint64_t sequence_in_frame;
};

std::vector<PendingComposition> pending_compositions_;
```

Design consequence:

- phase 1 removes the architectural scalar bottleneck
- phase 1 still intentionally does not commit to the full task-targeted global
  compositor

Target semantics:

- `submission.composite_target`
  = submission-local execution framebuffer
- `target_surface`
  = runtime/presentation surface context used by the renderer for late
    execution bookkeeping and presentability integration

These are not redundant:

- framebuffer answers where submission work is executed
- surface answers which runtime/presentation surface that submission belongs to,
  if any

## 7. View Runtime Ownership Migration

Target design:

- pipeline owns view intent and ordering policy
- renderer owns canonical runtime view identity, storage, readiness, cached
  products, cleanup, and execution-time lifetime

Current code does not fully match that target, so this is an explicit migration
direction rather than only a conceptual rule.

## 7.1 ForwardPipeline Migration Signal

`ForwardPipeline` currently depends directly on concrete renderer-owned methods.

Phase-1 design stance:

- this is acceptable in principle because pipelines are engine-internal
  production code
- but the direct coupling points must be inventoried and classified during
  migration:
  - acceptable permanent concrete dependency
  - should move to capability-owned access
  - should move to a bridged renderer API

This is a migration requirement, not just an observation.

## 8. Helper and Preset Strategy

Facade ergonomics should come from helper/preset layers outside the base facade.

Examples:

```cpp
namespace harness::single_pass::presets {
  auto ForFullscreenGraphicsPass(...);
  auto ForPreparedSceneGraphicsPass(...);
}

namespace harness::render_graph::presets {
  auto ForSingleViewGraph(...);
}

namespace offscreen::scene::presets {
  auto ForPreview(...);
  auto ForCapture(...);
}
```

This keeps:

- core facade semantics small
- production path clean
- test/tool ergonomics practical

## 9. Inter-View Dependency Design

Phase 1 supports only:

- composition order
- exposure dependency
- future routing-style composition-target relation

Phase 1 explicitly avoids:

- public dependency DAG authoring
- general dependency-edge authoring in `CompositionView`

## 10. Phase-1 Acceptance Matrix (met)

The architecture supports:

- `ForSinglePassHarness()`
- `ForRenderGraphHarness()`
- `ForOffscreenScene()`
- authoritative `RenderContext`
- pipeline-owned pass instances/default configs
- queued multi-submission, single-target composition

And should avoid:

- universal giant non-runtime facade
- generic typed-slot injection facade
- public inter-view dependency DAG
- full task-targeted global compositor

---

## 11. Phase-2 Design: Environment Lighting Service Extraction

Phase 2 extracts the `Environment Lighting` capability family from the
monolithic `Renderer` class into a standalone `EnvironmentLightingService`.
This is a structural refactor: no rendering behavior changes.

### 11.1 Service Responsibilities

`EnvironmentLightingService` owns the full lifecycle of environment lighting:

1. **Initialization.** Create BRDF LUT manager, IBL manager, static
   environment data manager, environment publishers, sky capture pass,
   atmosphere LUT compute pass, IBL compute pass, and their configs.
2. **Per-frame update.** Advance environment publishers, advance the static
   environment manager, apply blue-noise/debug toggle state, and maintain any
   dirty-tracking needed for sky capture or IBL regeneration.
3. **Per-view state contribution.** Own the environment-specific work currently
   embedded in renderer view preparation:
   - update `PerViewRuntimeState::environment_view`
   - create/update the per-view atmosphere LUT manager
   - synchronize `RenderContext::current_view.atmo_lut_manager`
   - keep `EnvironmentStaticDataManager` synchronized with the active view
4. **Per-view environment pass execution.** Run the atmosphere LUT compute,
   sky capture, and IBL compute passes for a given view. This absorbs the
   ~130-line orchestration block currently inline in `Renderer::OnRender`
   STEP 2 (lines ~2877-2990 in the current source). The service owns the
   pass objects **and** the sequencing logic that drives them.
5. **Per-view publication.** Publish `EnvironmentViewData` and
   `EnvironmentFrameBindings` for a given view. Manage per-view atmosphere
   LUT instances (create on first access, evict on view removal).
6. **Shutdown.** Tear down all owned objects in reverse order.

### 11.2 Service Interface Shape

The service exposes a small public surface. The exact signatures will follow
current engine conventions, but the semantic contract is:

- `Initialize(Graphics&, RendererConfig&, ...)` — called once during
  `Renderer::OnAttached` when `kEnvironmentLighting` is present.
- `OnFrameStart(...)` — called per frame to advance publisher/static-manager
  frame lifecycle and apply environment runtime toggles.
- `UpdatePreparedViewState(...)` — called from the renderer's scene-prep path
  to populate `PerViewRuntimeState::environment_view`, update atmosphere LUT
  parameters, and synchronize per-view environment products.
- `PrepareCurrentView(...)` — called while wiring the active
  `RenderContext` view to set `current_view.atmo_lut_manager` and keep static
  environment data aligned with the current view's atmosphere availability.
- `ExecutePerViewPasses(ViewId, RenderContext&, CommandRecorder&)` — called
  from `Renderer::OnRender` per-view loop (current STEP 2) to execute the
  atmosphere LUT compute, sky capture, and IBL compute passes. Encapsulates
  the full sequencing and intermediate `EnvironmentStaticData` updates
  between passes. Returns early (no-op) when dirty-tracking indicates no
  work is needed.
- `PublishForView(ViewId, RenderContext&, PerViewRuntimeState&, ...)` —
  called from `RepublishCurrentViewBindings` to handle the environment
  portion of optional-family publication.
- `NoteViewSeen(ViewId, frame::SequenceNumber)` /
  `EvictInactiveViewProducts(...)` — owns the inactive-view eviction window
  currently tracked through `last_seen_view_frame_seq_`.
- `EvictViewProducts(ViewId)` — called when a view is removed or its
  cached products are invalidated.
- `Shutdown()` — called during `Renderer::OnShutdown`.

Accessor methods for callers that need runtime environment objects:

- `GetBrdfLutManager()` / `GetIblManager()` / `GetIblComputePass()`
- `GetStaticDataManager()`
- `GetOrCreateAtmosphereLutManagerForView(ViewId)`
- `RequestSkyCapture()` / `RequestIblRegeneration()`
- `SetAtmosphereBlueNoiseEnabled(bool)` / `GetAtmosphereDebugFlags()`

### 11.3 Integration with Renderer

The `Renderer` class changes:

1. **New member.** A single `unique_ptr<EnvironmentLightingService>`
   replaces the environment publishers, managers, passes, configs, and
   environment-owned tracking state currently stored directly on `Renderer`.
2. **Conditional initialization.** In `OnAttached`, if
   `HasCapability(kEnvironmentLighting)`, create the service and call
   `Initialize(...)`. Otherwise the pointer stays null.
3. **Frame-start delegation.** In `BeginFrameServices`, the renderer no longer
   advances `environment_view_data_publisher_`,
   `environment_frame_bindings_publisher_`, or `env_static_manager_`
   directly. It calls `env_service_->OnFrameStart(...)`.
4. **Scene-prep delegation.** In `RunScenePrep`, the environment-specific
   block that populates `runtime_state.environment_view`, updates atmosphere
   LUT parameters, and touches `GetOrCreateSkyAtmosphereLutManagerForView(...)`
   moves into `env_service_->UpdatePreparedViewState(...)`.
5. **Current-view wiring delegation.** In
   `PrepareAndWireViewConstantsForView`, the environment-specific
   `current_view.atmo_lut_manager` assignment and static-data synchronization
   move into `env_service_->PrepareCurrentView(...)`.
6. **Execution delegation.** In `Renderer::OnRender`, the ~130-line STEP 2
   environment pass execution block is replaced by a single guarded call:

   ```cpp
   if (env_lighting_service_) {
     env_lighting_service_->ExecutePerViewPasses(view_id, render_context, recorder);
   }
   ```

   The renderer no longer references `sky_capture_pass_`,
   `sky_atmo_lut_compute_pass_`, or `ibl_compute_pass_` directly. The
   remaining per-view loop structure (STEP 1 scene prep → STEP 2 env
   service call → STEP 3 framebuffer → STEP 4 pipeline graph) is
   pipeline-agnostic and stable for any future pipeline (Deferred,
   Forward+, etc.).
7. **Publication delegation.** In `PublishOptionalFamilyViewBindings`, the
   environment block is replaced by a call to
   `env_service_->PublishForView(...)`. The shadow publication block remains
   unchanged in phase 2.
8. **Eviction delegation.** `Renderer` delegates both direct per-view eviction
   and inactive-view environment eviction bookkeeping to the service.
9. **Shutdown delegation.** `OnShutdown` calls `env_service_->Shutdown()`
   before resetting the pointer.
10. **Public API delegation.** The existing environment bridge methods on
   `Renderer` become thin one-line forwards to the service for phase 2. A
   `GetEnvironmentLightingService()` accessor is optional and may be added if
   it helps future extractions, but pass/pipeline call sites do not need to
   churn just to complete this refactor.

### 11.4 Integration with ForwardPipeline

`ForwardPipeline` currently accesses environment functionality through
bridged renderer APIs. After extraction, these bridged calls still go
through the renderer, which delegates to the service. The pipeline does not
hold a direct pointer to the service, and phase 2 does not require broad
environment-pass churn.

Representative bridge methods that continue to work unchanged:

- `SetAtmosphereBlueNoiseEnabled(...)` — renderer delegates to service
- `GetSkyAtmosphereLutManagerForView(...)` — renderer delegates to service

The pipeline does not need modification. The bridge pattern from phase 1
handles this transparently.

### 11.5 Service Dependencies

The service needs these inputs, provided by the renderer at construction or
per-call:

- `Graphics&` — for GPU resource creation
- `RendererConfig&` — for reading environment-related config
- `upload::UploadCoordinator&` and upload staging provider access — needed by
  BRDF LUT and per-view atmosphere LUT managers
- inline staging provider and `InlineTransfersCoordinator&` — needed by the
  environment publishers
- `renderer::resources::TextureBinder&` — needed by
  `EnvironmentStaticDataManager`
- per-view identifiers and view state at publish-time

The service does **not** receive a `Renderer*` or `Renderer&`. This keeps
the dependency arrow clean: `Renderer` → `EnvironmentLightingService`, never
the reverse.

### 11.6 File Placement

New files:

- `src/Oxygen/Renderer/Environment/EnvironmentLightingService.h`
- `src/Oxygen/Renderer/Environment/EnvironmentLightingService.cpp`

Existing environment-related files (e.g., `BrdfLutManager`,
`SkyAtmosphereLutManager`, `IblManager`, `EnvironmentStaticDataManager`) stay
where they are. They become dependencies of the new service instead of being
held directly by `Renderer`.

### 11.7 Test Strategy

The extraction is a structural refactor. The primary validation is:

1. **All relevant renderer proof suites remain green.**
   Specifically: the suites that exercise environment publication, atmosphere
   setup, or IBL/static-environment binding through the active renderer proof
   surface.
2. **Full-build gate.** `cmake --build --preset windows-debug --parallel 8`
   passes.
3. **Focused extraction proof.** Extend the active renderer proof surface with
   focused checks for:
   - capability-present environment delegation/publication
   - capability-absent environment no-op behavior
   - per-view environment cache eviction
4. **Runtime gate.** `Oxygen.Examples.RenderScene.exe` and
   `Oxygen.Examples.MultiView.exe` run without regression.
   `Oxygen.Examples.DemoShell.exe` is a secondary smoke if its current preset
   includes environment lighting.
5. **Absent-capability gate.** Keep the existing
   `OffscreenSceneFacade.Tests` reduced-capability execution proof green so a
   renderer without `kEnvironmentLighting` still works.

A focused unit test for the service is optional but recommended. It should
verify that `Initialize` + `Shutdown` do not crash, and that
`PublishForView` produces non-default environment bindings when given a
valid view.

## 12. Capability-Family Service Extraction Guide

This section documents the pattern established by the Environment Lighting
extraction so that future extractions follow a consistent approach.

### 12.1 When to Extract

Extract a capability family into a service when:

- the family owns 3+ member variables in `Renderer`
- the family has identifiable initialization, per-view, and shutdown
  lifecycle phases
- the family's dependencies do not form a cycle with other unextracted
  families

### 12.2 Steps

1. **Inventory.** List every `Renderer` member variable, private method, and
   public method that belongs to the family. Use the existing
   current-state inventory in [ARCHITECTURE.md](modular-renderer-architecture.md) §3.2.1
   as a starting reference.

2. **Create the service class.** Place it under
   `src/Oxygen/Renderer/<FamilyName>/`. It should have:
   - `Initialize(...)` — receives only the specific dependencies it needs
   - `OnFrameStart(...)` — if the family has per-frame work or publisher
     lifecycle
   - `UpdatePreparedViewState(...)` / `PrepareCurrentView(...)` when the family
     contributes per-view runtime state or `RenderContext` wiring
   - `PublishForView(...)` — if the family publishes optional bindings
   - `NoteViewSeen(...)` / `EvictViewProducts(ViewId)` when the family holds
     per-view state or eviction tracking
   - `Shutdown()` — tears down owned objects

3. **Move member variables.** Cut them from `Renderer.h` and add them as
   private members of the service. Do this mechanically: the variable names
   and types stay the same.

4. **Move initialization logic.** Find the code in `Renderer::OnAttached`
   (or `EnsureXxxInitialized`) that creates the family's objects. Move it
   into the service's `Initialize` method.

5. **Move publication logic.** Find the family's block inside
   `Renderer::PublishOptionalFamilyViewBindings`. Move it into the service's
   `PublishForView` method. Replace the original block with a one-line
   delegation call.

6. **Move per-view state management.** Find per-view maps, create-on-access
   helpers, and eviction logic. Move them into the service.

7. **Move debug/runtime toggles.** Move runtime toggle state that belongs to
   the family. Only move console bindings if they actually exist; do not invent
   new console surface just to satisfy the pattern.

8. **Move shutdown logic.** Find teardown code in `Renderer::OnShutdown`.
   Move it into the service's `Shutdown` method. Call `Shutdown` from
   `Renderer::OnShutdown`.

9. **Update Renderer.** Replace the removed members with a single
   `unique_ptr<XxxService>`. Gate creation on `HasCapability(kXxx)`. Add a
   public `GetXxxService()` accessor only if it meaningfully helps callers;
   existing bridge methods may remain as thin forwards.

10. **Update callers.** Public methods that moved to the service either
    become thin forwards on `Renderer` (if many external callers exist) or
    callers switch to `renderer.GetXxxService()->Method()`.

11. **Validate.** Full build, all affected test suites, runtime examples.

### 12.3 Rules

- The service class must not hold a `Renderer*` back-pointer.
- The service receives only the specific objects it needs.
- Publisher instances for the extracted family move with the service so the
  renderer no longer owns family-specific publisher members.
- The service does not interact with other capability-family services
  directly. Cross-family coordination goes through the renderer.
