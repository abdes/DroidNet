# VTX-M06A - Multi-View Proof Closeout

Status: `in_progress`

## 1. Goal

VTX-M06A proves that Vortex can render and compose multiple runtime scene views
in one engine frame without leaking render settings, products, histories, or
composition state between views.

The milestone is not a PiP patch. PiP is one layer layout inside the broader
contract. The target proof surface is editor-class multi-view rendering: one,
two, three, or four viewports, each with its own camera, render/debug mode,
feature mask, scene products, overlays, and surface routing.

The primary implementation target is the Vortex-native path described by
[../lld/multi-view-composition.md](../lld/multi-view-composition.md). Legacy
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
- Detailed status updates to [../IMPLEMENTATION_STATUS.md](../IMPLEMENTATION_STATUS.md)
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

## 4. Current State

| Area | Current state | M06A action |
| --- | --- | --- |
| Roadmap/status | `PLAN.md` and `IMPLEMENTATION_STATUS.md` list `VTX-M06A` as the active `in_progress` milestone after B1+B2 landed. | Keep status `in_progress` until the full exit gate is proven; update evidence in place after each verified slice. |
| LLD | `multi-view-composition.md` is review-addressed and implementation is in progress. | Treat as the implementation contract; update it first if implementation discovers scope drift. |
| Runtime view intent | `CompositionView` has `ViewId`, name, view, z-order, opacity, camera, HDR, environment toggles, exposure source id, force-wireframe, shading override, and overlay callback. | Extend without breaking existing single-view clients; add typed settings instead of more ad hoc booleans. |
| View lifecycle | `ViewLifecycleService` materializes and sorts active `CompositionViewImpl` instances and currently validates exposure with same-frame ordering. | Move exposure sharing to previous-frame `ViewStateHandle` semantics and retain deterministic active-view ordering. |
| Frame packets | `FrameViewPacket` carries a `CompositionViewImpl`, published `ViewId`, `ViewRenderPlan`, and composite texture helpers. | Slice B1 makes plan/state authoritative; slice B2 adds kind, feature mask, route placeholders, auxiliary IO placeholders, and scene-texture key. |
| Frame plan | `FramePlanBuilder` builds packets but still keeps frame-global render/debug state accessors. | Slice B1 removes stage-readable global effective mode and makes packets authoritative. |
| Render context | `RenderContext` already has `frame_views`, `active_view_index`, and `current_view`, but the cursor is ambient. | Add `PerViewScope`; stage execution must enter scope before using current-view fields. |
| SceneRenderer | `SceneRenderer::OnRender` executes the scene stage chain for the selected current view. | Introduce `RenderViewFamily` / `RenderView` path and delegate single-view execution through it. |
| SceneTextures | `SceneRenderer` owns one concrete `SceneTextures` family. | Slice C serializes through it; Slice D adds descriptor-keyed lease/pool. |
| Composition | `CompositionPlanner` has single-surface/layer behavior and still contains primary/z-order assumptions. | Replace with structural surface plans and deterministic layers. |
| Product publishers | Several services already use per-view publishers or view-keyed inspection surfaces. | Audit each touched product for last-view-wins behavior and explicit disabled products. |
| Exposure/history | `PreviousViewHistoryCache` is keyed by `ViewId`; `ExposurePass` uses view id and current selected source. | Route histories through `ViewStateHandle` or make stateless where no handle is provided. |
| Existing demo | [Examples/MultiView](../../../Examples/MultiView/MainModule.cpp) exists, but current composition remains app/demo-shaped and not the Vortex closure proof. | Reuse or adapt only if it exercises the Vortex-native path and proof tooling. |

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

