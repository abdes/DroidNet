# Modular Renderer Architecture

**Date:** 2026-04-13
**Status:** Design / Architecture

This document captures the stable conceptual model for the modular renderer.
It defines architectural boundaries, ownership, capability families, and
architectural commitments across phases. It avoids implementation sequencing
and task-level planning.

Related:

- [PRD.md](modular-renderer-prd.md)
- [DESIGN.md](modular-renderer-design.md)
- [PLAN.md](modular-renderer-implementation-plan.md)

## 1. Ground Truths

1. In Oxygen, the render graph is a coroutine.
2. `ForwardPipeline` is one concrete pipeline, not the renderer itself.
3. Custom graph logic is a normal extension path.
4. Every render pass already has a config object.
5. Passes are engine-authored and engine-delivered.
6. Pipelines are engine-authored production code.
7. `Composition` is suitable for coarse capability assembly.

## 1.1 Current Understanding

Today the renderer still appears in the codebase as two broad layers:

- a renderer-owned service hub in `engine::Renderer`
- a higher-level pipeline/policy layer centered on `RenderingPipeline` and
  currently represented concretely by `ForwardPipeline`

This matters architecturally because the modular design is not inventing a new
layer stack from nothing; it is clarifying and restructuring an already-visible
one.

Important current-state signals:

- render graph is already a coroutine
- `ForwardPipeline` is already only one pipeline, not the renderer itself
- pass config objects already exist and are a real customization seam
- `Composition` is already an engine architectural tool for coarse composition

## 2. Layer Model

The renderer architecture is intentionally layered.

| Layer | Meaning | Owns | Customization mode |
| --- | --- | --- | --- |
| `Renderer` | Structural composition root and execution substrate | capability modules, runtime/session services, frame execution substrate | structural composition and scenario factories |
| `Pipeline` | Interchangeable policy package | view policy, scenario policy, default pass-config routing, graph construction rules | config data |
| `RenderGraph` | Concrete coroutine orchestration | ordered/conditional rendering logic for one scenario or view | logic |
| `RenderPass` | Reusable execution primitive | one rendering/compute operation plus config | config and new pass types |

Working interpretation:

- renderer answers: what capability families exist in this assembly?
- pipeline answers: what policy should be applied?
- render graph answers: what exact work runs?
- render pass answers: how does one operation prepare and execute?

This means customization is intentionally split across three different modes:

- **structural customization** at the renderer level
- **data-driven customization** at the pipeline and pass level
- **logic-driven customization** at the render-graph level

## 3. Architectural Slices

The current codebase already expresses these broad slices:

- application intent through `CompositionView`
- pipeline policy through `RenderingPipeline`
- renderer execution/runtime ownership through `engine::Renderer`
- reusable work units through `RenderPass`

These remain valid architectural slices in the modular design.

## 3.1 Scenario and Factory Model

The architecture should still be read through the scenario-factory model:

- renderer assemblies are selected structurally by scenario
- pipelines are selected/configured on top of those assemblies
- render graphs and passes execute against the resulting substrate

Phase-1 scenario-facing model:

- full runtime renderer assembly
- `ForSinglePassHarness()`
- `ForRenderGraphHarness()`
- `ForOffscreenScene()`
- feature-selected runtime variants

## 3.2 Current-State Inventory and Pressure Signals

The current production renderer still owns a broad set of concrete services.
That fact is important architecture context even after the phase-1 family model
is defined.

Current-state pressure signals from that ownership:

- the current renderer has historically mixed:
  - execution substrate
  - per-view runtime storage
  - scene preparation
  - upload/binding ingress
  - feature publication
  - lighting/shadow services
  - environment services
  - final composition
  - diagnostics

- this is why the modular design explicitly separates:
  - `Renderer Core`
  - bridged runtime view ownership
  - capability families
  - pipeline-owned policy and pass sets

Architecture consequence:

- the family model in this package is more authoritative than the old raw
  implementation grouping
- but implementers should remember that some boundaries are being improved from
  an existing monolith, not designed in a vacuum

## 3.2.1 Detailed Current-State Inventory

The original working design also carried a concrete current-state inventory.
That detail is preserved here because it is useful when mapping the architecture
back onto the existing codebase.

