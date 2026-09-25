# VTX-M06A — Multi-view proof closeout

Status: `validated`

| Field     | Summary                                                                                                                                                                                                                                                                      |
| --------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Outcome   | Per-view state, serialized view families, leases, auxiliary views and composition.                                                                                                                                                                                           |
| Remaining | Extensions: [VX-OVERLAY-01](../../OPEN_ITEMS.md#p3--unscheduled-capabilities), [VX-VIEW-01](../../OPEN_ITEMS.md#p2--engineering-follow-ups), [VX-VIEW-02](../../OPEN_ITEMS.md#p3--unscheduled-capabilities), [VX-POST-02](../../OPEN_ITEMS.md#p3--unscheduled-capabilities). |
| Evidence  | [Validation record](validation.md)                                                                                                                                                                                                                                           |

[Roadmap](../../PLAN.md) · [Design index](../../lld/README.md)

Dependencies: VTX-M05A, VTX-M05B, VTX-M05C, VTX-M05D

## Delivered scope

B1 per-view plan/state-handle substrate, B2 classification payloads, C `PerViewScope` serialized view-family loop, D scene-texture lease pool, E data-driven surface composition, F auxiliary dependency graph, G overlay lanes/view extensions, and H runtime proof tooling are implemented and validated. Standard and auxiliary MultiView proof layouts pass focused tests, CDB/debug-layer audit, RenderDoc scripted analysis, and 60-frame allocation-churn proof, including runtime/capture proof that an auxiliary producer output is extracted and consumed by a dependent view.

## Scope and acceptance

Scope:

- Heterogeneous views in one frame.
- Per-view capability flags, per-view shading mode, PiP/multi-surface routing,
  and product isolation.
- Validate service publications are per-view safe.

Status: `validated`

## 1. Goal

VTX-M06A proves that Vortex can render and compose multiple runtime scene views
in one engine frame without leaking render settings, products, histories, or
composition state between views.

The milestone is not a PiP patch. PiP is one layer layout inside the broader
contract. The target proof surface is editor-class multi-view rendering: one,
two, three, or four viewports, each with its own camera, render/debug mode,
feature mask, scene products, overlays, and surface routing.

The primary implementation target is the Vortex-native path described by
[../lld/multi-view-composition.md](../../lld/multi-view-composition.md). Legacy
`Oxygen.Renderer` is not a reference, fallback, or simplification path.

## 2. Scope

In scope:

- `CompositionView` / adjacent runtime intent extensions for view kind,
  per-view render settings, feature masks, state handles, surface routes,
  overlay policy, and auxiliary IO descriptors.
- `FrameViewPacket` / `ViewRenderPlan` as the per-frame, per-view source of
  effective render/debug settings.
- Producer-owned `ViewStateHandle` semantics for exposure and other histories.
- A renderer-owned view-family/render-batch loop that executes all eligible
  scene views, not just the first selected cursor.
- `PerViewScope` to constrain the existing `RenderContext::current_view`
  transition cursor and prevent ambient cross-view mutation.
- Per-view service product publication and explicit empty products when a
  feature is disabled for a view.
- Scene-texture lease/pool mechanics or an equivalent documented mechanism
  that avoids steady-state per-frame resource churn.
- Data-driven surface composition, including PiP as ordinary layer data.
- First-class auxiliary view modeling, dependency sorting, output extraction,
  and at least one same-frame producer/consumer validation path or explicit
  human-approved deferral.
- Minimal typed view extension hooks and overlay lane routing needed by the
  validation scenarios.
- Validation tooling for CDB/debug-layer, RenderDoc labels/output routing, and
  scene-texture allocation churn.
- Detailed status updates to [Milestone record](../../PLAN.md)
  only when evidence exists.

## 3. Non-Scope

Out of scope:

- Full editor UI or viewport manager.
- Parallel per-view command recording.
- New render graph / RDG replacement.
- XR/mobile multiview or instanced stereo.
- Multi-GPU fork/join.
- Dynamic resolution, screen percentage, TSR, or TAA implementation.
- Complete UE-style show-flag coverage.
- Full editor primitive mesh processor and full MSAA editor primitive
  rasterization, unless the LLD is updated first.
- Offscreen-only facade proof; that is `VTX-M06B`.
- Feature-gated runtime variants such as no-shadows/no-volumetrics; that is
  `VTX-M06C`.

## 5. Existing Behavior To Preserve

- Single-view runtime examples continue to work through the same public
  `CompositionView` entry points.
- Published runtime view id mapping remains stable for existing clients.
- Single-pass and render-graph harnesses remain available for tests/tools.
- Existing validated service behavior from M05A through M05D is not regressed:
  diagnostics, occlusion consumers, translucency, and directional/spot/point
  conventional shadows.
- `ForScene`, `ForPip`, `ForHud`, `ForImGui`, and `ForOverlay` factory helpers
  remain source-compatible unless a call site is updated in the same slice.
  `ForPip` may become a thin wrapper over surface-route data, but it must not
  survive as a divergent special composition path.
- Existing `SceneTextures` resource format contract remains intact while lease
  ownership changes.
- `RenderMode::kSolid`, `kWireframe`, and `kOverlayWireframe` behavior remains
  visually and diagnostically equivalent in a single-view frame.
- Debug modes that were validated in M05A remain registry-driven and do not
  regain hidden global behavior.
- Proof scripts continue to use `tools/vortex/VortexProofCommon.ps1`; no
  duplicate RenderDoc/CDB wrapper style is introduced.

## 6. UE5.7 Parity References

Every implementation slice that touches the relevant behavior must re-check the
local UE5.7 source. The first implementation commit for a slice should cite the
specific files/lines checked in commit notes or status evidence.

Core view/family references:

- `F:\Epic Games\UE_5.7\Engine\Source\Runtime\Engine\Public\SceneView.h`
  - `FSceneView::State`
  - `FSceneViewFamily`
  - `FSceneViewFamily::Views`
  - `FSceneViewFamily::AllViews`
  - view family screen-percentage/upscaler fields
- `F:\Epic Games\UE_5.7\Engine\Source\Runtime\Renderer\Private\SceneRendering.cpp`
  - `FViewInfo::FViewInfo(const FSceneView*)`
  - `FSceneRenderer::FSceneRenderer`
  - custom render pass / scene capture additional views
  - late `ViewFamily->Views.Add(NewView)` creation path

Extension and editor references:

- `F:\Epic Games\UE_5.7\Engine\Source\Runtime\Engine\Public\SceneViewExtension.h`
  - `ISceneViewExtension` setup and render-thread hook points
- `F:\Epic Games\UE_5.7\Engine\Source\Runtime\Renderer\Private\BasePassRendering.cpp`
  - wireframe, shader complexity, and editor primitive compositing paths
- `F:\Epic Games\UE_5.7\Engine\Source\Runtime\Engine\Public\SceneTexturesConfig.h`
  - extent, sample count, depth aux, and editor primitive sample count

Accepted design divergences already documented in the LLD:

- UE keeps `FEngineShowFlags` on the family and commonly uses one family per
  editor viewport. Vortex allows per-view feature masks in one render batch and
  therefore must use union shared builds plus per-view filtered bindings.
- M06A render batches are not UE instanced stereo/mobile multiview.
- M06A locks render resolution scale to `1.0`; dynamic resolution and TSR/TAA
  remain future work with insertion points preserved.

## 7. Contract Truth Table

| Contract                      | Producer                                  | Consumer                                                                                                            | Valid state                                                                                                          | Disabled/invalid/stale state                                                                                              | Diagnostics/proof                                                        |
| ----------------------------- | ----------------------------------------- | ------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------ |
| `FrameViewPacket`             | `FramePlanBuilder`                        | Renderer Core, `SceneRenderer`, stages through `PerViewScope`                                                       | Packet has published view id, `ViewKind`, effective `ViewRenderPlan`, state handle policy, routes, and feature mask. | Invalid published id or missing required route fails before rendering.                                                    | Unit tests for packet count, per-view modes, and invalid inputs.         |
| `ViewRenderPlan`              | `FramePlanBuilder`                        | Stage modules                                                                                                       | Per-view render/debug/depth/tone-map policy is final.                                                                | Frame defaults may seed only missing settings; no stage-readable frame-global effective mode.                             | Grep/static gate plus leakage tests.                                     |
| `ViewStateHandle`             | View producer                             | Renderer Core and services                                                                                          | Stable opaque handle keys exposure/history state.                                                                    | Missing handle means stateless view; dropped handle retires history after fences; reused `ViewId` does not reuse history. | History reuse/invalidation tests.                                        |
| Exposure source               | View lifecycle / frame plan               | Exposure/post-process                                                                                               | Consumer reads source handle's previous-frame exposure product.                                                      | Missing/inactive source falls back to own previous or fixed exposure and records diagnostic.                              | Unit test for no current-frame order constraint and fallback diagnostic. |
| Scene view execution          | `SceneRenderer::RenderViewFamily`         | Stage modules                                                                                                       | Every eligible `Primary`/`Auxiliary` packet enters `PerViewScope` and renders with its own products.                 | Composition-only packets bypass scene stages.                                                                             | Multi-view scene-stage label capture and unit tests.                     |
| Per-view service products     | Lighting/shadow/environment/post services | Scene stages/shaders                                                                                                | Enabled views receive valid bindings for their products.                                                             | Disabled views receive explicit typed empty products, not another view's previous binding.                                | Shadow-enabled/disabled union-build test and diagnostics records.        |
| Scene-texture lease           | Scene-texture pool                        | `SceneRenderer` stages and extraction                                                                               | Exclusive live lease for one view render; released only after extraction/consumers.                                  | Active aliasing, pool exhaustion, or unsafe reuse fails with diagnostic/assertion before corruption.                      | Lease alias, reuse, exhaustion, and allocation-churn tests.              |
| Composition layer             | Composition planner                       | Renderer composition queue                                                                                          | Deterministic surface/layer list with source, rect, blend/copy mode, opacity, color policy, and final state.         | Missing required source fails plan validation; optional source publishes typed invalid layer skip.                        | Composition planner tests and RenderDoc layer labels.                    |
| Auxiliary IO                  | View packet batch graph                   | Producer/consumer views and stage bindings; materials read typed `AuxiliaryViewBindings` slots, never raw resources | Unique producer for required `AuxOutputId`, extracted before consumer begins.                                        | Missing required producer fails before GPU work; optional missing producer publishes invalid binding and diagnostic.      | Batch graph tests and same-frame auxiliary capture proof.                |
| Overlay lane                  | View extensions / overlay producers       | Composition planner and scene view stages                                                                           | Lane is explicitly `WorldDepthAware`, `ViewScreen`, or `SurfaceScreen`.                                              | Unknown lane or nested scene render from hook fails validation.                                                           | Overlay ordering tests and capture labels.                               |
| Surface handoff               | Composition execution                     | Graphics present or texture consumer                                                                                | Backbuffer final state is `Present`; offscreen texture final state is `ShaderResource`.                              | Submission failure preserves/retire resources by fence ownership; no leaked active lease.                                 | CDB/debug-layer state audit and failure-path tests where practical.      |
| Serialized validation payload | Tool/schema layer                         | Runtime validation harness                                                                                          | JSON payloads schema-validate before conversion to C++ types.                                                        | Invalid render modes, flags, lanes, routes, or auxiliary IO are rejected before Vortex stage code.                        | Schema tests and script negative cases.                                  |

## 8. Implementation Slices

### Slice A - Plan And Status Truth Surface

Status: `completed_docs_only`

- Create this detailed plan and link it from roadmap/status docs.
- Initially kept VTX-M06A `planned`; implementation progress is now recorded
  only from B1 evidence onward.

Sources: `design/vortex/milestones/VTX-M06A/README.md`, `design/vortex/PLAN.md`, `design/vortex/PLAN.md`, `design/vortex/PLAN.md`.

[Checks](validation.md#slice-a---plan-and-status-truth-surface--checks).

Design constraints:

- Any contradiction between this plan and the reviewed LLD.

### Slice B1 - Per-View Plan And State Handles

Status: `completed`

- Make `FrameViewPacket` / `ViewRenderPlan` the only effective source for
  per-view render/debug/depth/tone-map policy.
- Introduce producer-owned `ViewStateHandle` semantics for exposure/history.
- Remove stage-readable frame-global effective render/debug state.

Primary files/areas:

- `src/Oxygen/Vortex/CompositionView.h`
- `src/Oxygen/Vortex/Internal/CompositionViewImpl.*`
- `src/Oxygen/Vortex/Internal/FrameViewPacket.*`
- `src/Oxygen/Vortex/SceneRenderer/Internal/ViewRenderPlan.*`
- `src/Oxygen/Vortex/SceneRenderer/Internal/FramePlanBuilder.*`
- `src/Oxygen/Vortex/Internal/ViewLifecycleService.*`
- `src/Oxygen/Vortex/Internal/PreviousViewHistoryCache.*`
- `src/Oxygen/Vortex/PostProcess/Passes/ExposurePass.*`
- focused tests under `src/Oxygen/Vortex/Test/`

`CompositionView.h` is listed in B1 because `ViewStateHandle` is
producer-owned. If implementation proves the handle can live entirely on
`CompositionViewImpl` until B2, that narrower path is acceptable, but the
slice-B1 review must explicitly record that decision.

Required behavior:

- Existing frame-global render/debug settings are consumed only as defaults at
  packet construction.
- Production stage code no longer reads `FramePlanBuilder::GetRenderMode()` or
  `FramePlanBuilder::ShaderDebugMode()` as effective per-frame state.
- Exposure sharing resolves to previous-frame state handle data, not same-frame
  source-before-consumer order.
- Views without state handles are explicitly stateless.

[Checks](validation.md#slice-b1---per-view-plan-and-state-handles--checks).

Design constraints:

- A stage still needs a global effective render/debug mode after packet
  conversion. Update the LLD before accepting a compatibility path.
- `ViewStateHandle` cannot be plumbed through the current producer types
  without changing the public `CompositionView` ABI; update the LLD/plan before
  accepting the ABI shape.

### Slice B2 - View Kind, Feature Mask, Routes, And Payload Classification

Status: `completed`

- Add `ViewKind`, typed feature mask, surface route placeholders, overlay policy
  placeholders, scene-texture descriptor key, and auxiliary IO placeholders
  after B1 makes per-view plan/state ownership authoritative.
- Classify every new payload as runtime-only C++ validation or
  schema-enforced serialized validation before it reaches stage code.

Primary files/areas:

- `src/Oxygen/Vortex/CompositionView.h`
- `src/Oxygen/Vortex/Internal/CompositionViewImpl.*`
- `src/Oxygen/Vortex/Internal/FrameViewPacket.*`
- `src/Oxygen/Vortex/SceneRenderer/Internal/FramePlanBuilder.*`
- `src/Oxygen/Vortex/Internal/ViewLifecycleService.*`
- focused tests under `src/Oxygen/Vortex/Test/`

Required behavior:

- `ViewKind` distinguishes `Primary`, `Auxiliary`, and `CompositionOnly`
  packets without changing scene rendering yet.
- `ViewRenderSettings`, feature/show mask, `ViewSurfaceRoute`, and
  `OverlayPolicy` are C++ runtime API payloads validated by constructors,
  builder checks, and unit tests until a serialized authoring path exists.
- The first slice that adds JSON/demo-settings authoring for those payloads
  must add or extend a schema in the same commit.
- LLD section 14.1's concrete `AuxOutputDesc` / `AuxInputDesc` schema must be
  present before B2 lands concrete descriptor types. If implementation finds
  that schema insufficient, B2 carries opaque placeholder ids only and slice F
  finalizes concrete descriptors after the LLD is patched.
- No stringly typed render mode, feature flag, overlay lane, surface route, or
  auxiliary product id crosses into stage modules.

[Checks](validation.md#slice-b2---view-kind-feature-mask-routes-and-payload-classification--checks).

Design constraints:

- Slice B2 needs concrete auxiliary producer/consumer behavior instead of
  placeholders. Move that work to slice F or update the LLD and this plan.
- A payload cannot be validated through typed construction or schema-first
  serialized validation.

### Slice C - `PerViewScope` And Serialized View-Family Loop

Status: `completed`

- Replace the production single-current-view scene path with a serialized
  view-family loop while still using the existing single `SceneTextures`
  family.
- Prove product isolation and view iteration before adding pooling.

Primary files/areas:

- `src/Oxygen/Vortex/RenderContext.h`
- new or adjacent `src/Oxygen/Vortex/Internal/PerViewScope.*`
- `src/Oxygen/Vortex/Renderer.*`
- `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.*`
- `src/Oxygen/Vortex/SceneRenderer/Internal/FramePlanBuilder.*`
- `src/Oxygen/Vortex/Test/SceneRendererPublication_test.cpp`
- `src/Oxygen/Vortex/Test/SceneRendererDeferredCore_test.cpp`

Required behavior:

- `SceneRenderer::RenderViewFamily` iterates `all_scene_views`.
- `SceneRenderer::OnRender` delegates to the same path for one-view execution.
- Only `PerViewScope` mutates `RenderContext::current_view` and
  `active_view_index`.
- Nested `PerViewScope` on the same context fails in debug.
- Every scene pass/product diagnostic label includes view identity.

[Checks](validation.md#slice-c---perviewscope-and-serialized-view-family-loop--checks).

Design constraints:

- Product publication still behaves as last-view-wins after two serialized
  views render.

### Slice D - Scene Texture Lease Pool

Status: `completed`

- Introduce descriptor-keyed scene-texture leases and route the multi-view loop
  through the pool.
- Bound resource growth and prevent active aliasing.

Primary files/areas:

- `src/Oxygen/Vortex/SceneRenderer/SceneTextures.*`
- new scene-texture pool/lease owner under `src/Oxygen/Vortex/SceneRenderer/`
  or `src/Oxygen/Vortex/SceneRenderer/Internal/`
- `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.*`
- `src/Oxygen/Vortex/Test/SceneTextures_test.cpp`
- `src/Oxygen/Vortex/Test/SceneRendererShell_test.cpp`
- `src/Oxygen/Vortex/Test/SceneRendererPublication_test.cpp`

Required behavior:

- Lease key covers extent, render scale, color/depth formats, GBuffer layout,
  velocity/custom-depth requirements, sample count, editor primitive attachment
  requirement, reverse-Z/depth convention, HDR/SDR requirement, debug
  attachment requirement, and queue affinity.
- Two live views with the same key receive separate leases unless serialized
  and proven released.
- Pool exhaustion is explicit; no unbounded allocation in steady-state proof.
- Composition/history artifacts are extracted before release.
- Leases that differ only by queue affinity are not pooled together.

[Checks](validation.md#slice-d---scene-texture-lease-pool--checks).

Design constraints:

- The pool cannot express existing `SceneTextures` rebuild/publication
  contracts without duplicated resource ownership.

### Slice E - Data-Driven Surface Composition

Status: `completed`

- Generalize composition from primary/z-order assumptions to structural surface
  plans.
- Represent PiP, one-view-to-two-surfaces, and two-views-to-one-surface as
  ordinary layer data.

Primary files/areas:

- `src/Oxygen/Vortex/Internal/CompositionPlanner.*`
- `src/Oxygen/Vortex/Renderer.*`
- `src/Oxygen/Vortex/RendererCompositionQueue*`
- `src/Oxygen/Vortex/Internal/FrameViewPacket.*`
- `src/Oxygen/Vortex/Test/Internal/CompositionPlanner_test.cpp`
- `src/Oxygen/Vortex/Test/RendererCompositionQueue_test.cpp`
- `Examples/MultiView/*` only if used by the proof scenario

Required behavior:

- Full-surface fast copy predicate is structural: opaque single layer covering
  destination viewport with compatible format/color space.
- No layer or output path assumes a special primary view id.
- Auxiliary view outputs are valid sources for any surface layer; full
  auxiliary producer/consumer plumbing arrives in slice F.
- Offscreen surface-as-texture handoff ends in `ShaderResource`.
- Surface and layer debug names are deterministic.
- Slice E must keep the slice-D churn-bound lease test green. If slice-H
  runtime tooling is not present yet, that checked-in lease-pool metric is the
  temporary allocation-proof gate for composition changes.

[Checks](validation.md#slice-e---data-driven-surface-composition--checks).

Design constraints:

- Composition still needs special PiP or primary-view code after surface routes
  are modeled.

### Slice F - Auxiliary Views And Dependency Graph

Status: `completed`

- Implement `AuxOutputDesc` / `AuxInputDesc` resolution, in-batch and
  cross-batch dependency ordering, extraction, and typed invalid fallback for
  optional missing inputs.

Primary files/areas:

- `src/Oxygen/Vortex/CompositionView.h`
- `src/Oxygen/Vortex/Internal/FrameViewPacket.*`
- `src/Oxygen/Vortex/SceneRenderer/Internal/FramePlanBuilder.*`
- `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.*`
- `src/Oxygen/Vortex/Internal/CompositionPlanner.*`
- new or existing auxiliary dependency graph unit under `src/Oxygen/Vortex/Test/`
- validation harness/tooling as needed

UE5.7 references to cite in slice commit/status evidence:

- `F:\Epic Games\UE_5.7\Engine\Source\Runtime\Renderer\Private\SceneRendering.cpp:3005-3040`
  for custom-render-pass / scene-capture views added to the family.
- `F:\Epic Games\UE_5.7\Engine\Source\Runtime\Renderer\Private\SceneRendering.cpp:5026`
  for late single-view creation added to `ViewFamily->Views`.
- `F:\Epic Games\UE_5.7\Engine\Source\Runtime\Engine\Public\SceneView.h:2308-2311`
  for the `Views` / `AllViews` split.

Required behavior:

- Missing required producer fails before GPU work.
- Duplicate required producers for one `AuxOutputId` fail before GPU work.
- Optional missing producer publishes typed invalid binding and diagnostic.
- Consumers check the typed validity flag and never branch on a sentinel raw
  pointer.
- Cross-batch producer batch renders/extracts before consumer batch begins.

[Checks](validation.md#slice-f---auxiliary-views-and-dependency-graph--checks).

Design constraints:

- The first same-frame consumer requires raw framebuffer pointer leakage or
  stringly typed stage payloads.

### Slice G - Overlay Lanes And View Extensions

Status: `completed`

- Add the minimal typed extension hooks and overlay lanes required by editor
  proof scenarios without creating a full editor primitive system.

Primary files/areas:

- new or adjacent view extension interface under `src/Oxygen/Vortex/`
- `src/Oxygen/Vortex/CompositionView.h`
- `src/Oxygen/Vortex/Internal/CompositionPlanner.*`
- `src/Oxygen/Vortex/Renderer.*`
- DemoShell/MultiView proof integration only if needed

Required behavior:

- Hook points: `OnFamilyAssembled`, `OnViewSetup`, `OnPreRenderView_GPU`,
  `OnPostRenderView_GPU`, and `OnPostComposition`.
- `OnPostComposition` runs on the graphics queue after all layers for the
  surface have been submitted and before the surface handoff/present state is
  finalized.
- No untyped late callbacks or re-entrant scene render from hooks.
- Existing `CompositionView::on_overlay` becomes a compatibility producer for
  a typed screen-overlay lane.
- World-depth-aware overlays target their own view depth before flattening.
- Editor primitive attachment lanes are reserved with placeholder batches if
  full MSAA editor primitive rasterization remains deferred, matching the LLD
  section 12.3 contract.

[Checks](validation.md#slice-g---overlay-lanes-and-view-extensions--checks).

Design constraints:

- Overlay code needs to mutate stage internals directly or cache
  `RenderContext::current_view` outside `PerViewScope`.

### Slice H - Runtime Validation Scene And Scripts

Status: `completed`

- Add the proof harness for M06A closure: runtime scene/layout, CDB audit,
  RenderDoc capture analysis, allocation-churn report, and schema validation
  for serialized validation payloads.

Primary files/areas:

- `tools/vortex/Run-VortexMultiViewValidation.ps1`
- `tools/vortex/Assert-VortexMultiViewProof.ps1`
- `tools/vortex/AnalyzeRenderDocVortexMultiView.py`
- `tools/vortex/multiview_cdb_allow.json`
- `tools/vortex/schemas/multiview-validation.schema.json` if serialized proof
  layouts are introduced
- validation scene/example code selected during implementation

Required behavior:

- Tooling reuses `tools/vortex/VortexProofCommon.ps1`.
- RenderDoc UI analysis runs sequentially through the existing UI lock helper.
- RenderDoc analyzer asserts per-view stage labels, distinct outputs, and
  surface layer order. The validation wrapper asserts runtime allocation churn
  bounds from scene-texture lease-pool telemetry.
- Auxiliary dependency ordering is covered by focused unit tests and runtime
  publication tests. Runtime/capture validation now includes extracted
  auxiliary color products consumed by a dependent view through a
  RenderDoc-visible `Vortex.AuxView.Consume` GPU copy scope.
- CDB gate fails any D3D12/DXGI validation message severity `WARNING` or
  higher unless allow-listed.

[Checks](validation.md#slice-h---runtime-validation-scene-and-scripts--checks).

[Checks](validation.md#slice-h---runtime-validation-scene-and-scripts--checks-2).

Design constraints:

- The proof scene cannot isolate per-view render modes or products clearly
  enough for script assertions.

### Slice I - Closure And Ledger Update

Status: `completed`

- Close the milestone only after implementation, docs, tests, CDB, RenderDoc,
  allocation-churn proof, and any required manual visual confirmation exist.

Primary files:

- `design/vortex/PLAN.md`
- `design/vortex/PLAN.md`
- `design/vortex/milestones/VTX-M06A/README.md`
- `design/vortex/lld/multi-view-composition.md` if implementation finds
  approved divergences

[Checks](validation.md#slice-i---closure-and-ledger-update--checks).

Design constraints:

- Any acceptance-gate item remains unproven or deferred without explicit human
  approval recorded in status docs.

## 11. Exit Gate

VTX-M06A can move to `validated` only when all of the following are true:

- Implementation exists for the accepted M06A scope.
- `CompositionView` / packet / plan contracts are documented and tested.
- Multiple scene views render through the Vortex-native path in one frame.
- Per-view render/debug settings are isolated.
- Per-view service products and diagnostics are view-keyed and do not
  last-view-wins overwrite.
- Histories use producer-owned `ViewStateHandle` semantics or are explicitly
  stateless for views without handles.
- Scene-texture allocation is lease/pool based or an approved equivalent with
  steady-state churn proof using the section 10 bound:
  `0` allocations after warmup unless new descriptor keys appear, then
  `<= count(new distinct descriptor keys)`.
- Composition is data-driven by surface plans; PiP is not a special path.
- Auxiliary views are modeled and validated, or explicitly deferred with human
  approval recorded in this plan and `milestone README`.
- In-batch and cross-batch auxiliary dependency ordering is proven by tests and
  capture analysis, unless explicitly deferred with human approval.
- Unit/integration tests pass.
- ShaderBake/catalog validation is recorded if shader/ABI changes occurred.
- CDB/debug-layer audit passes.
- RenderDoc scripted analysis passes.
- Required docs and the single VTX-M06A ledger row are updated with evidence.
- Residual gaps are listed as blockers or explicitly accepted/deferred by the
  human reviewer.

Record each task result with its corresponding check or capture.

## 12. Replan Triggers

Revisit the design if:

- The implementation needs a global effective render/debug mode after slice B1.
- Producer-owned `ViewStateHandle` cannot be plumbed through existing producer
  types such as editor viewport, scene capture, or headless harness without an
  ABI change to `CompositionView`; keep this in sync with the slice B1 rollback
  rule.
- `PerViewScope` cannot constrain `current_view` without broad stage rewrites.
- Scene-texture lease pooling requires duplicate ownership of GPU resources
  instead of wrapping existing `SceneTextures`.
- A service product cannot publish an explicit disabled/empty per-view binding.
- Auxiliary view consumption requires raw framebuffer pointer leakage into
  material or stage payloads.
- A proof scenario needs offscreen-only behavior that belongs to VTX-M06B.
- A feature-gated variant becomes necessary to prove absence/presence behavior
  that belongs to VTX-M06C.
- Dynamic resolution, TSR/TAA, XR/mobile multiview, or full editor primitive
  rasterization becomes required for the acceptance gate.
- UE5.7 source review shows the LLD has a parity-breaking divergence not
  already recorded and approved.
- Runtime validation cannot produce scriptable evidence and would rely only on
  manual visual inspection.

## Supporting records

- [validation](validation.md)
