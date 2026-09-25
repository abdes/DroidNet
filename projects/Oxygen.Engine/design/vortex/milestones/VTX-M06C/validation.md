# VTX-M06C — validation

This record preserves the conditions and results at each implementation checkpoint.
The milestone README owns the final outcome and remaining work.

## Validation and remaining work

**Qualification:** `validated`

Detailed plan `design/vortex/milestones/VTX-M06C/README.md` exists. Slices B-E implemented and validated feature-profile vocabulary/carry, depth-only and shadow-only gates, no-environment/no-shadowing/no-volumetrics gates, diagnostics-only product truth, and focused source tests. Slice F added the MultiView `--feature-variant-proof-layout true` runtime proof, RenderDoc analyzer, CDB/debug-layer wrapper, assertion script, and allocation-churn report. Closure validation passed the full focused build plus `oxygen-graphics-direct3d12_shaders`, full focused CTest with 8/8 Vortex test executables, CDB/debug-layer report with runtime exit `0`, no debugger break, `d3d12_error_count=0`, `dxgi_error_count=0`, and `blocking_warning_count=0`, RenderDoc report with `overall_verdict=true`, `expected_variant_view_count=6`, `composition_view_ids=1,2,3,4,5,6`, correct reduced-variant stage omission, and Stage 22 scene-lighting-only proof, allocation report with `steady_state_frame_count=60` and `steady_state_allocations_after_warmup=0`, assertion report with 65 runtime records for every variant and `overall_verdict=pass`, absence of the prior missing-SceneColor compositing warning, and manual visual confirmation on 2026-04-28 approving the proof visuals, shadow stability, compact labels, and `BLACK expected` markers.

**Remaining work:** No open VTX-M06C closure gap.

## 3. Current State

| Area                   | Current state                                                                                                                                                                                      | VTX-M06C action                                                                                                             |
| ---------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------- |
| Roadmap/status         | `PLAN.md` and `milestone README` now record VTX-M06C as `validated`; VTX-M07 is the next planned milestone.                                                                                        | Keep the VTX-M06C closure evidence in this plan and the status ledger; future production-readiness work belongs to VTX-M07. |
| Capability families    | `RendererCapabilityFamily` gates construction of major services such as scene preparation, deferred shading, lighting, shadowing, environment lighting, final output composition, and diagnostics. | Preserve capability construction gates and add tests for required/optional variant capability sets.                         |
| View feature mask      | `CompositionView::ViewFeatureMask` carries scene lighting, shadows, environment, translucency, and diagnostics bits, and composition planning copies the mask.                                     | Promote the mask from carried metadata to the source of stage omission truth for per-view variants.                         |
| Runtime render context | `RenderContext::ViewExecutionEntry` carries per-view scene flags, render/shading overrides, and composition view pointer. It does not carry an effective runtime variant contract.                 | Add a typed effective variant/feature contract that SceneRenderer stages consume without consulting demo-only state.        |
| SceneRenderer services | Service construction is capability-gated, but per-view stage execution still assumes most constructed services are eligible.                                                                       | Add stage-level feature checks and disabled-state diagnostics before service calls and product publication.                 |
| Offscreen facade       | `OffscreenPipelineInput` selects deferred or forward shading only.                                                                                                                                 | Extend it with production-clean variant selection or feature profile inputs needed for offscreen proof.                     |
| Diagnostics proof      | Diagnostics service records pass/product truth and prior tools analyze M06A/B captures.                                                                                                            | Extend proof scripts to assert omitted stages, invalid products, and absent downstream consumption.                         |
| Demo proof surface     | `Examples/MultiView` already hosts visually inspectable proof layouts and offscreen products.                                                                                                      | Add a clean feature-variant proof layout without test-only code in production renderer paths.                               |

## 8. Test Plan

Focused test targets may be refined as source slices land, but closure requires
at minimum:

```powershell
cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.RendererCapability.Tests Oxygen.Vortex.RendererCompositionQueue.Tests Oxygen.Vortex.OffscreenSceneFacade.Tests Oxygen.Vortex.SceneRendererDeferredCore.Tests Oxygen.Vortex.SceneRendererPublication.Tests Oxygen.Vortex.EnvironmentLightingService.Tests Oxygen.Vortex.ShadowService.Tests Oxygen.Vortex.DiagnosticsService.Tests oxygen-examples-multiview --parallel 4
ctest --preset test-debug -R "Oxygen\.Vortex\.(RendererCapability|RendererCompositionQueue|OffscreenSceneFacade|SceneRendererDeferredCore|SceneRendererPublication|EnvironmentLightingService|ShadowService|DiagnosticsService)" --output-on-failure
```

