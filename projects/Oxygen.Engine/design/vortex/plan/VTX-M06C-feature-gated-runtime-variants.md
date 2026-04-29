# VTX-M06C - Feature-Gated Runtime Variants

Status: `validated`

## 1. Goal

VTX-M06C proves that Vortex can assemble and run intentionally reduced
runtime variants without crashes, fake publications, stale products, or
downstream consumers reading disabled subsystem data.

The milestone builds on the validated multi-view and offscreen surfaces from
VTX-M06A/B. The target is a production-clean feature gate contract for
runtime views and offscreen scene captures, not demo-only shortcuts.

## 2. Scope

In scope:

- Depth-only rendering: depth prepass product exists; base pass, lighting,
  environment, translucency, post-process, and composition color products are
  either omitted or explicitly invalid for the variant contract.
- Shadow-only rendering: conventional shadow products are generated for the
  requested view/light set without main-view color shading.
- No-environment rendering: sky, atmosphere, height fog, local fog, IBL, and
  environment frame products are disabled or invalid while opaque lighting,
  shadows, translucency, post, and composition remain valid.
- No-shadowing rendering: direct lighting and environment paths run without
  publishing or consuming shadow products.
- No-volumetrics rendering: sky/atmosphere/fog paths may run, but volumetric
  fog/local-fog volumetric products are disabled and not consumed.
- Diagnostics-only overlays: diagnostics/overlay output can be produced without
  scene lighting products.
- Variant input vocabulary on Vortex-native surfaces, capability gates,
  per-view feature masks, null-safe service behavior, diagnostics records, and
  proof tooling.
- Focused tests and runtime proof showing both enabled and disabled product
  states for every variant.

Out of scope:

- New renderer architecture, new render graph/RDG infrastructure, or a second
  production scene renderer.
- Legacy `Oxygen.Renderer` references, fallbacks, or simplification paths.
- Future families such as VSM, IBL cubemap capture/filtering, GI/reflections,
  heterogeneous volumes, clouds, water, hair, and distortion.
- Full editor UI for configuring variants. Demo-app proof controls are allowed
  when they are clean runtime inputs.

## 3. Current State

| Area | Current state | VTX-M06C action |
| --- | --- | --- |
| Roadmap/status | `PLAN.md` and `IMPLEMENTATION_STATUS.md` now record VTX-M06C as `validated`; VTX-M07 is the next planned milestone. | Keep the VTX-M06C closure evidence in this plan and the status ledger; future production-readiness work belongs to VTX-M07. |
| Capability families | `RendererCapabilityFamily` gates construction of major services such as scene preparation, deferred shading, lighting, shadowing, environment lighting, final output composition, and diagnostics. | Preserve capability construction gates and add tests for required/optional variant capability sets. |
| View feature mask | `CompositionView::ViewFeatureMask` carries scene lighting, shadows, environment, translucency, and diagnostics bits, and composition planning copies the mask. | Promote the mask from carried metadata to the source of stage omission truth for per-view variants. |
| Runtime render context | `RenderContext::ViewExecutionEntry` carries per-view scene flags, render/shading overrides, and composition view pointer. It does not carry an effective runtime variant contract. | Add a typed effective variant/feature contract that SceneRenderer stages consume without consulting demo-only state. |
| SceneRenderer services | Service construction is capability-gated, but per-view stage execution still assumes most constructed services are eligible. | Add stage-level feature checks and disabled-state diagnostics before service calls and product publication. |
| Offscreen facade | `OffscreenPipelineInput` selects deferred or forward shading only. | Extend it with production-clean variant selection or feature profile inputs needed for offscreen proof. |
| Diagnostics proof | Diagnostics service records pass/product truth and prior tools analyze M06A/B captures. | Extend proof scripts to assert omitted stages, invalid products, and absent downstream consumption. |
| Demo proof surface | `Examples/MultiView` already hosts visually inspectable proof layouts and offscreen products. | Add a clean feature-variant proof layout without test-only code in production renderer paths. |

## 4. Existing Behavior To Preserve

- Validated M06A multi-view behavior: per-view state handles, serialized
  view-family execution, scene-texture lease pool, data-driven composition,
  auxiliary dependency proof, overlay lanes, allocation-churn proof, and
  GroundGrid stability.
- Validated M06B offscreen behavior: deferred and solid-forward offscreen
  products, downstream texture composition, final shader-resource state, CDB
  proof, RenderDoc proof, and allocation-churn proof.
- Validated M05A-M05D behavior: diagnostics, occlusion, translucency, and
  conventional directional/spot/point shadowing.
