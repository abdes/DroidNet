# Modular Renderer Implementation Status

**Date:** 2026-04-12
**Status:** Design / Status Report

## Final Status

The modular renderer work on this branch is complete through:

- Phase 1: capability-family vocabulary, `RenderContext` materialization,
  single-pass/render-graph/offscreen facades, publication split, queued
  composition, runtime-view ownership migration, and facade presets
- Phase 2: `EnvironmentLightingService` extraction, with environment state,
  lifecycle, publication, per-view execution, and eviction moved out of
  `Renderer`

## Implemented Outcomes

- `RendererCapabilityFamily` and pipeline capability validation are in code.
- `RenderContext` materialization for modular non-runtime paths is in code.
- `ForSinglePassHarness()`, `ForRenderGraphHarness()`, and
  `ForOffscreenScene()` are in code and covered by focused tests.
- Baseline publication and optional-family publication are split.
- Composition uses queued multi-submission, single-target execution.
- Runtime view ownership moved behind renderer-owned bridges.
- Helper presets exist for the committed facade workflows.
- `EnvironmentLightingService` owns the environment capability family and
  `Renderer` delegates to it through the retained bridge surface.

## Verification Summary

Verified on this branch with evidence:

- full debug build: `cmake --build --preset windows-debug --parallel 8`
- focused modular renderer proof surface:
  `ctest --test-dir out/build-ninja -C Debug -R "^Oxygen\\.Renderer\\.(RenderContextMaterializer|SinglePassHarnessFacade|RenderGraphHarnessFacade|RendererFacadePresets|DepthPrePassContract|DepthPrePassExecution|DepthPrePassConfig|ScreenHzb|OffscreenSceneFacade|RendererPublicationSplit|RendererCompositionQueue|ConventionalShadows|LightCullingPassContract|LightCullingPassMembership|ShaderPassDepthState|RendererCapability)\\.Tests$" --output-on-failure -j 8`
  Result: PASS (`16/16`)
- direct `OffscreenSceneFacade` suite run:
  `out/build-ninja/bin/Debug/Oxygen.Renderer.OffscreenSceneFacade.Tests.exe`
  Result: PASS (`9` tests)
- supervised runtime smoke:
  `Oxygen.Examples.RenderScene.exe -v=0 --fps 0`
  `Oxygen.Examples.MultiView.exe -v=0 --fps 0`
  Result: PASS for supervised startup/frame-loop smoke
- active-surface legacy retirement check:
  `rg -n "BeginOffscreenFrame\\(|OffscreenFrameSession" src/Oxygen/Renderer src/Oxygen/Renderer/Test -g "*.cpp" -g "*.h"`
  Result: PASS (`0` matches)

## Accepted Gaps

- `DemoShell` runtime smoke was not rerun as a required final gate.
- The current renderer bridge surface remains intentionally transitional in a
  few places (`GetShadowManager()`, `CurrentViewDynamicBindingsUpdate`, and the
  retained environment bridge methods). These are accepted cleanup candidates,
  not blockers for Phase 1 or Phase 2 completion.
- No dedicated standalone environment-service test target exists; the current
  proof stays on the active renderer-facing test surface.