Run ShaderBake/catalog validation when shader source, shader ABI, root-binding,
or shader catalog data changes.

## 9. Runtime / Capture Proof

Closure proof must include:

- CDB/debug-layer audit with runtime exit code `0`, no debugger break, no
  D3D12/DXGI errors, and no blocking warnings.
- RenderDoc scripted analysis proving the expected stage/product matrix for
  depth-only, shadow-only, no-environment, no-shadowing, no-volumetrics, and
  diagnostics-only variants.
- Allocation-churn proof over at least 60 steady-state frames with zero
  allocations after warmup for the variant proof path.
- Visual confirmation for the demo proof layout because the scenario is meant
  to be inspectable by eye.

## A. Plan and Status Truth — checks

- `git diff --check`

## A. Plan and Status Truth — results

- `git diff --check` passed for the planning/status patch. Later slices closed
  runtime implementation and proof.

## B. Variant Vocabulary and Validation — checks

- `cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.RendererCapability.Tests Oxygen.Vortex.RendererCompositionQueue.Tests Oxygen.Vortex.OffscreenSceneFacade.Tests --parallel 4`
- `ctest --preset test-debug -R "Oxygen\.Vortex\.(RendererCapability|RendererCompositionQueue|OffscreenSceneFacade)" --output-on-failure`

## B. Variant Vocabulary and Validation — results

- Source implementation adds `CompositionView::ViewFeatureProfile`,
  `ViewFeatureProfileSpec`, `ResolveViewFeatureProfileSpec()`, the
  `kVolumetrics` feature bit, offscreen pipeline feature-profile selection,
  offscreen capability validation, runtime published-view profile/mask carry,
  render-context profile/mask carry, and frame-packet profile copy.
- Focused validation passed:
  `cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.RendererCapability.Tests Oxygen.Vortex.RendererCompositionQueue.Tests Oxygen.Vortex.OffscreenSceneFacade.Tests --parallel 4`.
- Additional carry-path validation passed because the slice updated
  `CompositionPlanner_test.cpp`:
  `cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.CompositionPlanner.Tests --parallel 4`.
- Focused tests passed:
  `ctest --preset test-debug -R "Oxygen\.Vortex\.(RendererCapability|RendererCompositionQueue|OffscreenSceneFacade)" --output-on-failure`
  with 3/3 test executables passing.
- Additional tests passed:
  `ctest --preset test-debug -R "Oxygen\.Vortex\.CompositionPlanner" --output-on-failure`
  with 1/1 test executable passing.
- Residual gap: slice B defines and carries variant intent only. It does not
  yet implement depth-only, shadow-only, disabled environment/shadow/
  volumetric stage omission, diagnostics-only rendering, CDB/debug-layer proof,
  RenderDoc proof, allocation-churn proof, or visual proof.

## C. Depth-Only and Shadow-Only Stage Gates — checks

- Focused `SceneRendererDeferredCore`/publication tests.
- CDB/debug-layer smoke before deeper capture analysis.

## C. Depth-Only and Shadow-Only Stage Gates — results

- Source implementation gates the SceneRenderer stage chain with the carried
  feature profile/mask. Depth-only forces Stage 3 depth prepass, publishes
  SceneDepth, resolves depth for later handoff, and skips SceneColor/GBuffer,
  lighting, shadow, environment, translucency, post-process, and ground-grid
  color work. Shadow-only builds light selection, runs Stage 8 shadow products,
  and skips main-view depth, SceneColor/GBuffer, lighting, environment,
  translucency, post-process, and ground-grid color work.
- Focused validation passed:
  `cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.SceneRendererDeferredCore.Tests --parallel 4`.
- Focused tests passed:
  `ctest --preset test-debug -R "Oxygen\.Vortex\.SceneRendererDeferredCore" --output-on-failure`
  with 1/1 test executable and 48/48 tests passing, including
  `DepthOnlyFeatureProfilePublishesDepthWithoutSceneColorProducts` and
  `ShadowOnlyFeatureProfilePublishesShadowBindingsWithoutSceneColorProducts`.
