# VTX-M06B — validation

This record preserves the conditions and results at each implementation checkpoint.
The milestone README owns the final outcome and remaining work.

## Validation and remaining work

**Qualification:** `validated`

Detailed plan `design/vortex/milestones/VTX-M06B/README.md` exists. Slices A-D landed source/test proof for docs truth, offscreen scene execution, deferred/forward routing, and output product final state. Slice E source commits `f12fcefb9`, `dfc3dab3c`, `5299d6c1c`, `62b4b525f`, and `97ca7fb65` add runtime texture composition layers, embedded offscreen execution, a visually inspectable MultiView offscreen layout, forward-wireframe regression coverage, and solid forward base-pass SceneColor output without deferred GBuffer publication; the offscreen capture proof tile now requests forward solid. Proof tooling `Run-VortexOffscreenValidation.ps1`, `AnalyzeRenderDocVortexOffscreen.py`, and `Assert-VortexOffscreenProof.ps1` runs the closure gate. Latest validation passed `powershell -NoProfile -ExecutionPolicy Bypass -File tools\vortex\Run-VortexOffscreenValidation.ps1 -Output out\build-ninja\analysis\vortex\m06b-offscreen\offscreen-proof -Frame 5 -RunFrames 65 -Fps 30 -BuildJobs 4`: CDB report `overall_verdict=pass`, runtime exit `0`, `d3d12_error_count=0`, `dxgi_error_count=0`, and `blocking_warning_count=0`; RenderDoc report `overall_verdict=true`, `deferred_draw_count=10`, `forward_draw_count=5`, preview/capture composite work counts `1/1`, and non-empty preview/capture texture RGB proof; allocation report `run_frames_at_least_60=true`, `steady_state_frame_count=190`, `steady_state_allocations_after_warmup=0`, and `overall_verdict=pass`; assertion report `overall_verdict=pass`. Focused build/CTest also passed `OffscreenSceneFacade`, `RendererCompositionQueue`, and `SceneRendererDeferredCore` with 3/3 test executables. Manual visual confirmation on 2026-04-28 approved the solid-forward offscreen proof visual. ShaderBake/catalog validation was not run because no shader source, shader ABI, root-binding, or catalog files changed.

**Remaining work:** No open VTX-M06B closure gap. Feature-gated runtime variants remain VTX-M06C.

## 3. Current Source Reality

| Area                     | Current state                                                                                                                                                                                                                        | VTX-M06B action                                                                                                       |
| ------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | --------------------------------------------------------------------------------------------------------------------- |
| LLD                      | `lld/offscreen-rendering.md` describes the desired offscreen facade behavior, but it predates the current renderer structure.                                                                                                        | Keep the LLD as the contract, and patch it first if execution discovers scope drift.                                  |
| Public facade            | `Renderer::ForOffscreenScene()` returns an `OffscreenSceneFacade` with frame, scene, view, output, and pipeline setters.                                                                                                             | Preserve source compatibility while adding real execution semantics.                                                  |
| Presets                  | `offscreen::scene::presets::ForPreview()` and `ForCapture()` configure the facade.                                                                                                                                                   | Make presets produce renderable offscreen sessions, including a valid view identity.                                  |
| Execution                | `ValidatedOffscreenSceneSession::Execute()` currently validates non-null renderer/scene/output and ensures a `SceneRenderer`, but it does not render to the output target.                                                           | First implementation slice must replace this no-op with a Vortex-native offscreen frame execution path.               |
| Pipeline settings        | `OffscreenPipelineInput` is currently empty.                                                                                                                                                                                         | Add the minimum typed settings needed for deferred/solid-forward proof without leaking cross-domain options.          |
| Forward solid scene path | Vortex routes `ShadingMode::kForward` through the base pass using the shared Forward+ mesh shader path and writes SceneColor directly without publishing GBuffer products. Forward wireframe remains debug/regression coverage only. | Validated by focused tests, CDB/debug-layer proof, RenderDoc structural proof, and non-empty offscreen product proof. |
| Product handoff          | The output framebuffer is accepted but not populated by the facade.                                                                                                                                                                  | Route scene/post/composition output into the caller target and leave the product in the documented final state.       |
| Runtime proof            | Dedicated offscreen runtime proof script, RenderDoc analyzer, assertion wrapper, and allocation-churn report are implemented.                                                                                                        | Validated by `Run-VortexOffscreenValidation.ps1`; keep these artifacts current if the proof layout changes.           |

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