| Current-state bucket | Concrete current ownership signals | Why it matters |
| --- | --- | --- |
| Execution/runtime host | `gfx_weak_`, `engine_`, `config_`, `render_context_pool_`, `render_context_`, `frame_seq_num`, `frame_slot_`, `last_frame_dt_seconds_`, `frame_budget_stats_`, `offscreen_frame_used_`, `offscreen_frame_active_` | This is the current execution substrate that maps to phase-1 `Renderer Core`. |
| View runtime state | `view_registration_mutex_`, `render_graphs_`, `view_state_mutex_`, `view_ready_states_`, `resolved_views_`, `prepared_frames_`, `per_view_storage_`, `per_view_runtime_state_`, `pending_cleanup_mutex_`, `pending_cleanup_` | This is the current raw material for `View Registration and Lifetime`. |
| Scene-prep path | `scene_prep_`, `scene_prep_state_`, plus the services installed into `ScenePrepState`: `GeometryUploader`, `TransformUploader`, `MaterialBinder`, `DrawMetadataEmitter`, `LightManager` | This is the current mixed ownership area later narrowed into `Scene Preparation`, `GPU Upload and Asset Binding`, and `Lighting Data`. |
| GPU ingress | `uploader_`, `upload_staging_provider_`, `inline_transfers_`, `inline_staging_provider_`, `texture_binder_`, `asset_loader_` | This becomes the stable `GPU Upload and Asset Binding` family. |
| Baseline shader publication | `view_const_cpu_`, `view_const_manager_`, `view_frame_bindings_publisher_`, `draw_frame_bindings_publisher_`, `view_color_data_publisher_` | This is why `Renderer Core` includes baseline shader-execution substrate rather than exposing it as a peer optional family. |
| Optional family publication | `debug_frame_bindings_publisher_`, `lighting_frame_bindings_publisher_`, `shadow_frame_bindings_publisher_`, `vsm_frame_bindings_publisher_`, `environment_view_data_publisher_`, `environment_frame_bindings_publisher_`, `conventional_shadow_draw_record_buffer_` | This is the source of the publication refactor pressure and why there is no top-level `Feature Shader Inputs` family in phase 1. |
| Lighting/shadow services | `shadow_manager_`, with `LightManager` still indirectly owned through `ScenePrepState` | This is the ownership drift that motivates the split into `Lighting Data` and `Shadowing`. |
| Environment services | `environment_view_data_publisher_`, `environment_frame_bindings_publisher_`, `brdf_lut_manager_`, `per_view_atmo_luts_`, `ibl_manager_`, `sky_capture_pass_`, `sky_capture_pass_config_`, `sky_atmo_lut_compute_pass_`, `sky_atmo_lut_compute_pass_config_`, `ibl_compute_pass_`, `env_static_manager_`, `last_atmo_generation_`, `last_seen_view_frame_seq_`, `sky_capture_requested_`, atmosphere state | This becomes `Environment Lighting` as a coherent family with internal subfacets. |
| Composition state | `composition_mutex_`, `composition_submission_`, `composition_surface_`, `compositing_pass_`, `compositing_pass_config_` | This is the current scalar bottleneck replaced by queued multi-submission composition. |
| Diagnostics state | `gpu_debug_manager_`, `gpu_timeline_profiler_`, `gpu_timeline_panel_`, `imgui_module_subscription_`, `gpu_timeline_panel_drawer_token_`, `console_`, timing/stat accumulators | This remains an optional `Diagnostics and Profiling` family. |

Important exclusion:

- `last_scene_identity_` is not part of the environment family in the current
  implementation. It is used to reset shadow cache state on scene changes and
  therefore stays with the shadow/runtime side unless a later phase proves
  otherwise.

Useful source anchors for this inventory:

- `Renderer` owned state in
  [src/Oxygen/Renderer/Renderer.h](../src/Oxygen/Renderer/Renderer.h)
- eager/full initialization in
  [src/Oxygen/Renderer/Renderer.cpp](../src/Oxygen/Renderer/Renderer.cpp)
  (`OnAttached`, `EnsureOffscreenFrameServicesInitialized`,
  `BeginFrameServices`)
- scene preparation state in
  [src/Oxygen/Renderer/ScenePrep/ScenePrepState.h](../src/Oxygen/Renderer/ScenePrep/ScenePrepState.h)

## 4. Stable Contracts

The following are treated as stable phase-1 architectural contracts:

- `CompositionView` remains the public view-intent surface
- `RenderingPipeline` remains the pipeline policy interface
- render-graph coroutine registration remains valid
- `CompositionSubmission` remains the sole final handoff in phase 1
- pass-local config objects remain the pass customization surface
- `RenderContext` remains the authoritative execution context