| Contract | Producer | Consumer | Valid state | Disabled/invalid/stale state | Diagnostics/proof |
| --- | --- | --- | --- | --- | --- |
| `FrameViewPacket` | `FramePlanBuilder` | Renderer Core, `SceneRenderer`, stages through `PerViewScope` | Packet has published view id, `ViewKind`, effective `ViewRenderPlan`, state handle policy, routes, and feature mask. | Invalid published id or missing required route fails before rendering. | Unit tests for packet count, per-view modes, and invalid inputs. |
| `ViewRenderPlan` | `FramePlanBuilder` | Stage modules | Per-view render/debug/depth/tone-map policy is final. | Frame defaults may seed only missing settings; no stage-readable frame-global effective mode. | Grep/static gate plus leakage tests. |
| `ViewStateHandle` | View producer | Renderer Core and services | Stable opaque handle keys exposure/history state. | Missing handle means stateless view; dropped handle retires history after fences; reused `ViewId` does not reuse history. | History reuse/invalidation tests. |
| Exposure source | View lifecycle / frame plan | Exposure/post-process | Consumer reads source handle's previous-frame exposure product. | Missing/inactive source falls back to own previous or fixed exposure and records diagnostic. | Unit test for no current-frame order constraint and fallback diagnostic. |
| Scene view execution | `SceneRenderer::RenderViewFamily` | Stage modules | Every eligible `Primary`/`Auxiliary` packet enters `PerViewScope` and renders with its own products. | Composition-only packets bypass scene stages. | Multi-view scene-stage label capture and unit tests. |
| Per-view service products | Lighting/shadow/environment/post services | Scene stages/shaders | Enabled views receive valid bindings for their products. | Disabled views receive explicit typed empty products, not another view's previous binding. | Shadow-enabled/disabled union-build test and diagnostics records. |
| Scene-texture lease | Scene-texture pool | `SceneRenderer` stages and extraction | Exclusive live lease for one view render; released only after extraction/consumers. | Active aliasing, pool exhaustion, or unsafe reuse fails with diagnostic/assertion before corruption. | Lease alias, reuse, exhaustion, and allocation-churn tests. |
| Composition layer | Composition planner | Renderer composition queue | Deterministic surface/layer list with source, rect, blend/copy mode, opacity, color policy, and final state. | Missing required source fails plan validation; optional source publishes typed invalid layer skip. | Composition planner tests and RenderDoc layer labels. |
| Auxiliary IO | View packet batch graph | Producer/consumer views and stage bindings; materials read typed `AuxiliaryViewBindings` slots, never raw resources | Unique producer for required `AuxOutputId`, extracted before consumer begins. | Missing required producer fails before GPU work; optional missing producer publishes invalid binding and diagnostic. | Batch graph tests and same-frame auxiliary capture proof. |
| Overlay lane | View extensions / overlay producers | Composition planner and scene view stages | Lane is explicitly `WorldDepthAware`, `ViewScreen`, or `SurfaceScreen`. | Unknown lane or nested scene render from hook fails validation. | Overlay ordering tests and capture labels. |
| Surface handoff | Composition execution | Graphics present or texture consumer | Backbuffer final state is `Present`; offscreen texture final state is `ShaderResource`. | Submission failure preserves/retire resources by fence ownership; no leaked active lease. | CDB/debug-layer state audit and failure-path tests where practical. |
| Serialized validation payload | Tool/schema layer | Runtime validation harness | JSON payloads schema-validate before conversion to C++ types. | Invalid render modes, flags, lanes, routes, or auxiliary IO are rejected before Vortex stage code. | Schema tests and script negative cases. |

## 8. Implementation Slices

### Slice A - Plan And Status Truth Surface

Status: `completed_docs_only`

Purpose:

- Create this detailed plan and link it from roadmap/status docs.
- Initially kept VTX-M06A `planned`; implementation progress is now recorded
  only from B1 evidence onward.

Current evidence:

- This plan exists and is linked from `PLAN.md`, `IMPLEMENTATION_STATUS.md`,
  and `plan/README.md`.
- First plan-review feedback is incorporated: Slice B is split into B1/B2,
  schema/auxiliary sequencing is explicit, and slice-local proof gates cover
  history handles, `PerViewScope`, lease churn, composition routing, auxiliary
  dependencies, and overlays.
- Docs-only validation passed with `git diff --check`; no build/runtime tests
  are implied by this planning slice.