- Adjacent slice-B regression validation passed:
  `cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.RendererCapability.Tests Oxygen.Vortex.RendererCompositionQueue.Tests Oxygen.Vortex.OffscreenSceneFacade.Tests Oxygen.Vortex.CompositionPlanner.Tests --parallel 4`
  and
  `ctest --preset test-debug -R "Oxygen\.Vortex\.(RendererCapability|RendererCompositionQueue|OffscreenSceneFacade|CompositionPlanner)" --output-on-failure`
  with 4/4 test executables passing.
- No open Slice C closure gap. Later Slice F proof closed D3D12 CDB/debug-layer
  and RenderDoc proof for these variant gates.

## D. Environment, Shadowing, and Volumetric Disable Gates — checks

- Focused environment, shadow, translucency, SceneRenderer, and publication
  tests.
- ShaderBake/catalog validation if any shader ABI, root binding, catalog, or
  HLSL source changes.

## D. Environment, Shadowing, and Volumetric Disable Gates — results

- Source evidence exists in `EnvironmentLightingService` and
  `SceneRendererDeferredCore` coverage for no-environment, no-shadowing, and
  no-volumetrics. No-environment suppresses environment frame bindings while
  preserving scene lighting; no-shadowing suppresses shadow products while
  preserving scene lighting; no-volumetrics preserves environment publication
  and Stage 15 sky/height-fog rendering while suppressing VolumetricFogPass,
  integrated light scattering, and volumetric GPU flags.
- Validation passed:
  `cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.EnvironmentLightingService.Tests Oxygen.Vortex.SceneRendererDeferredCore.Tests --parallel 4`
  and
  `ctest --preset test-debug -R "Oxygen\.Vortex\.(EnvironmentLightingService|SceneRendererDeferredCore)" --output-on-failure`
  with 2/2 test executables passing.
- ShaderBake/catalog validation was not run for this slice because no shader
  source, shader ABI, root-binding, or catalog files changed. Later Slice F
  proof closed the runtime CDB/debug-layer, RenderDoc, allocation-churn, visual
  proof, and final VTX-M06C closure gates.

## E. Diagnostics-Only and Overlay Variant — checks

- Focused diagnostics and composition tests.
- Runtime CDB/debug-layer smoke before capture proof.

## E. Diagnostics-Only and Overlay Variant — results

- Source evidence exists in `SceneRenderer`: diagnostics-only views publish a
  real diagnostics-ledger pass/product when diagnostics capability is present,
  keep depth, scene color, custom depth/stencil, GBuffer, shadow, environment,
  resolve, and deferred-lighting products invalid, and avoid advertising
  produced scene outputs for omitted stages.
- Validation passed:
  `cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.SceneRendererDeferredCore.Tests Oxygen.Vortex.DiagnosticsService.Tests Oxygen.Vortex.CompositionPlanner.Tests --parallel 4`
  and
  `ctest --preset test-debug -R "Oxygen\.Vortex\.(SceneRendererDeferredCore|DiagnosticsService|CompositionPlanner)" --output-on-failure`
  with 3/3 test executables passing.
- ShaderBake/catalog validation was not run for this slice because no shader
  source, shader ABI, root-binding, or catalog files changed. Later Slice F
  proof closed the runtime CDB/debug-layer, RenderDoc, allocation-churn, visual
  proof, and final VTX-M06C closure gates.

## F. Runtime Proof Layout and Analyzer — checks

- `powershell -NoProfile -ExecutionPolicy Bypass -File tools\vortex\Run-VortexFeatureVariantValidation.ps1 -Output out\build-ninja\analysis\vortex\m06c-feature-variants -Frame 5 -RunFrames 65 -Fps 30 -BuildJobs 4`

## F. Runtime Proof Layout and Analyzer — results

- Source implementation adds `Examples/MultiView --feature-variant-proof-layout
true`, a visually inspectable 3x2 proof layout for depth-only, shadow-only,
  no-environment, no-shadowing, no-volumetrics, and diagnostics-only runtime
  views. The overlay uses one-line clipped labels and explicitly marks the
  expected black reduced-output views: depth-only, shadow-only, and
  diagnostics-only.
- `Examples/MultiView/README.md` now documents the current Vortex-native
  MultiView demo, all proof-layout switches, the VTX-M06C expected-black
  views, and the feature-variant validation wrapper.
- The proof layout uses production `CompositionView::ViewFeatureProfile`
  inputs. Proof-specific layout, camera, and marker text remain in
  `Examples/MultiView` and `tools/vortex`; no test-only feature branching was
  added to the production renderer path.