## A. Plan and Status Truth — checks

- `git diff --check`

## A. Plan and Status Truth — results

- Landed in commit `5ddf9c064` after `git diff --check` passed. The
  milestone remains `in_progress`.

## B. Offscreen Frame Execution Substrate — checks

- Focused offscreen facade tests that prove `Execute()` invokes rendering.
- Focused build target for the affected Vortex library/tests.

## B. Offscreen Frame Execution Substrate — results

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

## C. Deferred and Forward Pipeline Selection — checks

- Focused tests for deferred, forward, default, and invalid pipeline inputs.
- ShaderBake/catalog validation only if shader ABI, root bindings, or shader
  source changes.

## C. Deferred and Forward Pipeline Selection — results

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
  still distinguish deferred and solid forward products in the visual proof
  scenario. Forward wireframe/debug execution does not satisfy this gap.

## D. Product Handoff and Final State — checks

- Focused tests/probes for final product availability.
- CDB/debug-layer audit for resource-state and descriptor errors.

## D. Product Handoff and Final State — results

- Source implementation leaves the caller-owned offscreen color attachment in
  `ResourceStates::kShaderResource` after `ValidatedOffscreenSceneSession`
  rendering and rejects output framebuffers that do not expose a color product.
- Validation passed:
  `cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.OffscreenSceneFacade.Tests --parallel 4`.
- Validation passed:
  `ctest --preset test-debug -R "Oxygen\.Vortex\.OffscreenSceneFacade" --output-on-failure`
  with 7/7 tests passing.
- Residual gap: this is focused source/test final-state proof only. Runtime
  D3D12 CDB/debug-layer audit, RenderDoc scripted analysis, visually
  inspectable runtime preview/capture proof, and allocation-churn proof remain
  required before `VTX-M06B` can be validated.

## E. Runtime Preview and Capture Proof — checks

- CDB/debug-layer audit.
- RenderDoc scripted analysis.
- 60-frame allocation-churn proof for repeated offscreen renders.

## E. Runtime Preview and Capture Proof — results

- Source implementation landed in commits `f12fcefb9`, `dfc3dab3c`,
  `5299d6c1c`, and `62b4b525f`. It adds
  `Examples/MultiView --offscreen-proof-layout true`
  with a normal Vortex scene plus two visible offscreen products: a deferred
  preview panel and an interim forward/wireframe capture thumbnail. The
  products are generated through
  `ForOffscreenScene`/`ValidatedOffscreenSceneSession` and displayed through
  Vortex runtime texture composition layers. This proves the offscreen route
  and caught a production bug where forward wireframe was skipped, but it does
  not close the required solid forward offscreen product.
- Focused source validation passed:
  `cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.OffscreenSceneFacade.Tests Oxygen.Vortex.RendererCompositionQueue.Tests oxygen-examples-multiview --parallel 4`.
- Focused tests passed:
  `ctest --preset test-debug -R "Oxygen\.Vortex\.(OffscreenSceneFacade|RendererCompositionQueue)" --output-on-failure`
  with 2/2 test executables passing.
- Early CDB smoke passed:
  `Oxygen.Examples.MultiView.exe --frames 8 --fps 30 --offscreen-proof-layout true --capture-provider off --debug-layer true`
  under `cdb -G -g`, exit code 0, no `CHECK FAILED`, no D3D12/DXGI
  errors, no access violation, and runtime logs proving both offscreen products
  rendered. The same smoke showed warmup lease-pool allocations on frame 1 and
  `allocations_delta=0` for preview, capture, and main scene passes from frame
  2 onward.
- Focused regression validation passed for the forward-wireframe bug:
  `cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.SceneRendererDeferredCore.Tests oxygen-examples-multiview --parallel 4`
  and
  `ctest --preset test-debug -R "Oxygen\.Vortex\.SceneRendererDeferredCore" --output-on-failure`
  with 45/45 tests passing.