## 4.1 Provisional Policy

The following are intentionally **not** frozen as stable phase-1 architecture:

- the exact `ForwardPipeline` pass order
- the current HDR/SDR intermediate ownership split
- the current copy-vs-texture-blend compositing heuristics
- the current implementation placement of scene-preparation responsibilities
- the current binding/publication factoring details

These are production policy or implementation-shape decisions that may still
change without invalidating the architecture defined here.

## 5. Phase-1 Structural Model (delivered)

Phase-1 distinguished three different kinds of architectural parts.
All of the following are delivered and stable.

### 5.1 Baseline Substrate

- `Renderer Core`

Rules:

- baseline substrate is not a selectable capability family
- it is always present in every renderer assembly

### 5.2 Bridged Runtime Concerns

- `View Registration and Lifetime`

Rules:

- bridged runtime concerns are not selectable capability families
- they remain cross-cutting runtime seams governed by the ownership bridge rule

### 5.3 Selectable Capability Families

- `Scene Preparation`
- `GPU Upload and Asset Binding`
- `Lighting Data`
- `Shadowing`
- `Environment Lighting`
- `Final Output Composition`
- `Diagnostics and Profiling`

Important architectural rules:

- `Scene Preparation` is not universal; it is required only for
  scene-derived rendering scenarios
- `Lighting Data` and `Shadowing` are separate families
- `Final Output Composition` is an explicit family, not an implicit side effect

## 5.4 Phase-2 Structural Evolution: Capability-Family Services

Phase 1 established the capability-family vocabulary. The `Renderer` class
knows which families are present and gates pipeline binding on them. However,
the `Renderer` class still physically owns all member variables, initialization,
publication, per-view management, and shutdown for every family. This means the
monolith is ~4,800 lines and ~65 member variables.

Phase 2 starts extracting families into **capability-family services**:
standalone classes that own a family's full lifecycle.

### 5.4.1 What Is a Capability-Family Service?

A capability-family service is a class that encapsulates everything a single
capability family needs to operate:

- all member variables (managers, passes, configs, per-view state, tracking)
- initialization (called once when the renderer attaches to the graphics
  device)
- per-frame setup (called at frame start, including publisher lifecycle)
- per-view state contribution during scene prep / current-view binding
- per-view publication (called when the renderer publishes bindings for a view)
- per-view cached-product eviction (called when a view is removed)
- shutdown (called when the renderer detaches)
- debug/runtime toggle ownership for the family

The renderer holds a `unique_ptr` to the service, gated by `HasCapability`.
When the capability is absent, the pointer is null and the renderer skips all
calls to it.

### 5.4.2 Ownership Rules for Extracted Services

1. The service is **renderer-owned**. The renderer creates it, holds it, and
   destroys it.
2. The service **does not** hold a back-pointer to the full `Renderer`. It
   receives only the specific dependencies it needs (e.g., `Graphics&`,
   publication substrate references, view identity) through its constructor
   or through per-call parameters.
3. The service **owns** its optional-family publishers and advances their
   per-frame lifecycle. The renderer still owns the generic publication
   substrate concept, but the service owns the concrete publisher instances,
   the environment-specific source data contribution, and the `Publish()` calls.
4. The service **owns** any passes that are logically part of the family (e.g.,
   sky capture, atmosphere LUT computation, IBL computation for the
   `Environment Lighting` family). These passes are service-managed, not
   pipeline-managed and not renderer-managed.
5. The service **owns** per-view state maps for its family. Per-view creation,
   lookup, and eviction are service responsibilities.
6. Pipeline access to service functionality goes through **bridged renderer
   APIs** — the same pattern established in phase 1 for shadow and environment
   access.

### 5.4.3 Phase-2 Extraction Target: Environment Lighting

The first family to extract is `Environment Lighting`. It is the best candidate
because:

- it has the most member variables of any optional family (~15 variables, ~6
  public methods)
- it has no cyclical dependency on other optional families (Shadowing depends
  on the depth pre-pass chain; Environment does not)
- it already owns three renderer-held passes that should be service-owned
- it has per-view state (`per_view_atmo_luts_`) that demonstrates the per-view
  management pattern
- the publication seam (`PublishOptionalFamilyViewBindings`) already separates
  environment from shadow publication internally