Primary files:

- `design/vortex/plan/VTX-M06A-multi-view-proof-closeout.md`
- `design/vortex/plan/README.md`
- `design/vortex/PLAN.md`
- `design/vortex/IMPLEMENTATION_STATUS.md`

Validation:

```powershell
git diff --check -- design\vortex\plan\VTX-M06A-multi-view-proof-closeout.md design\vortex\plan\README.md design\vortex\PLAN.md design\vortex\IMPLEMENTATION_STATUS.md design\vortex\lld\multi-view-composition.md
rg -n "VTX-M06A|multi-view proof closeout|VTX-M06A-multi-view-proof-closeout" design\vortex
```

Rollback/replan trigger:

- Any contradiction between this plan and the reviewed LLD.

### Slice B1 - Per-View Plan And State Handles

Status: `completed`

Purpose:

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

Current evidence:

- Implemented per-view `ViewRenderSettings`, producer-owned
  `ViewStateHandle`, packet-level state-handle copy, packet-owned effective
  shader-debug mode, handle-keyed `PreviousViewHistoryCache`, and
  ViewStateHandle-keyed exposure state.
- Changed files: `CompositionView.h`, `FrameViewPacket.h`,
  `ViewRenderPlan.h`, `FramePlanBuilder.*`, `ViewLifecycleService.*`,
  `PreviousViewHistoryCache.*`, `ExposurePass.*`, `PostProcessService.*`,
  `RenderContext.h`, `Renderer.*`, `SceneRenderer.*`, and focused tests.
- UE5.7 references re-checked: `SceneView.h` (`FSceneView::State`,
  `EyeAdaptationViewState`, `FSceneViewFamily::Views/AllViews`) and
  `SceneRendering.cpp` (`FViewInfo`, `FSceneRenderer`, `AllViews` assembly).
- Validation passed:
  `cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.RenderContext.Tests Oxygen.Vortex.CompositionPlanner.Tests Oxygen.Vortex.SceneRendererDeferredCore.Tests Oxygen.Vortex.PreviousViewHistoryCache.Tests Oxygen.Vortex.PostProcessService.Tests --parallel 4`;
  `ctest --preset test-debug -R "Oxygen\.Vortex\.(RenderContext|CompositionPlanner|SceneRendererDeferredCore|PreviousViewHistoryCache|PostProcessService)\.Tests$" --output-on-failure`
  with 5/5 test targets passing; static gate
  `rg -n "FramePlanBuilder::GetRenderMode|FramePlanBuilder::ShaderDebugMode|frame_render_mode_|frame_shader_debug_mode_" src\Oxygen\Vortex`
  returned no matches; `git diff --check` passed with line-ending warnings
  only.
- Remaining VTX-M06A gap: C-I are not implemented; runtime multi-view,
  CDB/debug-layer, RenderDoc scripted proof, and allocation-churn proof are
  still open.

Validation:

```powershell
cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.RenderContext.Tests Oxygen.Vortex.CompositionPlanner.Tests Oxygen.Vortex.SceneRendererDeferredCore.Tests Oxygen.Vortex.PreviousViewHistoryCache.Tests Oxygen.Vortex.PostProcessService.Tests --parallel 4
ctest --preset test-debug -R "Oxygen\.Vortex\.(RenderContext|CompositionPlanner|SceneRendererDeferredCore|PreviousViewHistoryCache|PostProcessService)\.Tests$" --output-on-failure
rg -n "FramePlanBuilder::GetRenderMode|FramePlanBuilder::ShaderDebugMode|frame_render_mode_|frame_shader_debug_mode_" src\Oxygen\Vortex
git diff --check
```

The `PreviousViewHistoryCache` test target may be introduced by this slice if
it does not exist yet. It must prove history-by-handle behavior, stateless views
without a handle, handle drop/recreate with the same `ViewId`, and descriptor
change invalidation.

Rollback/replan trigger:

- A stage still needs a global effective render/debug mode after packet
  conversion. Update the LLD before accepting a compatibility path.
