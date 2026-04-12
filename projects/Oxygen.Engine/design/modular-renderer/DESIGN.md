# Modular Renderer Design

Status: `phase-1 design baseline`

This document captures the concrete chosen solution shapes for phase 1:
facades, contracts, validation rules, capability declarations, composition
behavior, and migration-oriented design decisions. It assumes the stable
conceptual model defined in [ARCHITECTURE.md](./ARCHITECTURE.md).

Related:

- [PRD.md](./PRD.md)
- [ARCHITECTURE.md](./ARCHITECTURE.md)
- [PLAN.md](./PLAN.md)

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

## 10. Phase-1 Acceptance Matrix

The architecture should support:

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