The concrete member variables that move into `EnvironmentLightingService`:

| Current `Renderer` member | Role |
| --- | --- |
| `environment_view_data_publisher_` | Environment view publication |
| `environment_frame_bindings_publisher_` | Environment frame publication |
| `brdf_lut_manager_` | Global BRDF LUT management |
| `per_view_atmo_luts_` | Per-view atmosphere LUT cache |
| `ibl_manager_` | Image-based lighting computation |
| `sky_capture_pass_` | Sky capture render pass |
| `sky_capture_pass_config_` | Sky capture pass configuration |
| `sky_atmo_lut_compute_pass_` | Atmosphere LUT compute pass |
| `sky_atmo_lut_compute_pass_config_` | Atmosphere LUT pass configuration |
| `ibl_compute_pass_` | IBL compute pass |
| `env_static_manager_` | Static environment data (bindless SRV) |
| `last_atmo_generation_` | Atmosphere generation tracking |
| `last_seen_view_frame_seq_` | Per-view frame sequence tracking |
| `sky_capture_requested_` | Pending sky capture request flag |
| `atmosphere_blue_noise_enabled_` | Atmosphere debug flag |
| `atmosphere_debug_flags_` | Atmosphere debug overrides |

The public `Renderer` methods that move to the service or become thin
delegations:

| Current `Renderer` method | After extraction |
| --- | --- |
| `GetEnvironmentStaticDataManager()` | Service method |
| `GetIblManager()` | Service method |
| `GetIblComputePass()` | Service method |
| `GetSkyAtmosphereLutManagerForView()` | Service method |
| `RequestSkyCapture()` | Service method |
| `RequestIblRegeneration()` | Service method |
| `SetAtmosphereBlueNoiseEnabled()` | Service method |
| `GetOrCreateSkyAtmosphereLutManagerForView()` | Internal service method |

Phase-2 bridge rule:

- Existing pass/pipeline entry points on `Renderer` may remain as thin
  one-line forwards in phase 2 when that avoids broad pass churn. The goal of
  the extraction is to remove environment implementation ownership from
  `Renderer`, not to force every environment-related public symbol to disappear
  in the same phase.

### 5.4.4 Future Extraction Candidates

After Environment Lighting, the following families are candidates for
extraction in priority order:

1. **Diagnostics and Profiling** — self-contained, no cross-family coupling,
   ~5 member variables.
2. **Shadowing** — higher complexity due to depth-chain coupling and
   draw-record buffers, but already has `ShadowManager` as partial precedent.
3. **Lighting Data** — once shadowing is clean, lighting is a natural
   follow-on.

## 6. Ownership Split

## 6.1 Renderer-Owned

Renderer owns:

- graphics/engine attachment and frame/session lifetime
- `RenderContext` allocation, reset, and materialization
- upload/staging services
- asset/resource binding services
- capability-family service instances (held as `unique_ptr`, gated by
  capability availability)
- publication substrate
- non-runtime facade infrastructure
- canonical runtime state

## 6.2 Pipeline-Owned

Pipeline owns:

- staged user-facing settings
- interpretation of view intent
- frame planning and pass-selection policy
- pass instances
- default pass configs
- render-graph construction logic
- final composition payload production

Pipelines are engine-internal production code. They may depend directly on
concrete renderer-owned services when that is the simplest and most robust
design.

## 6.2.1 Minimum Pipeline Validity Contract

Beyond the existing phase hooks, a pipeline should be considered valid only if
it satisfies the following minimum contract:

1. **Capability declaration**
   It declares the coarse capability families it requires and the ones it may
   opportunistically consume.

2. **Policy ownership**
   It owns its pass set, pass instances, default pass configs, staged settings,
   and render-graph construction logic.

3. **Deterministic phase behavior**
   It applies staged settings deterministically and only at the intended phase
   boundaries.

4. **View intent production**
   If the scenario is view-based, it can describe or publish the views it wants
   rendered through the bridged renderer-owned view APIs.

5. **Graph registration**
   It can register the render graph work it wants executed for its views, or
   explicitly choose to register none.

6. **Final handoff**
   If the scenario uses final composition, it can produce a valid
   `CompositionSubmission`-style payload, or explicitly choose to produce none.

7. **Boundary discipline**
   It does not directly own canonical renderer runtime state such as
   `RenderContext` lifecycle, runtime `ViewId` storage, or late composition
   execution.