- `ViewStateHandle` cannot be plumbed through the current producer types
  without changing the public `CompositionView` ABI; update the LLD/plan before
  accepting the ABI shape.

### Slice B2 - View Kind, Feature Mask, Routes, And Payload Classification

Status: `completed`

Purpose:

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

Current evidence:

- Implemented runtime C++ payload types for `ViewKind`,
  `ViewFeatureMask`, `ViewSurfaceRoute`, `OverlayPolicy`, and auxiliary input
  and output descriptors on `CompositionView`; factory helpers classify scene
  views as `Primary` and UI/HUD/tool overlays as `CompositionOnly`.
- `FrameViewPacket` now copies view kind, feature mask, surface routes,
  overlay policy, and auxiliary IO placeholders. `FramePlanBuilder` uses
  `ViewKind` for scene/composition classification and rejects invalid
  composition-only-with-camera or scene-kind-without-camera combinations.
- No serialized authoring path was added in B2; payload validation is runtime
  C++ construction/check coverage only, so no JSON schema was required by this
  slice.
- Validation passed:
  `cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.RenderContext.Tests Oxygen.Vortex.CompositionPlanner.Tests Oxygen.Vortex.SceneRendererDeferredCore.Tests --parallel 4`;
  `ctest --preset test-debug -R "Oxygen\.Vortex\.(RenderContext|CompositionPlanner|SceneRendererDeferredCore)\.Tests$" --output-on-failure`
  with 3/3 test targets passing; `git diff --check` passed with line-ending
  warnings only.
- Remaining VTX-M06A gap: C-I are not implemented; view-kind payloads are
  modeled but not yet executed by a multi-view family loop, lease pool,
  data-driven composition planner, auxiliary dependency graph, overlay lanes,
  or runtime proof tooling.

Validation:

```powershell
cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.RenderContext.Tests Oxygen.Vortex.CompositionPlanner.Tests Oxygen.Vortex.SceneRendererDeferredCore.Tests --parallel 4
ctest --preset test-debug -R "Oxygen\.Vortex\.(RenderContext|CompositionPlanner|SceneRendererDeferredCore)\.Tests$" --output-on-failure
git diff --check
```

Rollback/replan trigger:

- Slice B2 needs concrete auxiliary producer/consumer behavior instead of
  placeholders. Move that work to slice F or update the LLD and this plan.
- A payload cannot be validated through typed construction or schema-first
  serialized validation.

### Slice C - `PerViewScope` And Serialized View-Family Loop

Status: `planned`

Purpose:

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

Validation:

```powershell
cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.SceneRendererPublication.Tests Oxygen.Vortex.SceneRendererDeferredCore.Tests Oxygen.Vortex.RenderContext.Tests --parallel 4
ctest --preset test-debug -R "Oxygen\.Vortex\.(SceneRendererPublication|SceneRendererDeferredCore|RenderContext)" --output-on-failure
$cursorWrites = @(rg -n "current_view\s*=|active_view_index\s*=" src\Oxygen\Vortex --type cpp -g '!*Test*' -g '!*PerViewScope*'); if ($LASTEXITCODE -gt 1) { throw "rg cursor-write scan failed" }; if ($cursorWrites.Count -gt 0) { $cursorWrites; throw "Unexpected RenderContext cursor writes outside PerViewScope" }
cmake --build out\build-ninja --config Debug --target oxygen-examples-renderscene --parallel 4
.\out\build-ninja\bin\Debug\Oxygen.Examples.RenderScene.exe --frames 4 --fps 30 --vsync false --capture-provider off
git diff --check
```

The `rg` gate must return no production writes outside `PerViewScope`. The
RenderScene smoke is a positive check that the existing single-view runtime path
now delegates through `RenderViewFamily(batch_of_one)` without changing the
observable single-view behavior. `--capture-provider off` is the documented
RenderScene no-capture switch.

Rollback/replan trigger:

- Product publication still behaves as last-view-wins after two serialized
  views render.

