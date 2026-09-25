# Slice A - Architecture And Plan Authority — recorded evidence

Recorded evidence:

- [../lld/occlusion.md](../../lld/occlusion.md) now defines `OcclusionModule` as
  the HZB consumer/visibility publisher over the existing `ScreenHzbModule`.
- [../PLAN.md](../../PLAN.md), [../ARCHITECTURE.md](../../ARCHITECTURE.md), and
  [Milestone record](../../PLAN.md) no longer claim
  that `OcclusionModule` owns generic HZB generation.
- Validation passed on 2026-04-26:
  `rg` consistency scan for M05B/occlusion ownership references;
  `git diff --check`.
- Committed as `cd57ac692 docs: plan vortex occlusion consumer closeout`.

## Slice B - Visibility Result Substrate — recorded evidence

Recorded evidence:

- `OcclusionConfig`, `OcclusionFallbackReason`, `OcclusionFrameResults`,
  `OcclusionStats`, and `OcclusionModule` exist.
- `RenderContext::ViewSpecific` exposes a per-view `occlusion_results` pointer.
- `SceneRenderer` constructs the module when scene-prep/deferred capabilities
  are active and records Stage 5 occlusion diagnostics facts. The default
  module config is disabled, so this slice publishes conservative invalid
  results without changing draw behavior.
- Focused build/test validation passed on 2026-04-26:
  `cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.OcclusionModule --parallel 4`;
  `ctest --preset test-debug -R "Oxygen\.Vortex\.OcclusionModule" --output-on-failure`
  with 5/5 tests passing.
- Neighboring SceneRenderer regression validation passed on 2026-04-26 after
  rebuilding stale test executables:
  `ctest --preset test-debug -R "Oxygen\.Vortex\.(OcclusionModule|SceneRendererPublication|SceneRendererDeferredCore)" --output-on-failure`
  with 54/54 tests passing across 3 test programs.

## Slice C - HZB Occlusion Tester Pass — recorded evidence

Recorded evidence:

- `OcclusionModule` now builds fixed-capacity structured candidate/result
  buffers, submits `Vortex.Stage5.OcclusionTest`, and consumes a later
  `GpuBufferReadback` when available. Missing/pending readback remains
  conservatively visible.
- `Vortex/Stages/Occlusion/OcclusionTest.hlsl` implements a furthest-HZB sphere
  test aligned with the existing Screen HZB binding contract.
- The shader is registered in `EngineShaderCatalog.h`.
- Validation passed on 2026-04-26:
  `cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.OcclusionModule --parallel 4`;
  `ctest --preset test-debug -R "Oxygen\.Vortex\.OcclusionModule" --output-on-failure`
  with 6/6 tests passing;
  `cmake --build out\build-ninja --config Debug --target Oxygen.Graphics.Direct3D12.ShaderBake --parallel 4`;
  `Oxygen.Graphics.Direct3D12.ShaderBake.exe rebuild --workspace-root F:\projects\DroidNet\projects\Oxygen.Engine --build-root F:\projects\DroidNet\projects\Oxygen.Engine\out\build-ninja\shader-bake-validation --out F:\projects\DroidNet\projects\Oxygen.Engine\out\build-ninja\shader-bake-validation\shaders.bin --mode dev`;
  ShaderBake inspect confirmed `Vortex/Stages/Occlusion/OcclusionTest.hlsl`.

## Slice D - Consumer Integration — recorded evidence

Recorded evidence:

- `BasePassMeshProcessor` consumes `OcclusionFrameResults` as an optional
  prepared-draw filter. Missing or invalid occlusion results remain all-visible.
- `BasePassModule` passes the current view's occlusion results into solid and
  wireframe-overlay base-pass command building and records the number of
  otherwise-eligible base-pass draws culled by occlusion.
- Validation passed on 2026-04-26:
  `cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.SceneRendererDeferredCore --parallel 4`;
  `ctest --preset test-debug -R "Oxygen\.Vortex\.SceneRendererDeferredCore" --output-on-failure`
  with 33/33 tests passing.

## Validation and remaining work

**Qualification:** `validated`

