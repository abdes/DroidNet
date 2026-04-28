# VTX-M06B - Offscreen Proof Closeout

Status: `in_progress`

## 1. Goal

VTX-M06B proves that `Renderer::ForOffscreenScene()` renders scene-derived
Vortex content into caller-owned offscreen framebuffers without a swap chain,
and that the resulting product is usable by downstream Vortex-native
consumers.

The target is not a validation-only facade. The facade must execute the
existing Vortex scene renderer path against an offscreen target, cover deferred
and forward scene permutations, and leave a truthful product/final-state
contract for preview, thumbnail, and capture callers.

## 2. Scope

In scope:

- `ForOffscreenScene()` input validation for frame session, scene, view/camera,
  output target, and pipeline settings.
- Real `ValidatedOffscreenSceneSession::Execute()` rendering through Vortex
  frame/view execution, not a no-op or legacy renderer fallback.
- Deferred and forward-mode selection through offscreen pipeline settings.
- Output-target ownership and final state suitable for texture consumers.
- Focused unit/integration tests that fail if execution silently skips scene
  rendering.
- Runtime proof covering preview and capture/thumbnail style offscreen targets.
- CDB/debug-layer proof, RenderDoc scripted analysis, readback/product proof,
  and allocation-churn proof for repeated offscreen renders.
- Ledger updates only when the matching implementation and validation evidence
  exists.

Out of scope:

- Feature-gated variants such as depth-only, no-environment, no-shadows, and
  diagnostics-only. Those remain `VTX-M06C`.
- New render graph/RDG infrastructure.
- Legacy `Oxygen.Renderer` fallback paths.
- Full material-preview UI. VTX-M06B proves the rendering/product contract that
  such a UI can consume.

## 3. Current Source Reality

| Area | Current state | VTX-M06B action |
| --- | --- | --- |
| LLD | `lld/offscreen-rendering.md` describes the desired offscreen facade behavior, but it predates the current renderer structure. | Keep the LLD as the contract, and patch it first if execution discovers scope drift. |
| Public facade | `Renderer::ForOffscreenScene()` returns an `OffscreenSceneFacade` with frame, scene, view, output, and pipeline setters. | Preserve source compatibility while adding real execution semantics. |
| Presets | `offscreen::scene::presets::ForPreview()` and `ForCapture()` configure the facade. | Make presets produce renderable offscreen sessions, including a valid view identity. |
| Execution | `ValidatedOffscreenSceneSession::Execute()` currently validates non-null renderer/scene/output and ensures a `SceneRenderer`, but it does not render to the output target. | First implementation slice must replace this no-op with a Vortex-native offscreen frame execution path. |
| Pipeline settings | `OffscreenPipelineInput` is currently empty. | Add the minimum typed settings needed for deferred/forward proof without leaking cross-domain options. |
| Product handoff | The output framebuffer is accepted but not populated by the facade. | Route scene/post/composition output into the caller target and leave the product in the documented final state. |
| Runtime proof | No dedicated offscreen runtime proof script or analyzer exists. | Add proof tooling after the render path exists; keep status `in_progress` until the proof passes. |

## 4. UE5.7 References

VTX-M06B is a Vortex facade/product milestone. UE5.7 grounding is required
where the implementation touches scene capture, thumbnail, view family, or
custom render target behavior:

- `F:\Epic Games\UE_5.7\Engine\Source\Runtime\Engine\Classes\Engine\SceneCapture.h`
- `F:\Epic Games\UE_5.7\Engine\Source\Runtime\Engine\Private\SceneCaptureRendering.cpp`
- `F:\Epic Games\UE_5.7\Engine\Source\Runtime\Renderer\Private\SceneRendering.cpp`
- `F:\Epic Games\UE_5.7\Engine\Source\Runtime\Renderer\Private\RendererModule.cpp`
- `F:\Epic Games\UE_5.7\Engine\Source\Runtime\Renderer\Private\PostProcess\PostProcessing.cpp`

The proof target is Vortex-native behavior, not line-for-line scene-capture
feature parity.

## 5. Implementation Slices

### A. Plan and Status Truth

Required work:

- Create this detailed plan.
- Update `PLAN.md`, `IMPLEMENTATION_STATUS.md`, and the offscreen LLD status
  note so the milestone is `in_progress`, not `planned`.
- Record that current execution is validation-only/no-op and therefore not
  validated.

Validation:

- `git diff --check`

Evidence:

- Landed in commit `5ddf9c064` after `git diff --check` passed. The
  milestone remains `in_progress`.

### B. Offscreen Frame Execution Substrate

Required work:

- Resolve the requested offscreen camera/view through the Vortex scene-view
  resolver.
- Build a one-view render context using a valid published view id and
  offscreen output target.
- Execute the existing Vortex scene renderer frame path against that context.
- Ensure output framebuffer usage is explicit and no swap-chain/present path is
  required.

Validation:

- Focused offscreen facade tests that prove `Execute()` invokes rendering.
- Focused build target for the affected Vortex library/tests.

Evidence:

- Implemented by `Renderer::ValidatedOffscreenSceneSession::Execute()` building
  a real one-view `RenderContext`, resolving the camera through
  `SceneCameraViewResolver`, running `SceneRenderer::OnRender()`, and requiring
  a valid writable offscreen framebuffer. `SceneRenderer::OnStandaloneFrameStart`
  shares the normal per-frame reset path without a `FrameContext`.
- Focused validation passed:
  `cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.OffscreenSceneFacade.Tests --parallel 4`.
- Adjacent API validation passed:
  `cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.SinglePassHarnessFacade.Tests Oxygen.Vortex.RenderGraphHarnessFacade.Tests Oxygen.Vortex.RendererFacadePresets.Tests Oxygen.Vortex.RenderContextMaterializer.Tests Oxygen.Vortex.RendererPublicationSplit.Tests --parallel 4`.
- Focused tests passed:
  `ctest --preset test-debug -R "Oxygen\.Vortex\.(OffscreenSceneFacade|SinglePassHarnessFacade|RenderGraphHarnessFacade|RendererFacadePresets|RenderContextMaterializer|RendererPublicationSplit)" --output-on-failure`
  with 6/6 test executables passing.
- Residual gap: this is source/test proof for execution substrate only. It is
  not runtime/CDB/RenderDoc/final-state/allocation-churn closure.

### C. Deferred and Forward Pipeline Selection

Required work:

- Add typed offscreen pipeline settings for deferred and forward modes.
- Route the selected mode through per-view render settings.
- Validate that invalid or unsupported combinations fail before GPU work.

Validation:

- Focused tests for deferred, forward, default, and invalid pipeline inputs.
- ShaderBake/catalog validation only if shader ABI, root bindings, or shader
  source changes.

Evidence:

- `OffscreenPipelineInput` now carries a typed `ShadingMode`, defaults to
  deferred, and exposes `Deferred()` / `Forward()` factories. `Execute()`
  materializes that mode into the effective offscreen `CompositionView` so the
  normal SceneRenderer shading resolver consumes the setting.
- Focused validation passed:
  `cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.OffscreenSceneFacade.Tests --parallel 4`.
- Focused tests passed:
  `ctest --preset test-debug -R "Oxygen\.Vortex\.OffscreenSceneFacade" --output-on-failure`
  with 6/6 tests passing, covering default deferred selection, explicit
  forward selection, deferred execution, and forward execution.
- ShaderBake/catalog validation was not run because no shader source, shader
  ABI, root-binding, or catalog files changed.
- Residual gap: CPU/API routing is validated; runtime RenderDoc proof must
  still distinguish deferred/forward products in the visual proof scenario.

### D. Product Handoff and Final State

Required work:

- Ensure the caller-owned output target receives the composed scene result.
- Leave the offscreen product in a texture-consumer state rather than a present
  state.
- Add explicit diagnostics for missing or failed handoff.

Validation:

- Focused tests/probes for final product availability.
- CDB/debug-layer audit for resource-state and descriptor errors.

Evidence:

- Record the exact audit report and residual gap.

### E. Runtime Preview and Capture Proof

Required work:

- Add or extend a Vortex-native runtime proof path that renders at least one
  preview-sized offscreen target and one capture/thumbnail-style target.
- Display the same offscreen products inside a normal Vortex demo view so the
  proof is inspectable by eye as well as by capture analysis.
- Add scripted RenderDoc analysis for offscreen pass labels, output resource,
  draw/dispatch presence, and downstream texture consumption.
- Add readback or analyzer proof that output content is non-empty and isolated
  from swap-chain presentation.

Validation:

- CDB/debug-layer audit.
- RenderDoc scripted analysis.
- 60-frame allocation-churn proof for repeated offscreen renders.

Evidence:

- Required before `VTX-M06B` can become `validated`.

### F. Closure and Ledger Update

Required work:

- Run the full focused unit/integration suite for this milestone.
- Run `git diff --check`.
- Update `IMPLEMENTATION_STATUS.md` only with proven implementation and
  validation evidence.
- Keep residual gaps explicit. Do not mark `validated` without runtime proof,
  CDB/debug-layer proof, RenderDoc analysis, allocation-churn proof, and
  truthful final-state/product evidence.

Exit gate:

- Implementation exists.
- Docs/status are current.
- Focused tests pass.
- CDB/debug-layer audit passes.
- RenderDoc scripted analysis passes.
- Repeated offscreen renders show no steady-state allocation churn.
- Offscreen output is proven usable by a downstream Vortex-native consumer.
- Remaining gaps are recorded as none, accepted, or deferred with owner.

## 6. Validation Command Set

The exact command list may be refined as tooling lands, but closure requires at
minimum:

```powershell
cmake --build out\build-ninja --config Debug --target oxygen-vortex oxygen-graphics-direct3d12 --parallel 4
ctest --preset test-debug -R "Oxygen\.Vortex\.(RendererFacade|Offscreen|SceneRenderer|Composition)" --output-on-failure
pwsh tools\vortex\Run-VortexOffscreenValidation.ps1 -Output out\build-ninja\analysis\vortex\m06b-offscreen -Frame 5 -RunFrames 65 -Fps 30 -BuildJobs 4
git diff --check
```

If the script or test names change during implementation, update this plan in
the same slice that introduces the replacement.