### Slice D - Scene Texture Lease Pool

Status: `planned`

Purpose:

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

Validation:

```powershell
cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.SceneTextures.Tests Oxygen.Vortex.SceneTextureLeasePool.Tests Oxygen.Vortex.SceneRendererPublication.Tests Oxygen.Vortex.SceneRendererDeferredCore.Tests --parallel 4
ctest --preset test-debug -R "Oxygen\.Vortex\.(SceneTextures|SceneTextureLeasePool|SceneRendererPublication|SceneRendererDeferredCore)" --output-on-failure
git diff --check
```

The slice-D lease-pool tests must include a small N=10 frame harness with a
warmup window and assert that allocation count after warmup equals the expected
pool size for the observed descriptor keys. This local metric catches churn
before the full slice-H runtime proof exists.

Rollback/replan trigger:

- The pool cannot express existing `SceneTextures` rebuild/publication
  contracts without duplicated resource ownership.

### Slice E - Data-Driven Surface Composition

Status: `planned`

Purpose:

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

Validation:

```powershell
cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.CompositionPlanner.Tests Oxygen.Vortex.RendererCompositionQueue.Tests Oxygen.Vortex.SceneTextureLeasePool.Tests --parallel 4
ctest --preset test-debug -R "Oxygen\.Vortex\.(CompositionPlanner|RendererCompositionQueue|SceneTextureLeasePool)" --output-on-failure
git diff --check
```

Composition tests must prove the structural fast-copy predicate is reachable
from a non-`kZOrderScene` layer configuration; otherwise the old primary-view
path is still the only tested path.

Rollback/replan trigger:

- Composition still needs special PiP or primary-view code after surface routes
  are modeled.

### Slice F - Auxiliary Views And Dependency Graph

Status: `planned`

Purpose:

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

Validation:

```powershell
cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.AuxiliaryDependencyGraph.Tests Oxygen.Vortex.CompositionPlanner.Tests Oxygen.Vortex.SceneRendererDeferredCore.Tests --parallel 4
ctest --preset test-debug -R "Oxygen\.Vortex\.(AuxiliaryDependencyGraph|CompositionPlanner|SceneRendererDeferredCore)" --output-on-failure
git diff --check
```

If the dependency graph implementation lives inside existing planner code, the
dedicated target may wrap that code directly. The tests must cover missing
required producer, duplicate producer, optional missing producer, in-batch
ordering, cross-batch ordering, cycle rejection, and typed invalid consumer
binding.

Rollback/replan trigger:

- The first same-frame consumer requires raw framebuffer pointer leakage or
  stringly typed stage payloads.

### Slice G - Overlay Lanes And View Extensions

Status: `planned`

Purpose:

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

Validation:

```powershell
cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.CompositionPlanner.Tests Oxygen.Vortex.RendererCompositionQueue.Tests --parallel 4
ctest --preset test-debug -R "Oxygen\.Vortex\.(CompositionPlanner|RendererCompositionQueue)" --output-on-failure
git diff --check
```

Rollback/replan trigger:

- Overlay code needs to mutate stage internals directly or cache
  `RenderContext::current_view` outside `PerViewScope`.

### Slice H - Runtime Validation Scene And Scripts

Status: `planned`

Purpose:

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
- Analyzer asserts per-view labels, distinct outputs, surface layer order,
  auxiliary extraction before consumption, and allocation churn bounds.
- CDB gate fails any D3D12/DXGI validation message severity `WARNING` or
  higher unless allow-listed.

Validation:

```powershell
powershell -NoProfile -Command "$ps = [System.Management.Automation.PSParser]::Tokenize((Get-Content tools\vortex\Run-VortexMultiViewValidation.ps1 -Raw), [ref]$null); $ps.Count | Out-Null"
powershell -NoProfile -Command "$ps = [System.Management.Automation.PSParser]::Tokenize((Get-Content tools\vortex\Assert-VortexMultiViewProof.ps1 -Raw), [ref]$null); $ps.Count | Out-Null"
python -m py_compile tools\vortex\AnalyzeRenderDocVortexMultiView.py
git diff --check
```

