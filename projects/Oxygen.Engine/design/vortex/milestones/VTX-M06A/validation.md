# Slice A - Plan And Status Truth Surface — recorded evidence

Recorded evidence:

- This plan exists and is linked from `PLAN.md`, `milestone README`,
  and `PLAN.md`.
- First plan-review feedback is incorporated: Slice B is split into B1/B2,
  schema/auxiliary sequencing is explicit, and slice-local proof gates cover
  history handles, `PerViewScope`, lease churn, composition routing, auxiliary
  dependencies, and overlays.
- Docs-only validation passed with `git diff --check`; no build/runtime tests
  are implied by this planning slice.

## Slice B1 - Per-View Plan And State Handles — recorded evidence

Recorded evidence:

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

## Slice B2 - View Kind, Feature Mask, Routes, And Payload Classification — recorded evidence

Recorded evidence:

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

## Slice C - `PerViewScope` And Serialized View-Family Loop — recorded evidence

Recorded evidence:

- Implemented `internal::PerViewScope` as the only production writer for
  `RenderContext::current_view` selection and `active_view_index`.
- `Renderer::PopulateRenderContextViewState` now materializes frame entries
  without selecting a current cursor. `SceneRenderer::RenderViewFamily`
  iterates scene-view entries, enters `PerViewScope`, binds the prepared frame,
  resets per-view scene products while reusing the existing single
  `SceneTextures` family, and publishes pre/post per-view bindings through
  renderer helpers.
- `SceneRenderer::OnRender` delegates production frame-view batches through
  `RenderViewFamily`; existing single-current-view harness calls remain
  supported for focused tests/tools.
- Added focused coverage in `RenderContext.Tests`,
  `SceneRendererDeferredCore.Tests`, and updated
  `SceneRendererPublication.Tests` for no-eager-cursor materialization.
- Validation passed:
  `cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.SceneRendererPublication.Tests Oxygen.Vortex.SceneRendererDeferredCore.Tests Oxygen.Vortex.RenderContext.Tests --parallel 4`;
  `ctest --preset test-debug -R "Oxygen\.Vortex\.(SceneRendererPublication|SceneRendererDeferredCore|RenderContext)" --output-on-failure`
  with 4/4 matched targets passing (`RenderContextMaterializer` also matched
  the regex); cursor-write gate found no production writes outside
  `PerViewScope`; `cmake --build out\build-ninja --config Debug --target oxygen-examples-renderscene --parallel 4`
  passed; `.\out\build-ninja\bin\Debug\Oxygen.Examples.RenderScene.exe --frames 4 --fps 30 --vsync false --capture-provider off`
  exited 0 and reached `Engine completed after Frame(seq:4) frames`; the
  existing shutdown-time `Vortex.PointShadowCubeSurface` resource-registry note
  remains recorded as outside slice C closure proof; `git diff --check` passed
  with line-ending warnings only.
- No open Slice C closure gap. Later slices validated the scene-texture lease
  pool, data-driven surface composition, auxiliary graph, overlays, runtime
  proof scripts, CDB/debug-layer audit, RenderDoc scripted proof,
  allocation-churn proof, and final closure.

## Slice D - Scene Texture Lease Pool — recorded evidence

Recorded evidence:

- Implemented `SceneTextureLeaseKey`, `SceneTextureLease`, and
  `SceneTextureLeasePool` as a Vortex-native descriptor-keyed owner for concrete
  `SceneTextures` families. The key covers the current concrete allocation
  contract plus M06A placeholders for render scale, HDR/debug/editor primitive
  requirements, depth convention, and queue affinity.
- `SceneRenderer::RenderViewFamily` now acquires an exclusive lease per
  serialized scene view, routes stage execution through the leased
  `SceneTextures` family, extracts/publishes products before the lease leaves
  scope, and preserves the public `GetSceneTextures()` inspection contract by
  exposing the most recent rendered family.