- Vortex-native architecture only. No legacy renderer use.
- Truthful product publication: disabled stages must not leave valid-looking
  descriptors, revisions, or diagnostics products.

## 5. UE5.7 Parity References

VTX-M06C is feature-gating parity, not a new shading model. Local UE5.7
grounding for the gate contract:

- `F:\Epic Games\UE_5.7\Engine\Source\Runtime\Engine\Private\WorldPartition\WorldPartitionMiniMapHelper.cpp`
  uses a scene-capture component with show flags disabling lighting,
  atmosphere, post processing, fog, volumetric fog, dynamic shadows, and sky
  lighting for a reduced runtime capture.
- `F:\Epic Games\UE_5.7\Engine\Source\Runtime\Renderer\Private\DeferredShadingRenderer.cpp`
  branches the renderer on output mode, including a depth-prepass-only
  `ERendererOutput::DepthPrepassOnly` path that copies depth/capture output
  without continuing through the full final-scene-color path.
- `F:\Epic Games\UE_5.7\Engine\Source\Runtime\Renderer\Private\DeferredShadingRenderer.cpp`
  gates deferred lighting on `EngineShowFlags.Lighting`,
  `EngineShowFlags.DeferredLighting`, GBuffer use, and ray-traced overlay
  state.
- `F:\Epic Games\UE_5.7\Engine\Source\Runtime\Renderer\Private\DeferredShadingRenderer.cpp`
  gates sky atmosphere, volumetric fog, local fog volumes, volumetric clouds,
  and dynamic shadows through view-family show flags and renderer-output
  predicates.

Vortex should mirror the discipline: feature gates are input contracts that
select real stage behavior and product validity. They are not post-hoc analyzer
exceptions.

## 6. Contract Truth Table

| Variant | Required products | Disabled products | Consumers that must be safe |
| --- | --- | --- | --- |
| Depth-only | Prepared view, SceneDepth, optional depth diagnostics. | SceneColor, GBuffer, lighting, shadows, environment, translucency, post-process color output. | Resolve/post/composition paths must not require SceneColor for the depth-only contract. |
| Shadow-only | Prepared view, light selection, shadow frame bindings/surfaces for eligible lights. | Main SceneColor/GBuffer/lighting/environment/post products. | Shadow service must not depend on base-pass color products. |
| No-environment | Prepared view, depth, GBuffer/SceneColor, lighting, optional shadows, post/composition. | Environment frame bindings, sky/atmosphere/fog/IBL, volumetric/local-fog products. | Lighting/translucency must see invalid environment slots and remain stable. |
| No-shadowing | Prepared view, depth, GBuffer/SceneColor, lighting, environment/post as requested. | Shadow frame bindings and shadow surfaces. | Lighting, environment, and translucency must not sample stale shadow descriptors. |
| No-volumetrics | Environment bindings may exist for sky/height fog; non-volumetric sky/fog may render. | Integrated light scattering and volumetric fog/local-fog volumetric products. | Stage 15 and translucency must treat volumetric slots as invalid. |
| Diagnostics-only | Diagnostics pass/product records and overlay/composition output if requested. | Scene lighting, shadows, environment, translucency, post products unless explicitly enabled. | Diagnostics overlay path must not fabricate scene products. |

Every disabled state must be represented by invalid shader-visible indices,
`valid=false` diagnostics products, omitted RenderDoc pass scopes, or explicit
disabled diagnostics records.

## 7. Implementation Slices

### A. Plan and Status Truth

Required work:

- Create this detailed plan.
- Update `PLAN.md`, `IMPLEMENTATION_STATUS.md`, and the plan package README.
- During execution, keep VTX-M06C unvalidated until implementation and closure
  proof exist; the final status ledger now records that closure proof.

Validation:

- `git diff --check`

Evidence:

- `git diff --check` passed for the planning/status patch. Later slices closed
  runtime implementation and proof.

### B. Variant Vocabulary and Validation

Required work:

- Add a typed runtime variant/profile vocabulary that can represent all six
  VTX-M06C variants without demo-only flags or cross-domain option leakage.
- Map profiles to `ViewFeatureMask`, capability requirements, render/shading
  settings, and product expectations.
- Extend offscreen inputs and runtime view materialization so both runtime and
  offscreen proof paths use the same production contract.
- Add focused tests for profile defaults, invalid combinations, and capability
  requirement reporting.

Validation:

- `cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.RendererCapability.Tests Oxygen.Vortex.RendererCompositionQueue.Tests Oxygen.Vortex.OffscreenSceneFacade.Tests --parallel 4`
- `ctest --preset test-debug -R "Oxygen\.Vortex\.(RendererCapability|RendererCompositionQueue|OffscreenSceneFacade)" --output-on-failure`