Runtime closure command shape:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\vortex\Run-VortexMultiViewValidation.ps1 -Output out\build-ninja\analysis\vortex\m06a-multiview -Frame 5 -RunFrames 65 -Fps 30 -BuildJobs 4
```

Rollback/replan trigger:

- The proof scene cannot isolate per-view render modes or products clearly
  enough for script assertions.

### Slice I - Closure And Ledger Update

Status: `planned`

Purpose:

- Close the milestone only after implementation, docs, tests, CDB, RenderDoc,
  allocation-churn proof, and any required user visual confirmation exist.

Primary files:

- `design/vortex/IMPLEMENTATION_STATUS.md`
- `design/vortex/PLAN.md`
- `design/vortex/plan/VTX-M06A-multi-view-proof-closeout.md`
- `design/vortex/lld/multi-view-composition.md` if implementation finds
  approved divergences

Validation:

- All commands listed in sections 9 and 10 have passing evidence.
- `git diff --check`.

Rollback/replan trigger:

- Any acceptance-gate item remains unproven or deferred without explicit human
  approval recorded in status docs.

## 9. Test Plan

Focused unit/integration targets, adjusted to final target names if CMake
wiring changes:

```powershell
cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.RenderContext.Tests Oxygen.Vortex.PreviousViewHistoryCache.Tests Oxygen.Vortex.SceneTextures.Tests Oxygen.Vortex.SceneTextureLeasePool.Tests Oxygen.Vortex.CompositionPlanner.Tests Oxygen.Vortex.AuxiliaryDependencyGraph.Tests Oxygen.Vortex.RendererCompositionQueue.Tests Oxygen.Vortex.SceneRendererPublication.Tests Oxygen.Vortex.SceneRendererDeferredCore.Tests --parallel 4
ctest --preset test-debug -R "Oxygen\.Vortex\.(RenderContext|PreviousViewHistoryCache|SceneTextures|SceneTextureLeasePool|CompositionPlanner|AuxiliaryDependencyGraph|RendererCompositionQueue|SceneRendererPublication|SceneRendererDeferredCore)" --output-on-failure
```

Required test coverage before closure:

- Four active scene views produce four packets and four independent effective
  render plans.
- Wireframe/debug/depth/tone-map state does not leak between views.
- Exposure sharing uses previous-frame state handle data and has documented
  fallback diagnostics.
- `ViewId` reuse with a new `ViewStateHandle` does not reuse history.
- Descriptor-key changes invalidate incompatible persistent histories.
- `PerViewScope` selects and restores view state; nested scope fails in debug.
- Two views with the same scene-texture key get distinct live leases unless
  serialized and released.
- Lease pool rejects active aliasing and bounded exhaustion.
- Leases that differ only by queue affinity do not share a pool entry.
- Scene texture bindings and view frame bindings remain per view until
  composition consumes them.
- Shadow-enabled plus shadow-disabled views prove union build/per-view consume.
- Composition routes one view to two surfaces and two views to one surface.
- Offscreen surface output can be consumed as `ShaderResource` by a next-frame
  material or stage.
- Auxiliary producer/consumer dependency sorting works in one batch and across
  batches.
- Overlay lanes sort by lane and remain scoped to their target view/surface.
- Serialized validation payloads reject invalid render mode, feature flag,
  overlay lane, route, and auxiliary IO values before C++ conversion.

If shader bytecode, shader catalog entries, HLSL ABI, or root bindings change:

```powershell
cmake --build out\build-ninja --config Debug --target oxygen-graphics-direct3d12_shaders Oxygen.Graphics.Direct3D12.ShaderBakeCatalog.Tests --parallel 4
ctest --preset test-debug -R "Oxygen\.Graphics\.Direct3D12\.ShaderBakeCatalog" --output-on-failure
```

## 10. Runtime / Capture Proof

Runtime closure requires one focused multi-view proof scene or harness that is
constructed to expose the M06A contract:

- four visible viewports in one surface:
  - lit perspective
  - wireframe top/orthographic
  - normal/base-color/debug view
  - shadow-mask or other product-dependent view
- at least one PiP-style layer represented by the same surface-plan machinery
  used for the grid.
- at least one same-frame auxiliary producer/consumer path, unless explicitly
  deferred with human approval and recorded in status docs.
- one feature disabled for a view while enabled for another, with explicit
  empty product publication for the disabled view.
- no sun/atmosphere/light setup that hides the validation signal; authored
  lighting must make per-view differences and shadow/product behavior visible.

Runtime command shape:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\vortex\Run-VortexMultiViewValidation.ps1 -Output out\build-ninja\analysis\vortex\m06a-multiview -Frame 5 -RunFrames 65 -Fps 30 -BuildJobs 4
```