- Added focused `SceneTextureLeasePool.Tests` coverage for same-key reuse,
  simultaneous same-key separation, explicit exhaustion, queue-affinity keying,
  and a 10-frame two-key warmup harness proving zero allocations after the
  warmup window.
- Validation passed:
  `cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.SceneTextures.Tests Oxygen.Vortex.SceneTextureLeasePool.Tests Oxygen.Vortex.SceneRendererPublication.Tests Oxygen.Vortex.SceneRendererDeferredCore.Tests --parallel 4`;
  `ctest --preset test-debug -R "Oxygen\.Vortex\.(SceneTextures|SceneTextureLeasePool|SceneRendererPublication|SceneRendererDeferredCore)" --output-on-failure`
  with 4/4 matched targets passing; cursor-write gate found no production
  writes outside `PerViewScope`; RenderScene Debug build passed; RenderScene
  4-frame smoke exited 0; and `git diff --check` passed with line-ending
  warnings only.
- No open Slice D closure gap. Later slices validated data-driven surface
  composition, auxiliary graph, overlays, runtime proof scripts,
  CDB/debug-layer audit, RenderDoc scripted proof, 60-frame allocation-churn
  proof, and final closure.

## Slice E - Data-Driven Surface Composition — recorded evidence

Recorded evidence:

- `CompositionPlanner` now builds route-aware layer plans first, then lowers
  the selected surface route into composition tasks. Empty routes still target
  the default surface using the view viewport.
- The full-surface fast-copy predicate is structural: an opaque layer whose
  source format matches the destination target and whose destination covers the
  target becomes a copy without checking for `kZOrderScene` or a primary view
  id. Other layers remain texture blends unless the route explicitly requests
  copy.
- `CompositionSubmission` and `CompositingTask` now carry deterministic surface
  and layer debug names, and renderer composition task labels consume the task
  debug name when present.
- Focused tests prove non-scene full-surface copy, partial-opacity overlay
  blending, surface-route filtering, deterministic z/submission ordering, and
  that the slice-D lease-pool warmup metric remains green.
- Validation passed:
  `cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.CompositionPlanner.Tests Oxygen.Vortex.RendererCompositionQueue.Tests Oxygen.Vortex.SceneTextureLeasePool.Tests --parallel 4`;
  `ctest --preset test-debug -R "Oxygen\.Vortex\.(CompositionPlanner|RendererCompositionQueue|SceneTextureLeasePool)" --output-on-failure`
  with 3/3 matched targets passing; and `git diff --check` passed with
  line-ending warnings only.
- No open Slice E closure gap. Later slices validated the auxiliary dependency
  graph, overlay lanes/extensions, runtime proof scripts, CDB/debug-layer audit,
  RenderDoc scripted proof, 60-frame allocation-churn proof, and final closure.

## Slice F - Auxiliary Views And Dependency Graph — recorded evidence

Recorded evidence:

- Added a Vortex-native `AuxiliaryDependencyGraph` used by
  `FramePlanBuilder` after frame-packet materialization and before GPU work.
  The resolver validates unique typed producers, required inputs, expected
  input kind, and dependency cycles, then topologically reorders packets so
  auxiliary producers render before consumers.
- `AuxInputDesc` now carries an expected `AuxOutputKind`, allowing optional
  missing inputs to publish a typed invalid `AuxiliaryResolvedInput` instead
  of requiring consumers to branch on raw pointer sentinels.
- `FrameViewPacket` now carries resolved auxiliary inputs with explicit
  `valid`, `kind`, `producer_view_id`, producer packet index, and deterministic
  debug name fields. Full material/stage consumption of those bindings remains
  runtime proof/future consumer work, but the graph-facing consumer contract is
  typed and validated.
- UE5.7 parity reference was rechecked locally: custom-render-pass views are
  appended to the view family and then to `AllViews` in
  `SceneRendering.cpp:3005-3040`; late single-view creation appends to
  `ViewFamily->Views` at `SceneRendering.cpp:5022-5030`; `SceneView.h:2308-2311`
  documents the `Views` / `AllViews` split that keeps auxiliary/scene-capture
  style views in the frame family without treating them as the primary view.