LLD/plan updated; result substrate, HZB tester, base-pass consumers, diagnostics facts, `vtx.occlusion.*` controls, and VortexBasic `--with-occlusion` proof scene/tooling exist. Validation: ShaderBake rebuilt 186 modules; focused `RendererCapability`, `OcclusionModule`, `SceneRendererPublication`, and `SceneRendererDeferredCore` tests passed 62/62; CDB/D3D12 audit report `out/build-ninja/analysis/vortex/occlusion/vortex-occlusion.debug-layer.report.txt` passed with 0 D3D12/DXGI errors; RenderDoc proof `vortex-occlusion.proof.report.txt` shows Stage 3 depth draws 3, Stage 5 occlusion dispatch 1, Stage 9 base-pass scene draws 2, and Stage 20 ground grid absent; manual visual confirmation approved.

**Remaining work:** No open M05B closure gap.

## 3. Current State

| Area                      | Current state                                                                | M05B action                                                          |
| ------------------------- | ---------------------------------------------------------------------------- | -------------------------------------------------------------------- |
| Screen HZB                | Current/previous closest/furthest HZB production and bindings exist.         | Reuse as the producer; request furthest HZB for occlusion.           |
| Occlusion stage directory | Placeholder directories only.                                                | Add the Vortex-native module, types, pass wrapper, and shader.       |
| Prepared scene            | Draw metadata, render items, matrices, and bounding spheres exist.           | Build candidates keyed to prepared draw indices.                     |
| Consumers                 | Base pass filters pass masks and `main_view_visible`; no HZB occlusion mask. | Add visibility-mask consumption without leaking diagnostics/options. |
| Diagnostics               | M05A ledger, manifest, and debug panel exist.                                | Publish compact occlusion counters and fallback reasons.             |

## Slice A - Architecture And Plan Authority — checks

- `rg` consistency scan for M05B and occlusion ownership references.
- `git diff --check`.

## Slice B - Visibility Result Substrate — checks

- Focused Vortex build.
- Unit tests for result substrate and fallback semantics.

## Slice C - HZB Occlusion Tester Pass — checks

- ShaderBake/catalog validation.
- Focused tests or GPU-proof harness for capacity, fallback, and result decode.
- CDB/D3D12 debug-layer audit for the pass.

## Slice D - Consumer Integration — checks

- Focused tests proving filtered draws are skipped and visible fallback keeps
  all draws.
- Runtime proof in a controlled occluder/occludee scene.

## Slice E - Diagnostics And Capture Surface — checks

- `cmake --build out\build-ninja --config Debug --target
Oxygen.Vortex.DiagnosticsCaptureManifest
Oxygen.Vortex.SceneRendererPublication
Oxygen.Vortex.SceneRendererDeferredCore --parallel 4` passed.
- `ctest --preset test-debug -R
"Oxygen\.Vortex\.(DiagnosticsCaptureManifest|SceneRendererPublication|SceneRendererDeferredCore)"
--output-on-failure` passed: 52/52.

## Slice F - Runtime Proof And Closeout — checks

- `cmake --build out\build-ninja --config Debug --target
Oxygen.Vortex.RendererCapability.Tests Oxygen.Vortex.OcclusionModule
Oxygen.Vortex.SceneRendererPublication Oxygen.Vortex.SceneRendererDeferredCore
--parallel 4` passed.
- `ctest --preset test-debug -R
"Oxygen\.Vortex\.(RendererCapability|OcclusionModule|SceneRendererPublication|SceneRendererDeferredCore)"
--output-on-failure` passed: 62/62.
- `cmake --build out\build-ninja --config Debug --target
Oxygen.Graphics.Direct3D12.ShaderBake oxygen-examples-vortexbasic
Oxygen.Vortex.OcclusionModule Oxygen.Vortex.SceneRendererDeferredCore
--parallel 4` passed; ShaderBake repacked 186 modules after the occlusion
  compute shader fix.
- CDB/D3D12 debug-layer audit passed:
  `out/build-ninja/analysis/vortex/occlusion/vortex-occlusion.debug-layer.report.txt`
  records runtime exit code 0, no debugger break, 0 D3D12 errors, 0 DXGI
  errors, and `overall_verdict=pass`.
- RenderDoc proof passed:
  `out/build-ninja/analysis/vortex/occlusion/vortex-occlusion.proof.report.txt`
  records Stage 3 depth draws 3, Stage 5 occlusion dispatch 1, and Stage 9
  base-pass draws 2 for the VortexBasic `--with-occlusion` proof scene. The
  refreshed proof also records Stage 20 ground-grid scope count 0 after the
  proof-scene cleanup.
- Manual visual confirmation approved after the proof-scene cleanup.