The generated proof package must contain:

- runtime log
- CDB/debug-layer report
- RenderDoc capture path
- RenderDoc analyzer key/value report
- allocation-churn report for at least 60 frames with 5-frame warmup
- optional user visual confirmation note if visual quality is part of the
  closure claim

CDB/debug-layer pass criteria:

- runtime exit code `0`
- no debugger break
- zero D3D12/DXGI validation messages of severity `WARNING` or higher unless
  explicitly allow-listed in `tools/vortex/multiview_cdb_allow.json`
- no present-without-presentable-state
- no mismatched resource state, untracked transition, descriptor lifetime, or
  live-object leak not covered by the allow-list

RenderDoc analyzer pass criteria:

- `Vortex.View[...]` labels exist for every active scene view.
- Every event label produced under the multi-view harness matches
  `^Vortex\.(View|Surface|AuxView)\[[^\]]+\]\..+$`.
- repeated scene stages are labeled by view and do not collapse into
  last-view-wins records.
- every requested view output exists and is consumed by the expected surface
  layer.
- final composition order matches the surface plan.
- per-view render/debug modes differ as authored.
- disabled feature products are explicitly empty for disabled views and valid
  for enabled views.
- auxiliary outputs are extracted before dependent consumers.
- steady-state scene-texture family allocations after warmup are `0`; if a new
  descriptor key appears after warmup, additional allocations are
  `<= count(new distinct descriptor keys)`.

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
  approval recorded in this plan and `IMPLEMENTATION_STATUS.md`.
- In-batch and cross-batch auxiliary dependency ordering is proven by tests and
  capture analysis, unless explicitly deferred with human approval.
- Unit/integration tests pass.
- ShaderBake/catalog validation is recorded if shader/ABI changes occurred.
- CDB/debug-layer audit passes.
- RenderDoc scripted analysis passes.
- Required docs and the single VTX-M06A ledger row are updated with evidence.
- Residual gaps are listed as blockers or explicitly accepted/deferred by the
  human reviewer.

If any item lacks evidence, VTX-M06A remains `in_progress` or `planned`; no
completion claim is allowed.

## 12. Replan Triggers

Stop implementation claims and update the LLD/plan first if any of these occur:

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

## 13. Status Update Requirements

During implementation:

- Keep this plan's slice status current, but do not mark the milestone
  validated here.
- Slice-by-slice evidence accumulates in each slice's `Current evidence`
  subsection in this plan while work is underway; the final closure summary
  lives only in the single `VTX-M06A` ledger row.
- Update exactly the `VTX-M06A` row in
  [../IMPLEMENTATION_STATUS.md](../IMPLEMENTATION_STATUS.md) when a slice has
  implementation and validation evidence.
- Record files/areas changed, commands run, result counts, artifact paths, UE5.7
  references checked, and remaining gaps.
- Do not append per-commit evidence rows.
- Do not claim closure until section 11 evidence is present.
- If a gap is accepted by the human reviewer, record the approval and the
  reason in both this plan and the `VTX-M06A` status row.
- Keep [../PLAN.md](../PLAN.md) at `planned` until implementation starts, then
  `in_progress`; only set `validated` after the exit gate is proven.
