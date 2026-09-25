# VTX-M05C — validation

This record preserves the conditions and results at each implementation checkpoint.
The milestone README owns the final outcome and remaining work.

## Validation and remaining work

**Qualification:** `validated`

Stage 18 `TranslucencyModule`/`TranslucencyMeshProcessor`, SceneRenderer wiring, forward unlit-material exposure contract fix, VortexBasic cyan sphere + magenta cylinder proof scene, and RenderDoc/CDB proof tooling are present. Senior-review remediation on 2026-04-26 fixed sparse-bounds sort fallback, invalid draw rejection, projection-kind detection, diagnostics skip reasons/logging, and Stage 18 PSO/root-binding descriptor caching; broader UE-class gaps for per-material sided culling, instanced draw merging, lightweight translucent shading, and material fog/AP controls are documented as deferred scope. UE5.7 re-check covered standard straight-alpha blending and read-only depth state. Validation: focused ShaderBake/catalog tests passed previously; remediation validation passed `cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.SceneRendererDeferredCore --parallel 4`, `ctest --preset test-debug -R "Oxygen\.Vortex\.SceneRendererDeferredCore" --output-on-failure` with 40/40 tests, and `git diff --check`. Fresh VortexBasic translucency proof after remediation passed `cmake --build out\build-ninja --config Debug --target oxygen-vortex oxygen-graphics-direct3d12 oxygen-examples-vortexbasic --parallel 4`, a CDB/debug-layer audit, runtime log inspection, RenderDoc capture, and `Verify-VortexTranslucencyProof.ps1`. Fresh CDB report `out/build-ninja/analysis/vortex/translucency/m05c-review-remediation/vortexbasic-translucency-review-remediation.debug-layer.report.txt` passed with runtime exit 0, no debugger break, 0 D3D12/DXGI errors, and 0 blocking warnings. Fresh RenderDoc report `out/build-ninja/analysis/vortex/translucency/m05c-review-remediation/vortexbasic-translucency-review-remediation_capture.rdc_vortex_translucency_report.txt` proves Stage 18 scope count 1, Stage 18 draw count 2, Stage 9 draw count 2, ground grid absent, Stage 18 after post-opaque and before resolve, cyan pixels 2130, magenta pixels 225, max RGB delta 2684, `stage18_scene_color_changed=true`, `runtime_log_translucency_enabled=true`, and `runtime_log_draw_metadata_count=4`. Manual visual confirmation approved the final scene.

**Remaining work:** No open M05C closure gap.

## 3. Current State

| Area                  | Current state                                                                              | M05C action                                                                        |
| --------------------- | ------------------------------------------------------------------------------------------ | ---------------------------------------------------------------------------------- |
| Material partitioning | `PassMaskBit::kTransparent` exists and draw metadata tests cover alpha-blended pass masks. | Consume the transparent partition in a Stage 18 mesh processor.                    |
| Shader family         | `Vortex/Stages/Translucency/ForwardMesh_*` shaders are cataloged.                          | Create the pipeline using those shaders and HDR scene-color output.                |
| Stage directory       | Translucency directories contain placeholders only.                                        | Add `TranslucencyModule` and `TranslucencyMeshProcessor`.                          |
| SceneRenderer         | Stage 18 is a comment between environment/fog and overlays.                                | Construct and execute the module with diagnostics facts.                           |
| VortexBasic           | It proves opaque, shadows, fog, occlusion, and diagnostics scenarios.                      | Add a focused translucency validation scene option with visible blend/depth cases. |

## 7. Expected Test Commands

Initial focused commands:

```powershell
cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.SceneRendererDeferredCore oxygen-examples-vortexbasic --parallel 4
ctest --preset test-debug -R "Oxygen\.Vortex\.SceneRendererDeferredCore" --output-on-failure
git diff --check
```

If shader catalog or shader request metadata changes:

```powershell
cmake --build out\build-ninja --config Debug --target Oxygen.Graphics.Direct3D12.ShaderBake Oxygen.Graphics.Direct3D12.ShaderBakeCatalog.Tests --parallel 4
ctest --preset test-debug -R "Oxygen\.Graphics\.Direct3D12\.ShaderBakeCatalog" --output-on-failure
```

Runtime proof artifacts live under:

```text
out/build-ninja/analysis/vortex/translucency/m05c-final/
  vortexbasic-translucency-m05c-final.debug-layer.report.txt
  vortexbasic-translucency-m05c-final_capture.rdc
  vortexbasic-translucency-m05c-final_capture.rdc_vortex_translucency_report.txt

out/build-ninja/analysis/vortex/translucency/m05c-review-remediation/
  vortexbasic-translucency-review-remediation.debug-layer.report.txt
  vortexbasic-translucency-review-remediation.stderr.log
  vortexbasic-translucency-review-remediation_capture.rdc
  vortexbasic-translucency-review-remediation_capture.rdc_vortex_translucency_report.txt
```

## Slice A - Architecture And Plan Authority — checks

- `git diff --check`.
- Consistency scan for stale M05C status/plan references.

## Slice B - Mesh Processor — checks

- Focused Vortex test target build.
- Focused unit tests for transparent filtering and stable back-to-front order.
- Senior-review regressions for sparse bounds, perspective versus orthographic
  key calculation, and invalid draw rejection.

## Slice C - Stage 18 Module And Pipeline — checks

- Focused Vortex build.
- Tests or assertions for result reporting and no-draw behavior.
- Focused reverse-Z pipeline descriptor regression.
- ShaderBake/catalog validation only if shader catalog or shader requests
  change.

## Slice D - SceneRenderer Diagnostics Integration — checks

- Focused `SceneRendererPublication` or diagnostics tests where practical.
- Runtime capture manifest or RenderDoc analyzer proof of Stage 18 facts.

## Slice E - VortexBasic Validation Scene — checks

- Build `oxygen-examples-vortexbasic`.
- Capture a scenario that shows the translucent Stage 18 draw calls.

## Slice F - Runtime Proof And Closeout — checks

- Focused build/test commands recorded in the ledger.
- Debug-layer report records zero D3D12/DXGI errors.
- RenderDoc analyzer report records Stage 18 proof facts.
- Manual visual confirmation is recorded before status becomes `validated`.