- Focused tests prove missing required producer failure, duplicate producer
  failure, optional missing typed-invalid binding, producer-before-consumer
  ordering, kind mismatch failure, and cycle rejection. Existing composition
  and deferred-core tests stayed green.
- Validation passed:
  `cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.AuxiliaryDependencyGraph.Tests Oxygen.Vortex.CompositionPlanner.Tests Oxygen.Vortex.SceneRendererDeferredCore.Tests --parallel 4`;
  `ctest --preset test-debug -R "Oxygen\.Vortex\.(AuxiliaryDependencyGraph|CompositionPlanner|SceneRendererDeferredCore)" --output-on-failure`
  with 3/3 matched targets passing; and `git diff --check` passed with
  line-ending warnings only.
- No open Slice F closure gap. Later slices validated runtime proof scripts,
  CDB/debug-layer audit, RenderDoc scripted proof, 60-frame allocation-churn
  proof, and final closure.

## Slice G - Overlay Lanes And View Extensions — recorded evidence

Recorded evidence:

- Added typed overlay lane payloads on `CompositionView`, including reserved
  world-depth-aware/world-foreground view-target batches and surface-target
  screen overlay batches. Existing `CompositionView::on_overlay` is now
  converted into a typed `kViewScreen` overlay batch during
  `FrameViewPacket` materialization.
- Added `IViewExtension` with typed `OnFamilyAssembled`, `OnViewSetup`,
  `OnPreRenderViewGpu`, `OnPostRenderViewGpu`, and `OnPostComposition`
  hooks. Renderer Core snapshots registered extensions and dispatches the
  hooks at the family, per-view, and post-surface-composition points without
  re-entrant scene rendering or direct stage mutation.
- `CompositionPlanner` now routes view/surface screen overlay batches into
  `CompositionSubmission::surface_overlays` per surface route while preserving
  world-depth-aware/world-foreground overlays as view-target placeholders.
  Renderer Core executes surface overlays after all surface composition layers
  and before presentable handoff, then runs `OnPostComposition` on the same
  graphics-queue recorder before the present barrier.
- Focused tests cover typed `on_overlay` conversion, reserved world-depth-aware
  placeholder batches, surface-overlay execution order before presentable
  handoff, and `OnPostComposition` hook ordering before presentable handoff.
- Validation passed:
  `cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.CompositionPlanner.Tests Oxygen.Vortex.RendererCompositionQueue.Tests --parallel 4`;
  `ctest --preset test-debug -R "Oxygen\.Vortex\.(CompositionPlanner|RendererCompositionQueue)" --output-on-failure`
  with 2/2 matched targets passing; and `git diff --check` passed with
  line-ending warnings only.
- No open Slice G closure gap. Later slices validated runtime validation
  scripts, CDB/debug-layer audit, RenderDoc scripted analysis,
  60-frame allocation-churn proof, and final ledger closure.

## Slice H - Runtime Validation Scene And Scripts — recorded evidence

Recorded evidence:

- Implementation committed in `3dee293e3 test(vortex): add multiview proof
validation`: `Examples/MultiView` exposes `--proof-layout true`, the
  four-view proof layout, and proof scripts/analyzer/schema under
  `tools/vortex`.
- Follow-up cleanup fixed proof-script strict-mode failures, aligned the
  RenderDoc analyzer with the authored four-view proof layout, and added
  scene-texture lease-pool churn telemetry to the runtime proof.
- Manual visual confirmation on 2026-04-28 approved the GroundGrid stability
  correction after smooth-motion state was made per `ViewId`.
- Validation passed:
  `powershell -NoProfile -Command '$parseErrors = $null; $ps = [System.Management.Automation.PSParser]::Tokenize((Get-Content tools\vortex\Run-VortexMultiViewValidation.ps1 -Raw), [ref]$parseErrors); if ($parseErrors -and $parseErrors.Count -gt 0) { $parseErrors | Format-List; exit 1 }; $ps.Count | Out-Null'`,
  the equivalent parser check for `Assert-VortexMultiViewProof.ps1`, and
  `python -m py_compile tools\vortex\AnalyzeRenderDocVortexMultiView.py`.
