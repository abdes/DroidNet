---
phase: 04-migration-critical-services-and-first-migration
plan: "02"
workstream: vortex
requirement: POST-01
completed_at: 2026-04-17T17:37:10.7295615+04:00
commits:
  - a877e2758
  - 03544f97d
  - 136fce1e9
  - 67f604fc9
key_files:
  - src/Oxygen/Vortex/PostProcess/PostProcessService.h
  - src/Oxygen/Vortex/PostProcess/PostProcessService.cpp
  - src/Oxygen/Vortex/PostProcess/Passes/TonemapPass.h
  - src/Oxygen/Vortex/PostProcess/Passes/TonemapPass.cpp
  - src/Oxygen/Vortex/SceneRenderer/SceneRenderer.h
  - src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp
  - src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/PostProcess/Tonemap.hlsl
  - src/Oxygen/Graphics/Direct3D12/Shaders/EngineShaderCatalog.h
---

# Phase 04 Plan 02: PostProcessService Activation Summary

Stage 22 now truthfully owns the visible-output path: `SceneRenderer` resolves at
Stage 21, runs `PostProcessService` tonemap at Stage 22 into the supplied post
target, republishes `post_process_frame_slot`, and leaves Stage 23 extraction
with `PostRenderCleanup`.

## Outcome

- Added the real `PostProcessService` family under `src/Oxygen/Vortex/PostProcess/`
  with typed `PostProcessFrameBindings`, per-view publication, and pass/helper
  shells for tonemap, bloom, and exposure.
- Implemented a real `TonemapPass` that records a fullscreen Stage 22 draw named
  `Vortex.PostProcess.Tonemap`.
- Wired `SceneRenderer` to execute `ResolveSceneColor(ctx)`, then
  `post_process_->Execute(...)`, then `PostRenderCleanup(ctx)`.
- Registered the Stage 22 shader family in
  `src/Oxygen/Graphics/Direct3D12/Shaders/EngineShaderCatalog.h` and added the
  new `Vortex/Services/PostProcess/*.hlsl` sources.
- Extended proof coverage so the direct post-process execution, SceneRenderer
  ordering seam, and retained harness output-target seam are all exercised.

## Verification

- `cmake --build --preset windows-debug --target oxygen-vortex oxygen-examples-vortexbasic Oxygen.Vortex.PostProcessService.Tests Oxygen.Vortex.SceneRendererShell.Tests Oxygen.Vortex.SinglePassHarnessFacade.Tests Oxygen.Vortex.RenderGraphHarnessFacade.Tests Oxygen.Renderer.SinglePassHarnessFacade.Tests Oxygen.Renderer.RenderGraphHarnessFacade.Tests --parallel 4`
  Result: passed; ShaderBake compiled the new Stage 22 shader family and `Oxygen.Examples.VortexBasic.exe` linked.
- `ctest --preset test-debug -C Debug --output-on-failure -R "^Oxygen\\.Vortex\\.(PostProcessService|SceneRendererShell|SinglePassHarnessFacade|RenderGraphHarnessFacade)\\.Tests$|^PostProcessService|^SceneRendererShellProofSurfaceTest|^SinglePassHarnessFacadeTest|^RenderGraphHarnessFacadeTest"`
  Result: passed; 41/41 matching Vortex tests passed.
- `powershell -NoProfile -File tools/vortex/Run-VortexBasicRuntimeValidation.ps1 -Output build/artifacts/vortex/phase-4/vortexbasic/04-02`
  Result: passed; `build/artifacts/vortex/phase-4/vortexbasic/04-02.validation.txt` reports `overall_verdict=pass`, `runtime_exit_code=0`, and `final_present_nonzero=true`.

## Commits

- `a877e2758` `test(vortex-04-02): prevent Stage 22 ownership drift during migration`
- `03544f97d` `feat(vortex-04-02): preserve Stage 22 ownership with a real post-process family`
- `136fce1e9` `test(vortex-04-02): expose the missing Stage 22 visible-output path`
- `67f604fc9` `feat(vortex-04-02): route visible output through truthful Stage 22 ownership`

## Deviations from Plan

### Verification Adjustments

1. `[Rule 3 - Verification]` Used the concrete build targets `oxygen-vortex` and
   `oxygen-examples-vortexbasic` because the plan's target spellings
   `Oxygen.Vortex` and `Oxygen.Examples.VortexBasic` are not valid build targets
   in this tree.
2. `[Rule 3 - Verification]` Anchored the `ctest` regex to Vortex and passed
   `-C Debug` because the unanchored plan regex also matches unrelated
   `Oxygen.Renderer` harness registrations whose CTest entries are broken in
   this workspace. The Vortex-owned verification slice passed.

## Known Stubs

- `src/Oxygen/Vortex/PostProcess/Passes/ExposurePass.cpp:28`
  Stage 22 exposure currently uses the fixed-exposure-safe fallback even when
  auto-exposure is enabled; histogram/adaptation work is still deferred.
- `src/Oxygen/Vortex/PostProcess/Passes/BloomPass.cpp:26`
  Bloom ownership exists, but the service does not build a bloom chain yet; it
  only consumes a published bloom texture if one already exists.
- `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/PostProcess/BloomDownsample.hlsl:10`
  Shader catalog placeholder for the future bloom chain.
- `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/PostProcess/BloomUpsample.hlsl:10`
  Shader catalog placeholder for the future bloom chain.
- `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/PostProcess/Exposure.hlsl:8`
  Shader catalog placeholder for future histogram-based exposure.

## Threat Flags

None.

## Self-Check

PASSED