8. **Optional capability tolerance**
   It must degrade coherently when optional capability families are absent.

What this contract deliberately does **not** require:

- that every pipeline use scene preparation
- that every pipeline produce composition work
- that every pipeline support every scenario
- that every pipeline hide direct dependency on concrete renderer-owned services

This keeps alternate pipelines flexible while still making them production-ready
engine objects rather than loose bundles of hooks.

## 6.3 Bridged

Bridged areas follow this rule:

- pipeline provides intent, policy, or payload
- renderer owns canonical runtime state, storage, identity, and execution

The important bridged areas are:

- view registration and lifetime
- optional feature-family publication/consumption
- scene-preparation use
- final output composition

## 6.4 Cluster-by-Cluster Ownership Map

| Capability cluster | Ownership | Working rule |
| --- | --- | --- |
| `Renderer Core` | renderer-owned | Core execution substrate and baseline shader-execution substrate |
| `View Registration and Lifetime` | bridged | pipeline provides intent; renderer owns canonical runtime state/lifetime |
| `Scene Preparation` | renderer-owned capability, bridged usage | renderer owns reusable service; pipeline decides whether it is used |
| `GPU Upload and Asset Binding` | renderer-owned | shared reusable infrastructure |
| `Lighting Data` | renderer-owned capability, bridged usage | renderer owns reusable family; pipeline decides consumption |
| `Shadowing` | renderer-owned capability, bridged usage | renderer owns shadow products/services; pipeline decides consumption |
| `Environment Lighting` | renderer-owned capability, bridged usage | same pattern as lighting/shadowing |
| `Final Output Composition` | bridged | pipeline produces intent/payload; renderer executes late composition |
| `Diagnostics and Profiling` | renderer-owned capability | reusable engine service with policy toggles |

## 6.5 Boundary Pressure Signals

The current implementation history exposed these useful architectural signals:

- `Renderer Core` is baseline substrate, not a selectable family
- `Lighting Data` and `Shadowing` should not be fused into one indivisible
  family
- `Scene Preparation` should remain narrower than a general “everything scene
  related” bucket
- publication should not survive as a monolithic cross-domain family

## 7. Bridged Boundary Contracts

## 7.1 View Registration and Lifetime

- pipeline provides desired views and policy
- renderer owns canonical runtime `ViewId`, storage, readiness, cleanup, and
  execution-time lifetime

## 7.2 Scene Preparation Use

- renderer owns the scene-preparation capability
- pipeline decides whether a scenario uses it

## 7.3 Feature Publication

- capability families own source data
- renderer owns publication substrate
- pipeline decides whether its graph consumes a feature family

## 7.4 Final Output Composition

- pipeline produces `CompositionSubmission`
- renderer executes late composition work

## 8. Dependency Direction Rules

1. Renderer should not own one baked pass sequence.
2. Pipeline depends on renderer capabilities, not on one monolithic
   “full renderer” assumption.
3. Render graphs may branch through normal coroutine logic.
4. Passes depend on `RenderContext` plus config and required published data.
5. Adding a new pass should normally extend graph vocabulary, not force a
   renderer-core redesign.
6. Changing graph logic should not normally require a new renderer type.

One intentional engine-internal coupling point remains:

- if a pass needs typed cross-pass registration/lookup, extending
  `RenderContext::KnownPassTypes` is acceptable

## 8.1 Existing Pipeline Feature Vocabulary

The package introduces `RendererCapabilityFamily`, but the existing
`PipelineFeature` vocabulary remains valid and should coexist with it.

They serve different levels:

- `RendererCapabilityFamily`
  structural/runtime availability of coarse renderer families
- `PipelineFeature`
  pipeline discovery/advertised rendering features

Working rule:

- `GetCapabilityRequirements()` does not replace `GetSupportedFeatures()`
- both may coexist on a pipeline
- capability requirements answer:
  "can this pipeline legally run on this renderer assembly?"
- supported features answer:
  "what rendering features does this pipeline advertise/support?"

## 8.1 Injection Boundaries

The architecture distinguishes between:

- **high-level runtime injection**
  Example: `FrameContext`, scene source, view intent, surface topology
- **low-level non-runtime staged injection**
  Example: frame session, output target, resolved view, prepared frame, core
  shader-input override, explicit render-graph input
- **authoritative execution context**
  `RenderContext`, which remains the only pass-execution context

Phase-1 runtime rule:

- full runtime and editor-style scenarios inject primarily through
  `FrameContext` and renderer-owned runtime paths

Phase-1 non-runtime rule:

- harness and offscreen scenarios inject through separate facades with staged
  inputs
- those facades materialize a valid `RenderContext` only after legal
  finalization

This keeps runtime injection narrow while still allowing explicit legal
non-runtime setup.

## 8.1.1 Injection-Point Matrix

| Injection point | Meaning | Typical producer | Typical consumer |
| --- | --- | --- | --- |
| `Frame Context` | engine-owned runtime frame/view/surface/scene context | engine | renderer and pipelines |
| `Frame Session` | slot, sequence, delta time, session identity | engine or non-runtime caller | renderer/facades |
| `Scene Source` | scene authority | engine/app/tool | scene-rendering scenarios |
| `Resolved View` | resolved view/camera state | pipeline/view resolver or caller | renderer/graph/passes |
| `Prepared Frame` | finalized draw-ready per-view scene data | scene prep or caller | renderer/graph/passes |
| `Output Target` | framebuffer/surface/render target | engine/app/tool | renderer/graph/passes |
| `Render Graph Factory` | caller-authored graph coroutine | pipeline or engine dev | renderer |
| `Pass Config` | per-pass config payload | pipeline, graph, or tool/test caller | render pass |

## 8.2 Change Propagation Heuristic

This heuristic helps keep future changes in the right architectural layer:

- if the change selects or removes a capability family, it is a renderer
  composition concern
- if the change changes policy using the same capability set, it is a pipeline
  concern
- if the change changes control flow or conditional sequencing, it is a
  render-graph concern
- if the change changes one operation's parameters or implementation, it is a
  render-pass concern

Examples:

- disabling atmosphere in a runtime profile:
  renderer composition concern
- switching between two shadow strategies with the same assembled capability
  availability:
  pipeline policy or render-graph concern
- omitting `SkyPass` in one debug graph branch:
  render-graph concern
- adjusting `ToneMapPassConfig`:
  render-pass config concern
- introducing a new `SSRPass`:
  new pass type plus possible `KnownPassTypes` extension if typed cross-pass
  lookup is needed

## 8.3 Boundary Heuristic

The architecture should keep using these heuristics during implementation:

- if two responsibilities are selected together in every plausible scenario,
  question whether they are truly separate architecture families
- if a responsibility is omitted in multiple plausible scenarios, it deserves to
  remain an optional family rather than being pulled into baseline substrate
- if a scenario factory becomes awkward to express, the architecture boundary is
  probably wrong
- if a production concern can be phrased as policy, keep it in the pipeline
- if it can be phrased as reusable substrate or canonical runtime state, keep it
  in the renderer

## 9. Capability Model Selection

Phase-1 answer:

- the capability model is mixed
- but runtime-primary at the coarse capability-family level

Meaning:

- scenario factories choose which capability families are assembled
- internal implementation of each family remains free to use direct concrete
  C++ structure and compile-time techniques where helpful

## 10. Inter-View Dependency Model

Phase 1 supports only a small explicit set of inter-view relationships:

- composition order via `z_order` and submission order
- exposure dependency via `exposure_source_view_id`
- future composition-target relations as routing, not as a general DAG

Phase 1 explicitly avoids:

- general public view-dependency DAG authoring
- arbitrary dependency-edge authoring in `CompositionView`

## 11. Multi-Surface Composition Architecture

Phase-1 composition architecture is:

- `CompositionSubmission` remains the sole final handoff
- renderer performs late composition execution
- renderer moves to queued multi-submission, single-target submission
- full task-targeted global composition routing is deferred

This keeps the phase-1 architecture narrow while removing the scalar
single-submission bottleneck.

## 12. Runtime and Non-Runtime Lifetime Model

Phase-1 conceptual baseline:

- one renderer instance per scenario

Shared internals across instances may exist later as an implementation
optimization, but they are not the conceptual model.

## 13. Phase-1 Architecture Closure

Phase-1 architecture is considered shaped around these decisions:

- renderer/pipeline/render-graph/pass layer model
- authoritative `RenderContext`
- three non-runtime facades
- runtime-primary capability-family assembly
- bridged view/publication/composition boundaries
- queued multi-submission, single-target composition
- narrow explicit inter-view dependency model

What remains after this is mostly implementation migration and later-phase
expansion, not blank architectural uncertainty.