- Full runtime proof passed:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools\vortex\Run-VortexMultiViewValidation.ps1 -Output out\build-ninja\analysis\vortex\m06a-multiview\multiview-proof -Frame 5 -RunFrames 65 -Fps 30 -BuildJobs 4`.
  Generated reports:
  `multiview-proof.debug-layer.report.txt` (`overall_verdict=pass`,
  `runtime_exit_code=0`, `d3d12_error_count=0`, `dxgi_error_count=0`,
  `blocking_warning_count=0`),
  `multiview-proof.renderdoc.txt` (`overall_verdict=true`,
  `stage9_scope_count=4`, `stage3_scope_count=3`,
  `stage12_scope_count=3`, `composition_view_ids=1,2,3,4`,
  `aux_consume_scope_count=1`, `aux_consume_copy_count=1`,
  `aux_consume_before_composition=true`),
  `multiview-proof.allocation-churn.txt` (`run_frames=65`,
  `steady_state_frame_count=60`,
  `steady_state_allocations_after_warmup=0`), and
  `multiview-proof.validation.txt` (`overall_verdict=pass`).
- Auxiliary producer/consumer runtime proof passed:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools\vortex\Run-VortexMultiViewValidation.ps1 -AuxProofLayout -Output out\build-ninja\analysis\vortex\m06a-multiview\multiview-aux-proof -Frame 5 -RunFrames 65 -Fps 30 -BuildJobs 4`.
  Generated reports:
  `multiview-aux-proof.debug-layer.report.txt` (`overall_verdict=pass`,
  `runtime_exit_code=0`, `d3d12_error_count=0`, `dxgi_error_count=0`,
  `blocking_warning_count=0`),
  `multiview-aux-proof.renderdoc.txt` (`overall_verdict=true`,
  `stage9_scope_count=4`, `stage3_scope_count=3`,
  `stage12_scope_count=3`, `composition_view_ids=1,2,3,4`,
  `aux_consume_scope_count=1`, `aux_consume_copy_count=1`,
  `stage9_before_aux_consume=true`,
  `aux_consume_before_composition=true`),
  `multiview-aux-proof.allocation-churn.txt` (`run_frames=65`,
  `steady_state_frame_count=60`,
  `steady_state_allocations_after_warmup=0`), and
  `multiview-aux-proof.validation.txt` (`overall_verdict=pass`).
- Focused section 9 validation passed:
  `cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.RenderContext.Tests Oxygen.Vortex.PreviousViewHistoryCache.Tests Oxygen.Vortex.SceneTextures.Tests Oxygen.Vortex.SceneTextureLeasePool.Tests Oxygen.Vortex.CompositionPlanner.Tests Oxygen.Vortex.AuxiliaryDependencyGraph.Tests Oxygen.Vortex.RendererCompositionQueue.Tests Oxygen.Vortex.SceneRendererPublication.Tests Oxygen.Vortex.SceneRendererDeferredCore.Tests --parallel 4`
  and
  `ctest --preset test-debug -R "Oxygen\.Vortex\.(RenderContext|PreviousViewHistoryCache|SceneTextures|SceneTextureLeasePool|CompositionPlanner|AuxiliaryDependencyGraph|RendererCompositionQueue|SceneRendererPublication|SceneRendererDeferredCore)" --output-on-failure`
  with 10/10 matched tests passing.
- ShaderBake/catalog validation was not run because no shader bytecode, shader
  catalog, HLSL ABI, or root-binding files changed.
- Remaining VTX-M06A exit-gate gap: none. Offscreen-only proof remains VTX-M06B
  and feature-gated runtime variants remain VTX-M06C.

## Validation and remaining work

**Qualification:** `validated`