Evidence:

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

### C. Depth-Only and Shadow-Only Stage Gates

Required work:

- Route depth-only views through preparation and depth products while bypassing
  color shading and consumers that require SceneColor.
- Route shadow-only views through light selection and shadow service products
  without requiring main-view base-pass color products.
- Publish explicit disabled or invalid state for omitted products.
- Add SceneRenderer tests for stage omission and product validity.

Validation:

- Focused `SceneRendererDeferredCore`/publication tests.
- CDB/debug-layer smoke before deeper capture analysis.

Evidence:

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

### D. Environment, Shadowing, and Volumetric Disable Gates

Required work:

- Implement no-environment, no-shadowing, and no-volumetrics gates at the
  per-view stage level.
- Ensure LightingService, EnvironmentLightingService, TranslucencyModule, and
  post-process consumers receive invalid slots rather than stale descriptors.
- Preserve normal validated behavior when the corresponding features are
  enabled.

Validation:

- Focused environment, shadow, translucency, SceneRenderer, and publication
  tests.
- ShaderBake/catalog validation if any shader ABI, root binding, catalog, or
  HLSL source changes.

Evidence:

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

### E. Diagnostics-Only and Overlay Variant

Required work:

- Support a diagnostics-only proof view that can publish diagnostics/overlay
  output without scene-lighting products.
- Keep diagnostics records truthful for omitted passes and invalid products.
- Add tests that fail if diagnostics-only creates fake scene products.

Validation:

- Focused diagnostics and composition tests.
- Runtime CDB/debug-layer smoke before capture proof.

Evidence:

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

### F. Runtime Proof Layout and Analyzer

Required work:

- Add a visually inspectable Vortex demo proof layout for all six variants.
- Reuse production runtime/offscreen inputs; keep proof-specific scenario setup
  in demo/tooling code.
- Extend or add RenderDoc analysis to prove expected pass presence/absence,
  product publication, disabled-product state, and downstream consumption.
- Add a wrapper that runs build, CDB/debug-layer audit, RenderDoc capture,
  scripted assertions, and 60-frame allocation-churn proof.

Validation:

- `powershell -NoProfile -ExecutionPolicy Bypass -File tools\vortex\Run-VortexFeatureVariantValidation.ps1 -Output out\build-ninja\analysis\vortex\m06c-feature-variants -Frame 5 -RunFrames 65 -Fps 30 -BuildJobs 4`

Evidence:

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
- User visual confirmation on 2026-04-28 approved the runtime proof visuals,
  confirmed the shadow flicker was gone after the per-view shadow lifetime fix,
  and approved the updated one-line labels plus `BLACK expected` markers.

### G. Closure and Ledger Update

Required work:

- Run the full focused suite and runtime proof.
- Run `git diff --check`.
- Update this plan and `IMPLEMENTATION_STATUS.md` only with proven evidence.
- Keep residual gaps explicit. Do not mark VTX-M06C `validated` without
  implementation, focused tests, CDB/debug-layer proof, RenderDoc scripted
  analysis, allocation-churn proof, visual confirmation for the visual layout,
  and recorded residual gaps.

Evidence:

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

## 10. Exit Gate

VTX-M06C cannot be marked `validated` until:

1. Implementation exists in production Vortex code.
2. Required plan/status/LLD updates are current.
3. Focused tests pass.
4. CDB/debug-layer proof passes.
5. RenderDoc scripted analysis passes for every variant.
6. Allocation-churn proof passes.
7. ShaderBake/catalog validation is recorded if shader/ABI changed.
8. Visual confirmation is recorded for the proof layout.
9. Residual gaps are recorded and accepted, or there are no residual gaps.

## 11. Replan Triggers

Replan before continuing if:

- A requested variant cannot be represented by the current capability or
  per-view feature model without leaking test/demo state into production.
- A disabled stage still requires valid descriptors for downstream execution.
- Depth-only or shadow-only output requires a new product handoff contract not
  covered by the current offscreen/runtime composition model.
- UE5.7 grounding reveals that the chosen feature gate maps to a different
  renderer-output class than this plan assumes.
- Validation requires shader ABI or root-binding changes not captured by the
  current slice plan.

## 12. Status Update Requirements

- Update this plan slice evidence only after the relevant validation command
  passes.
- Update `IMPLEMENTATION_STATUS.md` when a slice has implementation plus
  validation evidence, and keep the milestone `in_progress` until the full exit
  gate is satisfied.
- Update `PLAN.md` only for milestone-level status or scope changes.