- Solid forward source implementation landed in commit `97ca7fb65`: the base pass accepts
  `ShadingMode::kForward` solid draws, binds a SceneColor-only framebuffer,
  runs the shared Forward+ mesh shader path, avoids deferred GBuffer
  publication, and the MultiView offscreen proof capture tile now requests
  forward solid instead of forced wireframe. UE5.7 grounding: local
  `BasePassCommon.ush` keeps `USES_GBUFFER` false under `FORWARD_SHADING`, and
  forward opaque fog/lighting is a base-pass concern.
- Focused solid-forward validation passed:
  `cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.SceneRendererDeferredCore.Tests oxygen-examples-multiview --parallel 4`
  and
  `ctest --preset test-debug -R "Oxygen\.Vortex\.SceneRendererDeferredCore" --output-on-failure`
  with 46/46 tests passing, including `BasePassSolidForwardWritesSceneColor`.
- CDB/debug-layer smoke passed for the solid-forward offscreen layout:
  `cdb -G -g -o -c "g;q" out\build-ninja\bin\Debug\Oxygen.Examples.MultiView.exe --frames 5 --fps 30 --offscreen-proof-layout true --capture-provider off --debug-layer true`
  with exit code 0, no `CHECK FAILED`, no D3D12/DXGI errors, no access
  violation, 5 deferred preview renders, and 5 forward capture renders. The log
  is recorded at
  `out\build-ninja\analysis\vortex\m06b-offscreen\cdb-solid-forward-smoke.log`.
- Runtime closure tooling landed in the current proof-tooling slice:
  `tools\vortex\Run-VortexOffscreenValidation.ps1`,
  `tools\vortex\AnalyzeRenderDocVortexOffscreen.py`, and
  `tools\vortex\Assert-VortexOffscreenProof.ps1`.
- Full slice-E validation passed:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools\vortex\Run-VortexOffscreenValidation.ps1 -Output out\build-ninja\analysis\vortex\m06b-offscreen\offscreen-proof -Frame 5 -RunFrames 65 -Fps 30 -BuildJobs 4`.

## E. Runtime Preview and Capture Proof — results (2)

`offscreen-proof.debug-layer.report.txt` has `overall_verdict=pass`,
`runtime_exit_code=0`, `d3d12_error_count=0`, `dxgi_error_count=0`, and
`blocking_warning_count=0`;
`offscreen-proof.renderdoc.txt` has `overall_verdict=true`,
`deferred_scope_count=2`, `deferred_draw_count=10`,
`forward_scope_count=1`, `forward_draw_count=5`,
`preview_composite_work_count=1`, `capture_composite_work_count=1`,
`preview_texture_rgb_nonzero=true`, and
`capture_texture_rgb_nonzero=true`;
`offscreen-proof.allocation-churn.txt` has `run_frames_at_least_60=true`,
`steady_state_frame_count=190`,
`steady_state_allocations_after_warmup=0`, and `overall_verdict=pass`;
`offscreen-proof.validation.txt` has `overall_verdict=pass`.

- Manual visual confirmation on 2026-04-28 approved the solid-forward offscreen
  proof visual.
- Residual gap: none for slice E.

## F. Closure and Ledger Update — results

- Full focused closure validation passed through
  `Run-VortexOffscreenValidation.ps1`, which builds `oxygen-vortex`,
  `oxygen-graphics-direct3d12`, and `oxygen-examples-multiview`, runs the
  CDB/debug-layer audit, captures/analyzes RenderDoc, produces the 60-frame
  allocation-churn report, and runs the assertion wrapper.
- Focused unit/integration validation passed:
  `cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.OffscreenSceneFacade.Tests Oxygen.Vortex.RendererCompositionQueue.Tests Oxygen.Vortex.SceneRendererDeferredCore.Tests oxygen-examples-multiview --parallel 4`
  and
  `ctest --preset test-debug -R "Oxygen\.Vortex\.(OffscreenSceneFacade|RendererCompositionQueue|SceneRendererDeferredCore)" --output-on-failure`
  with 3/3 test executables passing.
- ShaderBake/catalog validation was not run because no shader source, shader
  ABI, root-binding, or catalog files changed in the closure slices.
- `git diff --check` passed before the solid-forward source commit; rerun it
  after this status/doc update before committing closure docs/tooling.