B1-H implementation exists. B1: per-view `ViewRenderSettings`, producer-owned `ViewStateHandle`, packet-level state-handle copy, packet-owned effective shader-debug mode, handle-keyed `PreviousViewHistoryCache`, ViewStateHandle-keyed exposure state, and removal of `FramePlanBuilder` frame-global render/debug fields. B2: runtime C++ payload types for `ViewKind`, `ViewFeatureMask`, `ViewSurfaceRoute`, `OverlayPolicy`, auxiliary IO descriptors, factory classification, packet copies, and `FramePlanBuilder` view-kind validation. C: `PerViewScope`, no-eager-cursor frame entry materialization, serialized `SceneRenderer::RenderViewFamily`, per-view scene-product reset while reusing the existing `SceneTextures` family, and pre/post per-view binding publication. D: descriptor-keyed `SceneTextureLeasePool`, exclusive per-view leases routed through `RenderViewFamily`, explicit exhaustion, queue-affinity keying, and focused allocation-churn coverage. E: route-aware layer planning, structural full-surface copy selection independent of `kZOrderScene`/primary id, deterministic surface/layer debug names, and filtered surface submissions. F: typed `AuxiliaryDependencyGraph`, runtime publication/materialization of auxiliary descriptors, required producer resolution, producer-before-consumer ordering, extracted color-product publication, and dependent view GPU consumption. G: typed overlay batches, `on_overlay` to `kViewScreen` compatibility conversion, reserved world-depth-aware/world-foreground view overlay placeholders, surface overlay execution before presentable handoff, and typed `IViewExtension` hooks including `OnPostComposition`. H: `Examples/MultiView --proof-layout true`, `--aux-proof-layout true`, CDB/debug-layer audit script, RenderDoc analyzer, assertion script, allow-list, validation schema, and scene-texture lease-pool runtime churn telemetry are implemented. Latest validation passed proof-script parser checks, `AnalyzeRenderDocVortexMultiView.py` py-compile, standard runtime proof `Run-VortexMultiViewValidation.ps1 -Output out\build-ninja\analysis\vortex\m06a-multiview\multiview-proof -Frame 5 -RunFrames 65 -Fps 30 -BuildJobs 4`, auxiliary runtime proof `Run-VortexMultiViewValidation.ps1 -AuxProofLayout -Output out\build-ninja\analysis\vortex\m06a-multiview\multiview-aux-proof -Frame 5 -RunFrames 65 -Fps 30 -BuildJobs 4`, CDB reports with `overall_verdict=pass`, runtime exit `0`, `d3d12_error_count=0`, `dxgi_error_count=0`, and `blocking_warning_count=0`, RenderDoc reports with `overall_verdict=true`, `stage9_scope_count=4`, `composition_view_ids=1,2,3,4`, `aux_consume_scope_count=1`, `aux_consume_copy_count=1`, and `aux_consume_before_composition=true`, allocation reports with `steady_state_frame_count=60` and `steady_state_allocations_after_warmup=0`, focused section 9 build/ctest with 10/10 matched tests passing, and manual visual confirmation on 2026-04-28 that GroundGrid stability is corrected. ShaderBake/catalog validation was not run because no shader bytecode, catalog, HLSL ABI, or root-binding files changed.

**Remaining work:** No open VTX-M06A closure gap. Offscreen-only proof remains VTX-M06B; feature-gated runtime variants remain VTX-M06C.

## 4. Current State