- Runtime proof tooling was added:
  `tools\vortex\Run-VortexFeatureVariantValidation.ps1`,
  `tools\vortex\AnalyzeRenderDocVortexFeatureVariants.py`, and
  `tools\vortex\Assert-VortexFeatureVariantProof.ps1`.
- During visual validation, multi-view shadow flicker exposed two production
  renderer bugs. Stage 8 shadow rendering now runs per serialized current view
  instead of reusing one frame-wide shadow-depth build with only the cursor
  view constants, and `ShadowDepthPass` now allocates per-slice transient
  structured-buffer constants so later shadow recordings cannot overwrite CPU
  upload memory still referenced by queued GPU work.
- The diagnostics-only profile no longer requires a color output. Runtime
  compositing still warns for color-required views that lack resolved
  SceneColor, but reduced-output views use their expected published composite
  source without emitting the per-frame warning.
- Runtime proof passed:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools\vortex\Run-VortexFeatureVariantValidation.ps1 -Output out\build-ninja\analysis\vortex\m06c-feature-variants -Frame 5 -RunFrames 65 -Fps 30 -BuildJobs 4`.
  The debug-layer report
  `out\build-ninja\analysis\vortex\m06c-feature-variants.debug-layer.report.txt`
  records `runtime_exit_code=0`, `debugger_break_detected=false`,
  `d3d12_error_count=0`, `dxgi_error_count=0`,
  `blocking_warning_count=0`, and `overall_verdict=pass`.
- RenderDoc analysis report
  `out\build-ninja\analysis\vortex\m06c-feature-variants.renderdoc.txt`
  records `overall_verdict=true`, `expected_variant_view_count=6`,
  `composition_view_ids=1,2,3,4,5,6`, `stage3_scope_count=4`,
  `stage8_scope_count=6`, `stage9_scope_count=3`,
  `stage12_scope_count=3`, `stage22_scope_count=3`,
  `depth_products_present=true`, `shadow_products_present=true`,
  `reduced_variants_omitted_scene_lighting=true`,
  `stage22_scene_lighting_only=true`, and no volumetric overrun.
- Allocation-churn report
  `out\build-ninja\analysis\vortex\m06c-feature-variants.allocation-churn.txt`
  records `run_frames=65`, `steady_state_frame_count=60`,
  `steady_state_allocations_after_warmup=0`, and
  `overall_verdict=pass`.
- Assertion report
  `out\build-ninja\analysis\vortex\m06c-feature-variants.validation.txt`
  records 65 runtime records for each of the six variant views and
  `overall_verdict=pass`.
- The validation logs were checked for the previous compositing warning
  (`missing the resolved scene-color artifact` /
  `falling back to published composite source`), and the warning was absent.
- Manual visual confirmation on 2026-04-28 approved the runtime proof visuals,
  confirmed the shadow flicker was gone after the per-view shadow lifetime fix,
  and approved the updated one-line labels plus `BLACK expected` markers.

## G. Closure and Ledger Update — results

- Full focused build and shader validation passed:
  `cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.RendererCapability.Tests Oxygen.Vortex.RendererCompositionQueue.Tests Oxygen.Vortex.OffscreenSceneFacade.Tests Oxygen.Vortex.SceneRendererDeferredCore.Tests Oxygen.Vortex.SceneRendererPublication.Tests Oxygen.Vortex.EnvironmentLightingService.Tests Oxygen.Vortex.ShadowService.Tests Oxygen.Vortex.DiagnosticsService.Tests oxygen-examples-multiview oxygen-graphics-direct3d12_shaders --parallel 4`.
- Full focused tests passed:
  `ctest --preset test-debug -R "Oxygen\.Vortex\.(RendererCapability|RendererCompositionQueue|OffscreenSceneFacade|SceneRendererDeferredCore|SceneRendererPublication|EnvironmentLightingService|ShadowService|DiagnosticsService)" --output-on-failure`
  with 8/8 test executables passing.
- Proof-tool syntax checks passed:
  `python -m py_compile tools\vortex\AnalyzeRenderDocVortexFeatureVariants.py`
  and PowerShell parser checks for
  `tools\vortex\Assert-VortexFeatureVariantProof.ps1` and
  `tools\vortex\Run-VortexFeatureVariantValidation.ps1`.
- `git diff --check` passed after the final docs/status update.
- No open VTX-M06C closure gap remains.