| Area                | Current state                                                                                                                                                                                            | M06A action                                                                                                                                      |
| ------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------ |
| Roadmap/status      | `PLAN.md` and `milestone README` listed `VTX-M06A` as active after B1-C landed.                                                                                                                          | During execution, keep status unvalidated until the full exit gate is proven; update evidence in place after each verified slice.                |
| LLD                 | `multi-view-composition.md` is review-addressed and implementation is in progress.                                                                                                                       | Treat as the implementation contract; update it first if implementation discovers scope drift.                                                   |
| Runtime view intent | `CompositionView` has `ViewId`, name, view, z-order, opacity, camera, HDR, environment toggles, exposure source id, force-wireframe, shading override, and overlay callback.                             | Extend without breaking existing single-view clients; add typed settings instead of more ad hoc booleans.                                        |
| View lifecycle      | `ViewLifecycleService` materializes and sorts active `CompositionViewImpl` instances and currently validates exposure with same-frame ordering.                                                          | Move exposure sharing to previous-frame `ViewStateHandle` semantics and retain deterministic active-view ordering.                               |
| Frame packets       | `FrameViewPacket` carries a `CompositionViewImpl`, published `ViewId`, `ViewRenderPlan`, and composite texture helpers.                                                                                  | Slice B1 makes plan/state authoritative; slice B2 adds kind, feature mask, route placeholders, auxiliary IO placeholders, and scene-texture key. |
| Frame plan          | `FramePlanBuilder` builds packets but still keeps frame-global render/debug state accessors.                                                                                                             | Slice B1 removes stage-readable global effective mode and makes packets authoritative.                                                           |
| Render context      | `RenderContext` has `frame_views`, `active_view_index`, and `current_view`; production cursor selection is now constrained by `PerViewScope`.                                                            | Keep new cursor writes scoped; slice D must preserve this contract while adding leases.                                                          |
| SceneRenderer       | `SceneRenderer::RenderViewFamily` serializes eligible scene views through `PerViewScope`; `OnRender` delegates to it for production frame-view batches and retains the single-current-view harness path. | Slice D adds descriptor-keyed lease/pool under the same family loop.                                                                             |
| SceneTextures       | `SceneRenderer` still owns one concrete `SceneTextures` family and slice C resets per-view scene products while serializing through it.                                                                  | Slice D adds descriptor-keyed lease/pool.                                                                                                        |
| Composition         | `CompositionPlanner` has single-surface/layer behavior and still contains primary/z-order assumptions.                                                                                                   | Replace with structural surface plans and deterministic layers.                                                                                  |
| Product publishers  | Several services already use per-view publishers or view-keyed inspection surfaces.                                                                                                                      | Audit each touched product for last-view-wins behavior and explicit disabled products.                                                           |
| Exposure/history    | `PreviousViewHistoryCache` is keyed by `ViewId`; `ExposurePass` uses view id and current selected source.                                                                                                | Route histories through `ViewStateHandle` or make stateless where no handle is provided.                                                         |
| Existing demo       | [Examples/MultiView](../../../../Examples/MultiView/MainModule.cpp) exists, but current composition remains app/demo-shaped and not the Vortex closure proof.                                            | Reuse or adapt only if it exercises the Vortex-native path and proof tooling.                                                                    |

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
powershell -NoProfile -ExecutionPolicy Bypass -File tools\vortex\Run-VortexMultiViewValidation.ps1 -Output out\build-ninja\analysis\vortex\m06a-multiview\multiview-proof -Frame 5 -RunFrames 65 -Fps 30 -BuildJobs 4
powershell -NoProfile -ExecutionPolicy Bypass -File tools\vortex\Run-VortexMultiViewValidation.ps1 -AuxProofLayout -Output out\build-ninja\analysis\vortex\m06a-multiview\multiview-aux-proof -Frame 5 -RunFrames 65 -Fps 30 -BuildJobs 4
```

The generated proof package must contain:

- runtime log
- CDB/debug-layer report
- RenderDoc capture path
- RenderDoc analyzer key/value report
- allocation-churn report for at least 60 frames with 5-frame warmup
- optional manual visual confirmation note if visual quality is part of the
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
- the auxiliary proof layout includes a required same-frame color texture
  dependency submitted consumer-first; runtime resolution must order the
  producer before the consumer, extract the producer output, and record a
  `Vortex.AuxView.Consume` copy into the consumer output before final surface
  composition.
- steady-state scene-texture family allocations after warmup are `0`; if a new
  descriptor key appears after warmup, additional allocations are
  `<= count(new distinct descriptor keys)`.

## Slice A - Plan And Status Truth Surface — checks

```powershell
git diff --check -- design\vortex\milestones\VTX-M06A\README.md design\vortex\PLAN.md design\vortex\PLAN.md design\vortex\PLAN.md design\vortex\lld\multi-view-composition.md
rg -n "VTX-M06A|multi-view proof closeout|VTX-M06A-multi-view-proof-closeout" design\vortex
```

## Slice B1 - Per-View Plan And State Handles — checks

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

## Slice B2 - View Kind, Feature Mask, Routes, And Payload Classification — checks

```powershell
cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.RenderContext.Tests Oxygen.Vortex.CompositionPlanner.Tests Oxygen.Vortex.SceneRendererDeferredCore.Tests --parallel 4
ctest --preset test-debug -R "Oxygen\.Vortex\.(RenderContext|CompositionPlanner|SceneRendererDeferredCore)\.Tests$" --output-on-failure
git diff --check
```

## Slice C - `PerViewScope` And Serialized View-Family Loop — checks

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

## Slice D - Scene Texture Lease Pool — checks

```powershell
cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.SceneTextures.Tests Oxygen.Vortex.SceneTextureLeasePool.Tests Oxygen.Vortex.SceneRendererPublication.Tests Oxygen.Vortex.SceneRendererDeferredCore.Tests --parallel 4
ctest --preset test-debug -R "Oxygen\.Vortex\.(SceneTextures|SceneTextureLeasePool|SceneRendererPublication|SceneRendererDeferredCore)" --output-on-failure
git diff --check
```

The slice-D lease-pool tests must include a small N=10 frame harness with a
warmup window and assert that allocation count after warmup equals the expected
pool size for the observed descriptor keys. This local metric catches churn
before the full slice-H runtime proof exists.

## Slice E - Data-Driven Surface Composition — checks

```powershell
cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.CompositionPlanner.Tests Oxygen.Vortex.RendererCompositionQueue.Tests Oxygen.Vortex.SceneTextureLeasePool.Tests --parallel 4
ctest --preset test-debug -R "Oxygen\.Vortex\.(CompositionPlanner|RendererCompositionQueue|SceneTextureLeasePool)" --output-on-failure
git diff --check
```

Composition tests must prove the structural fast-copy predicate is reachable
from a non-`kZOrderScene` layer configuration; otherwise the old primary-view
path is still the only tested path.

## Slice F - Auxiliary Views And Dependency Graph — checks

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

## Slice G - Overlay Lanes And View Extensions — checks

```powershell
cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.CompositionPlanner.Tests Oxygen.Vortex.RendererCompositionQueue.Tests --parallel 4
ctest --preset test-debug -R "Oxygen\.Vortex\.(CompositionPlanner|RendererCompositionQueue)" --output-on-failure
git diff --check
```

## Slice H - Runtime Validation Scene And Scripts — checks

```powershell
powershell -NoProfile -Command "$ps = [System.Management.Automation.PSParser]::Tokenize((Get-Content tools\vortex\Run-VortexMultiViewValidation.ps1 -Raw), [ref]$null); $ps.Count | Out-Null"
powershell -NoProfile -Command "$ps = [System.Management.Automation.PSParser]::Tokenize((Get-Content tools\vortex\Assert-VortexMultiViewProof.ps1 -Raw), [ref]$null); $ps.Count | Out-Null"
python -m py_compile tools\vortex\AnalyzeRenderDocVortexMultiView.py
git diff --check
```

## Slice H - Runtime Validation Scene And Scripts — checks (2)

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\vortex\Run-VortexMultiViewValidation.ps1 -Output out\build-ninja\analysis\vortex\m06a-multiview -Frame 5 -RunFrames 65 -Fps 30 -BuildJobs 4
```

## Slice I - Closure And Ledger Update — checks

- All commands listed in sections 9 and 10 have passing evidence.
- `git diff --check`.
