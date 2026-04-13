# Vortex Renderer Implementation Status

Status: `done — Phase 1 substrate migration is complete with the post-orchestrator FOUND-03 proof, the Vortex step-1.9 smoke path, and the targeted legacy substrate regressions recorded`

This document is the **running resumability ledger** for the Vortex renderer.
It records what is actually in the repo, what has been verified, what is still
missing, and exactly where to resume. All claims must be evidence-backed.

Related:

- [PRD.md](./PRD.md) — stable product requirements
- [ARCHITECTURE.md](./ARCHITECTURE.md) — stable architecture
- [DESIGN.md](./DESIGN.md) — evolving LLD (early draft)
- [PROJECT-LAYOUT.md](./PROJECT-LAYOUT.md) — authoritative file layout
- [PLAN.md](./PLAN.md) — active execution plan

## Ledger Rules

1. **Evidence, not intention.** Every entry records what exists in code and
   what validation was run, not what was planned or discussed.
2. **No-false-completion.** A phase is `done` only when: code exists,
   required docs are updated, and validation evidence is recorded here.
3. **Missing-delta explicit.** If build or tests were not run, the phase
   stays `in_progress` with the missing validation delta listed.
4. **Scope-drift trigger.** If scope changes or the current design is found
   incomplete, update design docs before claiming further progress.
5. **Per-session update.** Each implementation session must update this file
   with: changed files, commands run, results, and remaining blockers.

## Phase Summary

| Phase | Name | Status | Blocker |
| ----- | ---- | ------ | ------- |
| 0 | Scaffold and Build Integration | `done` | — |
| 1 | Substrate Migration | `done` | — |
| 2 | SceneTextures + SceneRenderer Shell | `not_started` | Execution not started |
| 3 | Deferred Core | `not_started` | Phase 2 + 5 LLD documents |
| 4 | Migration-Critical Services + First Migration | `not_started` | Phase 3 + per-service LLDs |
| 5 | Remaining Services + Runtime Scenarios | `not_started` | Phase 4 + per-service/scenario LLDs |
| 6 | Legacy Deprecation | `not_started` | Phase 5 |
| 7 | Future Capabilities (post-release) | `not_started` | Phase 6 |

## Design Deliverable Tracker

Each design deliverable required by PLAN.md is tracked here. A phase's
implementation cannot begin until its design prerequisites are met.

| ID | Deliverable | Required By | Status | Location |
| -- | ----------- | ----------- | ------ | -------- |
| D.1 | SceneTextures four-part contract | Phase 2 | `done` | [`lld/scene-textures.md`](lld/scene-textures.md) |
| D.2 | SceneRenderBuilder bootstrap | Phase 2 | `done` | [`lld/scene-renderer-shell.md`](lld/scene-renderer-shell.md) |
| D.3 | SceneRenderer shell dispatch | Phase 2 | `done` | [`lld/scene-renderer-shell.md`](lld/scene-renderer-shell.md) |
| D.4 | Depth prepass LLD | Phase 3 | `done` | [`lld/depth-prepass.md`](lld/depth-prepass.md) |
| D.5 | Base pass LLD | Phase 3 | `done` | [`lld/base-pass.md`](lld/base-pass.md) |
| D.6 | Deferred lighting LLD | Phase 3 | `done` | [`lld/deferred-lighting.md`](lld/deferred-lighting.md) |
| D.7 | Shader contracts LLD | Phase 3 | `done` | [`lld/shader-contracts.md`](lld/shader-contracts.md) |
| D.8 | InitViews LLD | Phase 3 | `done` | [`lld/init-views.md`](lld/init-views.md) |
| D.9 | LightingService LLD | Phase 4A | `done` | [`lld/lighting-service.md`](lld/lighting-service.md) |
| D.10 | PostProcessService LLD | Phase 4B | `done` | [`lld/post-process-service.md`](lld/post-process-service.md) |
| D.11 | ShadowService LLD | Phase 4C | `done` | [`lld/shadow-service.md`](lld/shadow-service.md) |
| D.12 | EnvironmentLightingService LLD | Phase 4D | `done` | [`lld/environment-service.md`](lld/environment-service.md) |
| D.13 | Migration playbook | Phase 4E | `done` | [`lld/migration-playbook.md`](lld/migration-playbook.md) |
| D.14 | DiagnosticsService LLD | Phase 5A | `done` | [`lld/diagnostics-service.md`](lld/diagnostics-service.md) |
| D.15 | TranslucencyModule LLD | Phase 5B | `done` | [`lld/translucency.md`](lld/translucency.md) |
| D.16 | OcclusionModule LLD | Phase 5C | `done` | [`lld/occlusion.md`](lld/occlusion.md) |
| D.17 | Multi-view composition LLD | Phase 5D | `done` | [`lld/multi-view-composition.md`](lld/multi-view-composition.md) |
| D.18 | Offscreen rendering LLD | Phase 5E | `done` | [`lld/offscreen-rendering.md`](lld/offscreen-rendering.md) |

---

## Documentation Sync Log

### 2026-04-13 — Phase 1 plan 01-11 closed step 1.9 and the Phase 1 exit gate

- Changed files this session:
  - `src/Oxygen/Vortex/Test/CMakeLists.txt`
  - `src/Oxygen/Vortex/Test/Fakes/Graphics.h`
  - `src/Oxygen/Vortex/Test/Link_test.cpp`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - `powershell -NoProfile -Command "cmake --build --preset windows-debug --target oxygen-vortex Oxygen.Vortex.LinkTest --parallel 4; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }; ctest --test-dir out/build-ninja -C Debug -R '^Oxygen\\.Vortex\\.LinkTest$' --output-on-failure"`
  - `powershell -NoProfile -Command "cmake --build --preset windows-debug --parallel 4"`
  - `ctest --test-dir out/build-ninja -C Debug --output-on-failure -R 'Oxygen\\.Renderer\\.(LinkTest|CompositionPlanner\\.Tests|SceneCameraViewResolver\\.Tests|RenderContext\\.Tests|RenderContextMaterializer\\.Tests|RendererCapability\\.Tests|RendererCompositionQueue\\.Tests|RendererPublicationSplit\\.Tests|RendererFacadePresets\\.Tests|SinglePassHarnessFacade\\.Tests|RenderGraphHarnessFacade\\.Tests|OffscreenSceneFacade\\.Tests|GpuTimelineProfiler\\.Tests|LightCullingConfig\\.Tests|ScenePrep\\.Tests|UploadCoordinator\\.Tests|RingBufferStaging\\.Tests|UploadTracker\\.Tests|AtlasBuffer\\.Tests|UploadPlanner\\.Tests|TransientStructuredBuffer\\.Tests|TextureBinder\\.Tests|MaterialBinder\\.Tests|TransformUploader\\.Tests|DrawMetadataEmitter\\.Tests)'`
- Result:
  - `Oxygen.Vortex.LinkTest` now uses a Vortex-local backend-free fake graphics harness, constructs `oxygen::vortex::Renderer` with an empty capability set, and drives `OnFrameStart`, `OnTransformPropagation`, `OnPreRender`, `OnRender`, `OnCompositing`, and `OnFrameEnd`
  - the Vortex smoke path passed through `ctest`, proving the stripped renderer frame hooks execute without relying on `Oxygen.Renderer`
  - the targeted legacy substrate regression suite passed after the post-orchestrator `FOUND-03` dependency-edge proof from `01-10`
  - the generated workspace does not define `ctest --preset windows-debug`, so the regression gate was run with the equivalent Debug build-tree invocation `ctest --test-dir out/build-ninja -C Debug ...`
- Code / validation delta:
  - step `1.9` is now **complete**
  - `FOUND-02` and `FOUND-03` now have the Phase 1 smoke/regression evidence required for the exit gate
  - Phase 1 is now **complete**
- Remaining blocker:
  - none for Phase 1; resume with Phase 2 execution when ready
### 2026-04-13 — Phase 1 plan 01-10 landed step 1.6, the stripped renderer orchestrator, and the final FOUND-03 proof

- Changed files this session:
  - `src/Oxygen/Vortex/CMakeLists.txt`
  - `src/Oxygen/Vortex/Errors.h`
  - `src/Oxygen/Vortex/FacadePresets.h`
  - `src/Oxygen/Vortex/Internal/ViewLifecycleService.h`
  - `src/Oxygen/Vortex/Passes/ComputeRenderPass.cpp`
  - `src/Oxygen/Vortex/Passes/ComputeRenderPass.h`
  - `src/Oxygen/Vortex/Passes/GraphicsRenderPass.cpp`
  - `src/Oxygen/Vortex/Passes/GraphicsRenderPass.h`
  - `src/Oxygen/Vortex/Passes/RenderPass.cpp`
  - `src/Oxygen/Vortex/Passes/RenderPass.h`
  - `src/Oxygen/Vortex/RenderContext.h`
  - `src/Oxygen/Vortex/Renderer.cpp`
  - `src/Oxygen/Vortex/Renderer.h`
  - `src/Oxygen/Vortex/SceneCameraViewResolver.cpp`
  - `src/Oxygen/Vortex/SceneCameraViewResolver.h`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - `rg -n 'Oxygen/Renderer/|OXGN_RNDR_|LightManager|ShadowManager|EnvironmentLightingService' src/Oxygen/Vortex/Errors.h src/Oxygen/Vortex/FacadePresets.h src/Oxygen/Vortex/Passes/ComputeRenderPass.cpp src/Oxygen/Vortex/Passes/ComputeRenderPass.h src/Oxygen/Vortex/Passes/GraphicsRenderPass.cpp src/Oxygen/Vortex/Passes/GraphicsRenderPass.h src/Oxygen/Vortex/Passes/RenderPass.cpp src/Oxygen/Vortex/Passes/RenderPass.h src/Oxygen/Vortex/RenderContext.h src/Oxygen/Vortex/SceneCameraViewResolver.cpp src/Oxygen/Vortex/SceneCameraViewResolver.h`
  - `rg -n 'ForwardPipeline|RenderingPipeline|LightManager|ShadowManager|EnvironmentLightingService|Oxygen/Renderer/|OXGN_RNDR_' src/Oxygen/Vortex/Renderer.h src/Oxygen/Vortex/Renderer.cpp`
  - `cmake --build --preset windows-debug --target oxygen-vortex --parallel 4`
  - `cmake --preset windows-default`
  - `powershell -NoProfile -Command "$ninja = (Select-String -Path 'out/build-ninja/CMakeCache.txt' -Pattern '^CMAKE_MAKE_PROGRAM:FILEPATH=(.+)$').Matches[0].Groups[1].Value; & $ninja -C out/build-ninja -f build-Debug.ninja -t query bin/Debug/Oxygen.Vortex-d.dll"`
- Result:
  - the remaining Phase 1 root-support files now live under Vortex ownership as `Errors.h`, `FacadePresets.h`, `RenderContext.h`, and `SceneCameraViewResolver.cpp/.h`
  - the step-`1.6` pass bases now live under `src/Oxygen/Vortex/Passes/` and compile against Vortex-owned `RenderContext` / `Renderer` contracts instead of the legacy renderer namespace
  - `Renderer.h/.cpp` now provide a stripped substrate-only Vortex renderer shell with no `ForwardPipeline`, `RenderingPipeline`, `LightManager`, `ShadowManager`, or `EnvironmentLightingService` ownership
  - `oxygen-vortex` builds successfully in Debug after the stripped renderer orchestrator lands
  - the final Debug Ninja target query for `bin/Debug/Oxygen.Vortex-d.dll` lists only Vortex objects plus non-renderer engine dependencies; it contains no `oxygen-renderer` / `Oxygen.Renderer` dependency edge after the orchestrator landed
- Code / validation delta:
  - steps `1.6` and `1.8` are now **complete**
  - `FOUND-03` now has the final post-orchestrator dependency-edge proof required by `01-10`
  - Phase 1 remains `in_progress` because step `1.9` smoke plus the legacy substrate regression gate are still owned by `01-11`
- Remaining blocker:
  - execute `01-11` to run the Vortex smoke path and the targeted legacy substrate regressions before claiming Phase 1 complete

### 2026-04-13 — Phase 1 plan 01-09 closed the private half of step 1.7

- Changed files this session:
  - `src/Oxygen/Vortex/CMakeLists.txt`
  - `src/Oxygen/Vortex/Internal/CompositionPlanner.cpp`
  - `src/Oxygen/Vortex/Internal/CompositionPlanner.h`
  - `src/Oxygen/Vortex/Internal/CompositionViewImpl.cpp`
  - `src/Oxygen/Vortex/Internal/CompositionViewImpl.h`
  - `src/Oxygen/Vortex/Internal/FrameViewPacket.cpp`
  - `src/Oxygen/Vortex/Internal/FrameViewPacket.h`
  - `src/Oxygen/Vortex/Internal/ViewLifecycleService.cpp`
  - `src/Oxygen/Vortex/Internal/ViewLifecycleService.h`
  - `src/Oxygen/Vortex/SceneRenderer/Internal/FramePlanBuilder.cpp`
  - `src/Oxygen/Vortex/SceneRenderer/Internal/FramePlanBuilder.h`
  - `src/Oxygen/Vortex/SceneRenderer/Internal/ViewRenderPlan.h`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - `rg -n 'Renderer/Pipeline|PipelineSettings|Oxygen/Renderer/|OXGN_RNDR_' src/Oxygen/Vortex/Internal/CompositionPlanner.cpp src/Oxygen/Vortex/Internal/CompositionPlanner.h src/Oxygen/Vortex/Internal/CompositionViewImpl.cpp src/Oxygen/Vortex/Internal/CompositionViewImpl.h src/Oxygen/Vortex/Internal/FrameViewPacket.cpp src/Oxygen/Vortex/Internal/FrameViewPacket.h src/Oxygen/Vortex/Internal/ViewLifecycleService.cpp src/Oxygen/Vortex/Internal/ViewLifecycleService.h`
  - `cmake --build --preset windows-debug --target oxygen-vortex --parallel 4`
  - `rg -n '\| 1\.7 \||FramePlanBuilder.cpp|ViewRenderPlan.h|01-10' design/vortex/IMPLEMENTATION-STATUS.md`
- Result:
  - the renderer-core private step-`1.7` files now live under `src/Oxygen/Vortex/Internal/`
  - `FramePlanBuilder.cpp/.h` and `ViewRenderPlan.h` now live under `src/Oxygen/Vortex/SceneRenderer/Internal/`, matching `PROJECT-LAYOUT.md`
  - `FramePlanBuilder` no longer owns the discarded `PipelineSettings` type; it now consumes a local planning input shape scoped to the scene-renderer private slice
  - `oxygen-vortex` builds successfully in Debug after the full private step-`1.7` rehome lands
- Code / validation delta:
  - step `1.7` is now **complete**
  - step `1.6` remains deferred to `01-10`; this plan did not pull the pass bases, `RenderContext.h`, or `Renderer.h/.cpp` forward
  - no Vortex runtime/facade validation was run in this plan
- Remaining blocker:
  - execute repaired `01-10` to land step `1.6`, the remaining root-support files, the stripped renderer orchestrator, and the final post-orchestrator dependency-edge proof

### 2026-04-13 — Phase 1 plan 01-08 landed the public step-1.7 header slice

- Changed files this session:
  - `src/Oxygen/Vortex/CMakeLists.txt`
  - `src/Oxygen/Vortex/CompositionView.h`
  - `src/Oxygen/Vortex/RendererCapability.h`
  - `src/Oxygen/Vortex/RenderMode.h`
  - `src/Oxygen/Vortex/SceneRenderer/DepthPrePassPolicy.h`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - `rg -n 'RenderingPipeline|PipelineFeature|PipelineSettings|Oxygen/Renderer/|OXGN_RNDR_' src/Oxygen/Vortex/CompositionView.h src/Oxygen/Vortex/RendererCapability.h src/Oxygen/Vortex/RenderMode.h`
  - `rg -n 'CompositionView.h|RendererCapability.h|RenderMode.h' src/Oxygen/Vortex/CMakeLists.txt`
  - `rg -n 'Oxygen/Renderer/|OXGN_RNDR_' src/Oxygen/Vortex/SceneRenderer/DepthPrePassPolicy.h`
  - `cmake --build --preset windows-debug --target oxygen-vortex --parallel 4`
  - `rg -n '\| 1\.6 \||\| 1\.7 \||01-09|01-10' design/vortex/IMPLEMENTATION-STATUS.md`
- Result:
  - the public step-`1.7` root vocabulary now lives under `src/Oxygen/Vortex/` as `CompositionView.h`, `RendererCapability.h`, and `RenderMode.h`
  - `DepthPrePassPolicy.h` now lives under `src/Oxygen/Vortex/SceneRenderer/`, matching `PROJECT-LAYOUT.md`
  - `src/Oxygen/Vortex/CMakeLists.txt` lists only the public step-`1.7` header slice added by `01-08`
  - `oxygen-vortex` builds successfully in Debug after the public header slice lands
- Code / validation delta:
  - step `1.7` is now **in progress**: the public header half is complete, but the private composition infrastructure remains deferred to `01-09`
  - step `1.6` remains deferred to `01-10`; this plan did not pull the pass bases, `RenderContext.h`, or `Renderer.h/.cpp` forward
  - no Vortex runtime/facade validation was run in this plan
- Remaining blocker:
  - execute repaired `01-09` to land the private half of step `1.7`, then `01-10` to land step `1.6` with the later root-contract wave

### 2026-04-13 — Phase 1 plan 01-07 migrated the ScenePrep execution slice and selected substrate-only internals

- Changed files this session:
  - `src/Oxygen/Vortex/CMakeLists.txt`
  - `src/Oxygen/Vortex/Internal/BlueNoiseData.h`
  - `src/Oxygen/Vortex/Internal/PerViewStructuredPublisher.h`
  - `src/Oxygen/Vortex/Internal/RenderContextMaterializer.h`
  - `src/Oxygen/Vortex/Internal/RenderContextPool.h`
  - `src/Oxygen/Vortex/Internal/RenderScope.h`
  - `src/Oxygen/Vortex/Internal/ViewConstantsManager.cpp`
  - `src/Oxygen/Vortex/Internal/ViewConstantsManager.h`
  - `src/Oxygen/Vortex/ScenePrep/Extractors.h`
  - `src/Oxygen/Vortex/ScenePrep/Finalizers.h`
  - `src/Oxygen/Vortex/ScenePrep/ScenePrepPipeline.cpp`
  - `src/Oxygen/Vortex/ScenePrep/ScenePrepPipeline.h`
  - `src/Oxygen/Vortex/ScenePrep/ScenePrepState.h`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - `rg -n 'Oxygen/Renderer/|OXGN_RNDR_' src/Oxygen/Vortex/ScenePrep/Extractors.h src/Oxygen/Vortex/ScenePrep/Finalizers.h src/Oxygen/Vortex/ScenePrep/ScenePrepPipeline.cpp src/Oxygen/Vortex/ScenePrep/ScenePrepPipeline.h`
  - `rg -n 'ScenePrep/ScenePrepPipeline.cpp|ScenePrep/Extractors.h|ScenePrep/Finalizers.h' src/Oxygen/Vortex/CMakeLists.txt`
  - `rg -n 'Oxygen/Renderer/|OXGN_RNDR_|IIblProvider|ISkyCaptureProvider|ISkyAtmosphereLutProvider|IBrdfLutProvider|IblManager|SunResolver|EnvironmentStaticDataManager|ConventionalShadow' src/Oxygen/Vortex/Internal/BlueNoiseData.h src/Oxygen/Vortex/Internal/PerViewStructuredPublisher.h src/Oxygen/Vortex/Internal/RenderContextMaterializer.h src/Oxygen/Vortex/Internal/RenderContextPool.h src/Oxygen/Vortex/Internal/RenderScope.h src/Oxygen/Vortex/Internal/ViewConstantsManager.cpp src/Oxygen/Vortex/Internal/ViewConstantsManager.h`
  - `cmake --build --preset windows-debug --target oxygen-vortex --parallel 4`
  - `cmake --preset windows-default`
  - `D:/dev/ninja/ninja.exe -C out/build-ninja -f build-Debug.ninja -t query bin/Debug/Oxygen.Vortex-d.dll`
- Result:
  - `src/Oxygen/Vortex/ScenePrep/Extractors.h`, `Finalizers.h`, and `ScenePrepPipeline.cpp/.h` now live under Vortex ownership, so the deferred execution half of step `1.4` is no longer outstanding
  - the selected substrate-only utility slice now lives under `src/Oxygen/Vortex/Internal/` without importing light, shadow, or environment-specific internals
  - `RenderContextPool.h` and `RenderContextMaterializer.h` were moved into Vortex ownership as dependency-safe templates so `01-07` did not have to pull `RenderContext.h` or `Renderer.h` forward from `01-10`
  - the stale `LightManager` hook was removed from the Vortex `ScenePrep` state/pipeline because compiling the new execution slice exposed it as a later-domain dependency outside the Phase 1 substrate boundary
  - `oxygen-vortex` builds successfully in Debug after the ScenePrep execution and selected internal utility slice land
  - the generated Debug Ninja target query for `bin/Debug/Oxygen.Vortex-d.dll` still shows no `oxygen-renderer` / `Oxygen.Renderer` dependency edge
- Code / validation delta:
  - steps `1.4` and `1.5` are now complete with build and dependency-edge evidence
  - no Vortex runtime/facade validation was run in this plan
- Remaining blocker:
  - execute repaired `01-08` to land only the public half of step `1.7`, then `01-09`, then `01-10` to land step `1.6` with the later root-contract wave

### 2026-04-13 — Phase 1 tail repaired after the `01-08` pass-base/root-contract blocker

- Changed files this session:
  - `.planning/workstreams/vortex/ROADMAP.md`
  - `.planning/workstreams/vortex/STATE.md`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/.continue-here.md`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-08-PLAN.md`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-09-PLAN.md`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-10-PLAN.md`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-11-PLAN.md`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-VALIDATION.md`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - `Get-Content .planning/workstreams/vortex/phases/01-substrate-migration/01-08-PLAN.md`
  - `Get-Content .planning/workstreams/vortex/phases/01-substrate-migration/01-09-PLAN.md`
  - `Get-Content .planning/workstreams/vortex/phases/01-substrate-migration/01-10-PLAN.md`
  - `Get-Content .planning/workstreams/vortex/phases/01-substrate-migration/01-11-PLAN.md`
  - `Get-Content .planning/workstreams/vortex/phases/01-substrate-migration/01-VALIDATION.md`
  - `Get-Content .planning/workstreams/vortex/ROADMAP.md`
  - `Get-Content .planning/workstreams/vortex/STATE.md`
  - `Get-Content .planning/workstreams/vortex/phases/01-substrate-migration/.continue-here.md`
  - `Get-Content design/vortex/PLAN.md`
  - `Get-Content design/vortex/PROJECT-LAYOUT.md`
  - `Get-Content src/Oxygen/Renderer/Passes/RenderPass.cpp`
  - `Get-Content src/Oxygen/Renderer/Passes/GraphicsRenderPass.cpp`
  - `Get-Content src/Oxygen/Renderer/Passes/ComputeRenderPass.cpp`
- Result:
  - `01-08` now owns only `CompositionView.h`, `RendererCapability.h`, `RenderMode.h`, and `SceneRenderer/DepthPrePassPolicy.h`
  - the plan set no longer claims the step-`1.6` pass bases can land before Vortex owns `RenderContext` / `Renderer`
  - `01-09` still closes the private half of step `1.7`
  - `01-10` now owns the step-`1.6` pass bases together with the later-wave root contracts, stripped renderer orchestrator, and final post-orchestrator dependency-edge proof
  - the roadmap, validation contract, resume note, and state file now all point execution back to the completed-state handoff at `01-08`
- Code / validation delta:
  - no implementation code changed
  - no build or tests were run during this planning repair
  - Phase 1 remains `in_progress`; the next executable plan is repaired `01-08`
- Remaining blocker:
  - execute repaired `01-08`, `01-09`, and `01-10` in order from the current completed state at `01-07`

### 2026-04-13 — Phase 1 plan 01-06 migrated the remaining ScenePrep-only data/config slice

- Changed files this session:
  - `src/Oxygen/Vortex/CMakeLists.txt`
  - `src/Oxygen/Vortex/ScenePrep/CollectionConfig.h`
  - `src/Oxygen/Vortex/ScenePrep/Concepts.h`
  - `src/Oxygen/Vortex/ScenePrep/FinalizationConfig.h`
  - `src/Oxygen/Vortex/ScenePrep/RenderItemProto.h`
  - `src/Oxygen/Vortex/ScenePrep/ScenePrepContext.h`
  - `src/Oxygen/Vortex/ScenePrep/ScenePrepState.h`
  - `src/Oxygen/Vortex/ScenePrep/Types.h`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - `rg -n 'Oxygen/Renderer/|OXGN_RNDR_' src/Oxygen/Vortex/ScenePrep/CollectionConfig.h src/Oxygen/Vortex/ScenePrep/Concepts.h src/Oxygen/Vortex/ScenePrep/FinalizationConfig.h src/Oxygen/Vortex/ScenePrep/RenderItemProto.h src/Oxygen/Vortex/ScenePrep/ScenePrepContext.h src/Oxygen/Vortex/ScenePrep/ScenePrepState.h src/Oxygen/Vortex/ScenePrep/Types.h`
  - `rg -n 'ScenePrep/CollectionConfig.h|ScenePrep/ScenePrepContext.h|ScenePrep/Types.h' src/Oxygen/Vortex/CMakeLists.txt`
  - `cmake --build --preset windows-debug --target oxygen-vortex --parallel 4`
  - `cmake --preset windows-default`
  - `D:/dev/ninja/ninja.exe -C out/build-ninja -f build-Debug.ninja -t query bin/Debug/Oxygen.Vortex-d.dll`
- Result:
  - the remaining ScenePrep-only data/config contracts now live under `src/Oxygen/Vortex/ScenePrep/`
  - `src/Oxygen/Vortex/CMakeLists.txt` now exports those headers without re-owning the ABI files already landed by `01-04`
  - `CollectionConfig.h` and `FinalizationConfig.h` keep the deferred execution entry points as forward declarations instead of widening `01-06` into `Extractors.h`, `Finalizers.h`, or `ScenePrepPipeline.*`
  - `oxygen-vortex` builds successfully in Debug after the ScenePrep-only header slice lands
  - the generated Debug Ninja target query for `bin/Debug/Oxygen.Vortex-d.dll` still shows no `oxygen-renderer` / `Oxygen.Renderer` dependency edge
- Code / validation delta:
  - step `1.4` is now **in progress**: the ScenePrep-only data/config slice is complete, but `ScenePrep/Extractors.h`, `ScenePrep/Finalizers.h`, and `ScenePrepPipeline.cpp/.h` remain deferred to `01-07`
  - no Vortex/legacy link-test or runtime validation was run in this plan
- Remaining blocker:
  - execute repaired `01-07` to land `ScenePrep/Extractors.h`, `ScenePrep/Finalizers.h`, and `ScenePrepPipeline.cpp/.h`, then close step `1.4`

### 2026-04-13 — Phase 1 plan 01-05 migrated `Resources/*` and closed step 1.3

- Changed files this session:
  - `src/Oxygen/Vortex/CMakeLists.txt`
  - `src/Oxygen/Vortex/Resources/DrawMetadataEmitter.cpp`
  - `src/Oxygen/Vortex/Resources/DrawMetadataEmitter.h`
  - `src/Oxygen/Vortex/Resources/GeometryUploader.cpp`
  - `src/Oxygen/Vortex/Resources/GeometryUploader.h`
  - `src/Oxygen/Vortex/Resources/IResourceBinder.h`
  - `src/Oxygen/Vortex/Resources/MaterialBinder.cpp`
  - `src/Oxygen/Vortex/Resources/MaterialBinder.h`
  - `src/Oxygen/Vortex/Resources/TextureBinder.cpp`
  - `src/Oxygen/Vortex/Resources/TextureBinder.h`
  - `src/Oxygen/Vortex/Resources/TransformUploader.cpp`
  - `src/Oxygen/Vortex/Resources/TransformUploader.h`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - `rg -n 'Oxygen/Renderer/|OXGN_RNDR_' src/Oxygen/Vortex/Resources/DrawMetadataEmitter.cpp src/Oxygen/Vortex/Resources/DrawMetadataEmitter.h src/Oxygen/Vortex/Resources/GeometryUploader.cpp src/Oxygen/Vortex/Resources/GeometryUploader.h src/Oxygen/Vortex/Resources/IResourceBinder.h src/Oxygen/Vortex/Resources/MaterialBinder.cpp src/Oxygen/Vortex/Resources/MaterialBinder.h src/Oxygen/Vortex/Resources/TextureBinder.cpp src/Oxygen/Vortex/Resources/TextureBinder.h src/Oxygen/Vortex/Resources/TransformUploader.cpp src/Oxygen/Vortex/Resources/TransformUploader.h`
  - `rg -n 'ScenePrepState.h|ScenePrep/Types.h' src/Oxygen/Vortex/Resources/DrawMetadataEmitter.h src/Oxygen/Vortex/Resources/DrawMetadataEmitter.cpp`
  - `cmake --build --preset windows-debug --target oxygen-vortex --parallel 4`
  - `cmake --preset windows-default`
  - `powershell -NoProfile -Command \"$ninja = (Select-String -Path 'out/build-ninja/CMakeCache.txt' -Pattern '^CMAKE_MAKE_PROGRAM:FILEPATH=(.+)$').Matches[0].Groups[1].Value; $query = & $ninja -C out/build-ninja -f build-Debug.ninja -t query bin/Debug/Oxygen.Vortex-d.dll 2>&1; if ($query -match 'oxygen-renderer|Oxygen\\.Renderer') { Write-Error 'oxygen-vortex target still depends on Oxygen.Renderer'; exit 1 }\"`
- Result:
  - the full step-`1.3` resource subsystem now lives under `src/Oxygen/Vortex/Resources/`
  - `src/Oxygen/Vortex/CMakeLists.txt` now wires the migrated resource files into `oxygen-vortex`
  - the stale `ScenePrepState.h` and `ScenePrep/Types.h` includes were removed instead of widening the repaired prerequisite boundary
  - `oxygen-vortex` builds successfully in Debug after the resource slice lands
  - the generated Debug Ninja target query for `bin/Debug/Oxygen.Vortex-d.dll` shows no `oxygen-renderer` / `Oxygen.Renderer` dependency edge
- Code / validation delta:
  - step `1.3` is now **complete**
  - no Vortex/legacy link-test or runtime validation was run in this plan
- Remaining blocker:
  - execute repaired `01-06` to migrate the remaining ScenePrep-only data/config files

### 2026-04-13 — Phase 1 plan 01-04 landed the prerequisite ABI bundle for resources

- Changed files this session:
  - `src/Oxygen/Vortex/CMakeLists.txt`
  - `src/Oxygen/Vortex/PreparedSceneFrame.cpp`
  - `src/Oxygen/Vortex/PreparedSceneFrame.h`
  - `src/Oxygen/Vortex/ScenePrep/GeometryRef.h`
  - `src/Oxygen/Vortex/ScenePrep/Handles.h`
  - `src/Oxygen/Vortex/ScenePrep/MaterialRef.h`
  - `src/Oxygen/Vortex/ScenePrep/RenderItemData.h`
  - `src/Oxygen/Vortex/Types/ConventionalShadowDrawRecord.h`
  - `src/Oxygen/Vortex/Types/DrawMetadata.h`
  - `src/Oxygen/Vortex/Types/MaterialShadingConstants.h`
  - `src/Oxygen/Vortex/Types/PassMask.h`
  - `src/Oxygen/Vortex/Types/ProceduralGridMaterialConstants.h`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - `rg -n 'Oxygen/Renderer/|OXGN_RNDR_' src/Oxygen/Vortex/Types/ConventionalShadowDrawRecord.h src/Oxygen/Vortex/Types/DrawMetadata.h src/Oxygen/Vortex/Types/MaterialShadingConstants.h src/Oxygen/Vortex/Types/PassMask.h src/Oxygen/Vortex/Types/ProceduralGridMaterialConstants.h`
  - `rg -n 'Oxygen/Renderer/|OXGN_RNDR_' src/Oxygen/Vortex/PreparedSceneFrame.cpp src/Oxygen/Vortex/PreparedSceneFrame.h src/Oxygen/Vortex/ScenePrep/GeometryRef.h src/Oxygen/Vortex/ScenePrep/Handles.h src/Oxygen/Vortex/ScenePrep/MaterialRef.h src/Oxygen/Vortex/ScenePrep/RenderItemData.h`
  - `cmake --build --preset windows-debug --target oxygen-vortex --parallel 4`
  - `rg -n 'PreparedSceneFrame.cpp|ScenePrep/Handles.h|ScenePrep/RenderItemData.h' src/Oxygen/Vortex/CMakeLists.txt`
  - `rg -n 'prerequisite ABI|01-05|step 1\\.3' design/vortex/IMPLEMENTATION-STATUS.md`
- Result:
  - the full prerequisite ABI bundle required by `Resources/*` now lives under
    `src/Oxygen/Vortex/`: `PreparedSceneFrame.h/.cpp`,
    `ScenePrep/GeometryRef.h`, `ScenePrep/Handles.h`,
    `ScenePrep/MaterialRef.h`, `ScenePrep/RenderItemData.h`,
    `Types/PassMask.h`, `Types/DrawMetadata.h`,
    `Types/MaterialShadingConstants.h`,
    `Types/ProceduralGridMaterialConstants.h`, and
    `Types/ConventionalShadowDrawRecord.h`
  - `src/Oxygen/Vortex/CMakeLists.txt` now wires the complete prerequisite
    ABI bundle into `oxygen-vortex` while still leaving `Resources/*` out of
    the target for repaired `01-05`
  - `oxygen-vortex` builds successfully in Debug with the prerequisite bundle
    alone
  - the Vortex target emits an IDE warning listing `Resources/*` files that
    exist on disk but are not yet part of the target; this is expected at the
    repaired `01-04` boundary and confirms those files remain deferred to
    `01-05`
- Code / validation delta:
  - repaired `01-04` is now **complete**
  - step `1.3` remains open because no `Resources/*` implementation files were
    added to the Vortex target in this plan
  - no Vortex/legacy link-test or runtime validation was run in this plan
- Remaining blocker:
  - execute repaired `01-05` to migrate `Resources/*` on top of the landed
    prerequisite ABI bundle

### 2026-04-13 — Phase 1 plan repair resolved the `01-04` prerequisite blocker

- Changed files this session:
  - `.planning/workstreams/vortex/ROADMAP.md`
  - `.planning/workstreams/vortex/STATE.md`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/.continue-here.md`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-04-PLAN.md`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-05-PLAN.md`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-06-PLAN.md`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-07-PLAN.md`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-08-PLAN.md`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-09-PLAN.md`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-10-PLAN.md`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-09-PLAN.md`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-VALIDATION.md`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - `Get-Content .planning/workstreams/vortex/phases/01-substrate-migration/01-04-PLAN.md`
  - `Get-Content .planning/workstreams/vortex/phases/01-substrate-migration/01-05-PLAN.md`
  - `Get-Content .planning/workstreams/vortex/phases/01-substrate-migration/01-06-PLAN.md`
  - `Get-Content .planning/workstreams/vortex/phases/01-substrate-migration/01-07-PLAN.md`
  - `Get-Content .planning/workstreams/vortex/phases/01-substrate-migration/01-08-PLAN.md`
  - `Get-Content .planning/workstreams/vortex/phases/01-substrate-migration/01-09-PLAN.md`
  - `Get-Content .planning/workstreams/vortex/phases/01-substrate-migration/01-10-PLAN.md`
  - `Get-Content .planning/workstreams/vortex/phases/01-substrate-migration/01-RESEARCH.md`
  - `Get-Content design/vortex/PLAN.md`
  - `Get-Content design/vortex/PROJECT-LAYOUT.md`
  - `Get-Content .planning/workstreams/vortex/ROADMAP.md`
  - `Get-Content .planning/workstreams/vortex/phases/01-substrate-migration/01-VALIDATION.md`
  - `node "$HOME/.codex/get-shit-done/bin/gsd-tools.cjs" verify plan-structure ".planning/workstreams/vortex/phases/01-substrate-migration/01-04-PLAN.md"`
  - `node "$HOME/.codex/get-shit-done/bin/gsd-tools.cjs" verify plan-structure ".planning/workstreams/vortex/phases/01-substrate-migration/01-05-PLAN.md"`
  - `node "$HOME/.codex/get-shit-done/bin/gsd-tools.cjs" verify plan-structure ".planning/workstreams/vortex/phases/01-substrate-migration/01-10-PLAN.md"`
  - `node "$HOME/.codex/get-shit-done/bin/gsd-tools.cjs" verify plan-structure ".planning/workstreams/vortex/phases/01-substrate-migration/01-04-PLAN.md"`
  - `node "$HOME/.codex/get-shit-done/bin/gsd-tools.cjs" verify plan-structure ".planning/workstreams/vortex/phases/01-substrate-migration/01-05-PLAN.md"`
  - `node "$HOME/.codex/get-shit-done/bin/gsd-tools.cjs" verify plan-structure ".planning/workstreams/vortex/phases/01-substrate-migration/01-09-PLAN.md"`
- Result:
  - `01-04` now truthfully owns only the prerequisite ABI bundle that `Resources/*` already depends on:
    `Types/PassMask.h`, `Types/DrawMetadata.h`,
    `Types/MaterialShadingConstants.h`,
    `Types/ProceduralGridMaterialConstants.h`,
    `Types/ConventionalShadowDrawRecord.h`,
    `ScenePrep/Handles.h`, `ScenePrep/GeometryRef.h`,
    `ScenePrep/MaterialRef.h`, `ScenePrep/RenderItemData.h`, and
    `PreparedSceneFrame.h/.cpp`
  - `01-05` now owns `Resources/*` and closes step `1.3`
  - the remaining ScenePrep, pass-base, composition, and orchestrator scopes
    shift down one slot so no single remaining plan exceeds the checker
    file-budget threshold
  - `01-10` now owns the step-`1.6` pass bases together with the remaining
    root-support files plus the stripped orchestrator and records the final
    post-orchestrator dependency-edge proof
  - the roadmap, validation contract, and resume note now all point execution
    back to the repaired `01-04` boundary
  - task-structure verification confirms all three repaired plans still have
    two executable tasks with `files`, `action`, `verify`, `done`,
    `<read_first>`, and `<acceptance_criteria>` intact
- Code / validation delta:
  - no implementation code changed
  - no build or tests were run during this planning repair
  - Phase 1 remains `in_progress`; execution must resume at `01-04` and still
    collect build/test evidence before any stronger completion claim
- Remaining blocker:
  - execute the repaired `01-04` plan and verify the prerequisite ABI bundle
    in code before moving to repaired `01-05` for the actual resource
    migration

### 2026-04-13 — Phase 1 plan 01-03 completed step-1.2 upload migration

- Changed files this session:
  - `src/Oxygen/Vortex/CMakeLists.txt`
  - `src/Oxygen/Vortex/RendererTag.h`
  - `src/Oxygen/Vortex/Upload/AtlasBuffer.cpp`
  - `src/Oxygen/Vortex/Upload/AtlasBuffer.h`
  - `src/Oxygen/Vortex/Upload/Errors.cpp`
  - `src/Oxygen/Vortex/Upload/Errors.h`
  - `src/Oxygen/Vortex/Upload/InlineTransfersCoordinator.cpp`
  - `src/Oxygen/Vortex/Upload/InlineTransfersCoordinator.h`
  - `src/Oxygen/Vortex/Upload/RingBufferStaging.cpp`
  - `src/Oxygen/Vortex/Upload/RingBufferStaging.h`
  - `src/Oxygen/Vortex/Upload/StagingProvider.cpp`
  - `src/Oxygen/Vortex/Upload/StagingProvider.h`
  - `src/Oxygen/Vortex/Upload/TransientStructuredBuffer.cpp`
  - `src/Oxygen/Vortex/Upload/TransientStructuredBuffer.h`
  - `src/Oxygen/Vortex/Upload/Types.h`
  - `src/Oxygen/Vortex/Upload/UploaderTag.h`
  - `src/Oxygen/Vortex/Upload/UploadCoordinator.cpp`
  - `src/Oxygen/Vortex/Upload/UploadCoordinator.h`
  - `src/Oxygen/Vortex/Upload/UploadHelpers.cpp`
  - `src/Oxygen/Vortex/Upload/UploadHelpers.h`
  - `src/Oxygen/Vortex/Upload/UploadPlanner.cpp`
  - `src/Oxygen/Vortex/Upload/UploadPlanner.h`
  - `src/Oxygen/Vortex/Upload/UploadPolicy.cpp`
  - `src/Oxygen/Vortex/Upload/UploadPolicy.h`
  - `src/Oxygen/Vortex/Upload/UploadTracker.cpp`
  - `src/Oxygen/Vortex/Upload/UploadTracker.h`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - `rg -n 'Oxygen/Renderer/|OXGN_RNDR_' src/Oxygen/Vortex/Upload/AtlasBuffer.cpp src/Oxygen/Vortex/Upload/AtlasBuffer.h src/Oxygen/Vortex/Upload/Errors.cpp src/Oxygen/Vortex/Upload/Errors.h src/Oxygen/Vortex/Upload/RingBufferStaging.cpp src/Oxygen/Vortex/Upload/RingBufferStaging.h src/Oxygen/Vortex/Upload/StagingProvider.cpp src/Oxygen/Vortex/Upload/StagingProvider.h src/Oxygen/Vortex/Upload/TransientStructuredBuffer.cpp src/Oxygen/Vortex/Upload/TransientStructuredBuffer.h src/Oxygen/Vortex/Upload/Types.h src/Oxygen/Vortex/Upload/UploaderTag.h`
  - `rg -n 'Oxygen/Renderer/|OXGN_RNDR_' src/Oxygen/Vortex/Upload/InlineTransfersCoordinator.cpp src/Oxygen/Vortex/Upload/InlineTransfersCoordinator.h src/Oxygen/Vortex/Upload/UploadCoordinator.cpp src/Oxygen/Vortex/Upload/UploadCoordinator.h src/Oxygen/Vortex/Upload/UploadHelpers.cpp src/Oxygen/Vortex/Upload/UploadHelpers.h src/Oxygen/Vortex/Upload/UploadPlanner.cpp src/Oxygen/Vortex/Upload/UploadPlanner.h src/Oxygen/Vortex/Upload/UploadPolicy.cpp src/Oxygen/Vortex/Upload/UploadPolicy.h src/Oxygen/Vortex/Upload/UploadTracker.cpp src/Oxygen/Vortex/Upload/UploadTracker.h`
  - `cmake --build --preset windows-debug --target oxygen-vortex --parallel 4`
  - `cmake --preset windows-default`
  - `D:/dev/ninja/ninja.exe -C out/build-ninja -f build-Debug.ninja -t query bin/Debug/Oxygen.Vortex-d.dll`
- Result:
  - the complete step-`1.2` upload slice now lives under
    `src/Oxygen/Vortex/Upload/`, including the foundation, staging,
    coordination, planner, policy, helper, and tracker files
  - `src/Oxygen/Vortex/RendererTag.h` was added so the migrated upload headers
    can stop including `Oxygen/Renderer/RendererTag.h` while keeping the
    migration mechanical
  - `oxygen-vortex` builds successfully in Debug after the full upload
    migration lands
  - the generated Debug Ninja target query for
    `bin/Debug/Oxygen.Vortex-d.dll` shows no `oxygen-renderer` /
    `Oxygen.Renderer` dependency edge
- Code / validation delta:
  - step `1.2` is now **complete**
  - no Vortex runtime or link-test validation was run in this plan
- Remaining blocker:
  - execute `01-04` to migrate the step-`1.3` resources slice

### 2026-04-13 — Phase 1 plan 01-02 completed the remaining step-1.1 type migration

- Changed files this session:
  - `src/Oxygen/Vortex/CMakeLists.txt`
  - `src/Oxygen/Vortex/Types/EnvironmentViewData.h`
  - `src/Oxygen/Vortex/Types/ViewColorData.h`
  - `src/Oxygen/Vortex/Types/ViewConstants.cpp`
  - `src/Oxygen/Vortex/Types/ViewConstants.h`
  - `src/Oxygen/Vortex/Types/ViewFrameBindings.h`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - `rg -n 'Oxygen/Renderer/|OXGN_RNDR_' src/Oxygen/Vortex/Types/EnvironmentViewData.h src/Oxygen/Vortex/Types/LightCullingConfig.h src/Oxygen/Vortex/Types/SyntheticSunData.h src/Oxygen/Vortex/Types/ViewColorData.h src/Oxygen/Vortex/Types/ViewConstants.cpp src/Oxygen/Vortex/Types/ViewConstants.h src/Oxygen/Vortex/Types/ViewFrameBindings.h`
  - `cmake --build --preset windows-debug --target oxygen-vortex --parallel 4`
  - `cmake --preset windows-default`
  - `D:/dev/ninja/ninja.exe -C out/build-ninja -f build-Debug.ninja -t query oxygen-vortex`
- Result:
  - the remaining step-`1.1` files now live under `src/Oxygen/Vortex/Types/`:
    `EnvironmentViewData.h`, `ViewColorData.h`, `ViewConstants.h`,
    `ViewConstants.cpp`, and `ViewFrameBindings.h`
  - `ViewConstants.cpp` is now part of the `oxygen-vortex` private source list
  - `oxygen-vortex` builds successfully in Debug after the remaining type
    migration
  - the generated Debug Ninja target query for `oxygen-vortex` shows no
    `oxygen-renderer` / `Oxygen.Renderer` dependency edge
- Code / validation delta:
  - step `1.1` is now **complete**
  - no Vortex/legacy link-test or runtime validation was run in this plan
- Remaining blocker:
  - begin step `1.2` with `01-03` to migrate the upload foundation and staging
    slice

### 2026-04-13 — Phase 1 plan 01-01 started with the step-1.1 frame-binding slice

- Changed files this session:
  - `src/Oxygen/Vortex/CMakeLists.txt`
  - `src/Oxygen/Vortex/Types/CompositingTask.h`
  - `src/Oxygen/Vortex/Types/DebugFrameBindings.h`
  - `src/Oxygen/Vortex/Types/DrawFrameBindings.h`
  - `src/Oxygen/Vortex/Types/EnvironmentFrameBindings.h`
  - `src/Oxygen/Vortex/Types/LightCullingConfig.h`
  - `src/Oxygen/Vortex/Types/LightingFrameBindings.h`
  - `src/Oxygen/Vortex/Types/ShadowFrameBindings.h`
  - `src/Oxygen/Vortex/Types/SyntheticSunData.h`
  - `src/Oxygen/Vortex/Types/VsmFrameBindings.h`
- Commands used for verification:
  - `rg -n 'Oxygen/Renderer/|OXGN_RNDR_' src/Oxygen/Vortex/Types/CompositingTask.h src/Oxygen/Vortex/Types/DebugFrameBindings.h src/Oxygen/Vortex/Types/DrawFrameBindings.h src/Oxygen/Vortex/Types/EnvironmentFrameBindings.h src/Oxygen/Vortex/Types/LightingFrameBindings.h src/Oxygen/Vortex/Types/ShadowFrameBindings.h src/Oxygen/Vortex/Types/VsmFrameBindings.h`
  - `cmake --build --preset windows-debug --target oxygen-vortex --parallel 4`
  - `cmake --preset windows-default`
  - `rg -n "oxygen-renderer|Oxygen\\.Renderer" out/build-ninja/src/Oxygen/Vortex/CMakeFiles/Export out/build-ninja/src/Oxygen/Vortex/CMakeFiles/oxygen-vortex.dir/Debug/CXXDependInfo.json`
- Result:
  - the first frame-binding half of step `1.1` now lives under
    `src/Oxygen/Vortex/Types/`
  - `LightingFrameBindings.h` now uses Vortex-local
    `LightCullingConfig.h` and `SyntheticSunData.h` so the migrated slice
    carries no `Oxygen/Renderer/` include seam
  - `oxygen-vortex` builds successfully in Debug after the type migration
  - the generated Vortex export/depend info shows no `oxygen-renderer` /
    `Oxygen.Renderer` reference
- Code / validation delta:
  - step `1.1` is **still in progress**; the remaining type headers are
    deferred to `01-02`
  - no broader link-test or runtime validation was run in this plan
- Remaining blocker:
  - execute `01-02` to land the remaining step-1.1 type headers before the
    rest of Phase 1 continues

### 2026-04-13 — Phase 1 plan set repaired

- Changed files this session:
  - `.planning/workstreams/vortex/phases/01-substrate-migration/*-PLAN.md`
  - `.planning/workstreams/vortex/ROADMAP.md`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-VALIDATION.md`
  - `.planning/workstreams/vortex/STATE.md`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - structural inspection of all Phase 1 plan files
  - per-plan checks for `wave`, `depends_on`, `requirements`, task count,
    `<read_first>`, and `<acceptance_criteria>`
- Result:
  - the full 11-plan Phase 1 set now matches roadmap steps `1.1` through `1.9`
  - `01-04` through `01-06` now cover resources, ScenePrep, and selected
    internals instead of continuing upload work
  - `01-08` now lands only the public half of step `1.7`
  - `01-09` closes the private half of step `1.7`
  - `01-10` now owns step `1.6`, the remaining root-support files, and the
    stripped orchestrator for step `1.8`
- Code / validation delta:
  - no implementation code changed
  - no build or tests were run during plan repair
  - Phase 1 is planned and ready to execute, not complete
- Remaining blocker:
  - execute the repaired Phase 1 plan set and collect build/test evidence

### 2026-04-13 — Phase 1 execute-phase blocker recorded

- Changed files this session:
  - `design/vortex/IMPLEMENTATION-STATUS.md`
  - `.planning/workstreams/vortex/ROADMAP.md`
  - `.planning/workstreams/vortex/STATE.md`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-VALIDATION.md`
- Commands used for verification:
  - `node "$HOME/.codex/get-shit-done/bin/gsd-tools.cjs" init execute-phase 1 --ws vortex`
  - `node "$HOME/.codex/get-shit-done/bin/gsd-tools.cjs" phase-plan-index 1 --ws vortex`
  - `Get-Content .planning/workstreams/vortex/phases/01-substrate-migration/01-04-PLAN.md`
  - `Get-Content .planning/workstreams/vortex/phases/01-substrate-migration/01-05-PLAN.md`
  - `Get-Content .planning/workstreams/vortex/phases/01-substrate-migration/01-06-PLAN.md`
  - `Get-Content .planning/workstreams/vortex/phases/01-substrate-migration/01-08-PLAN.md`
  - `Get-Content .planning/workstreams/vortex/ROADMAP.md`
  - `Get-Content .planning/workstreams/vortex/phases/01-substrate-migration/01-VALIDATION.md`
- Result:
  - execute-phase preflight found scope drift between the intended Phase 1 scope and the generated `.planning` micro-plan set
  - `ROADMAP.md` and `01-VALIDATION.md` still require resources, ScenePrep, and internal-utility migration work, but the current `01-04` through `01-06` plan files instead continue the upload migration
  - `01-08-PLAN.md` attempts to record steps `1.4` through `1.7` as complete even though the missing resources / ScenePrep / internal-utility slices are not planned anywhere in the current phase directory
- Code / validation delta:
  - no implementation code changed
  - no build or tests were run because execution was blocked before a trustworthy task boundary existed
  - Phase 1 remains incomplete and must not be reported as having started beyond preflight discovery
- Ledger impact:
  - Phase 1 status is now `blocked`
  - execution must not resume from the current plan set until the missing step coverage is repaired
- Remaining blocker:
  - regenerate or repair the Phase 1 `.planning/workstreams/vortex/phases/01-substrate-migration/*-PLAN.md` files so they explicitly cover steps `1.3`, `1.4`, and `1.5` before implementation resumes

### 2026-04-13 — PLAN.md synchronization

- Changed files this session:
  - `design/vortex/PLAN.md`
- Commands used for verification:
  - repo inspection via `rg`
  - targeted section rereads
  - `git diff -- design/vortex/PLAN.md`
- Result:
  - PLAN.md now explicitly covers Phase 2 activation/validation for `Stencil`
    and `CustomDepth`
  - Phase 5 feature-gated runtime validation now includes `no-shadowing` and
    `no-volumetrics`
  - Phase 7 now maps `MegaLights-class lighting extensions`,
    `Heterogeneous volumes`, and `Hair strands` to explicit future activation
    slots
- Code / validation delta:
  - no implementation code changed
  - no build or tests were run
  - no phase status changed
- Ledger impact:
  - Phase 2 work-item list below must include 2.12 from the updated plan
  - Phase 5 runtime-variant scope below must reflect the expanded PRD §6.6 set
- Remaining blocker:
  - Phase 0 is still incomplete; no implementation progress is claimed from this
    documentation-only session

---

## Phase 0 — Scaffold and Build Integration

**Status:** `done`

### What Exists

| Item | Path | Verified |
| ---- | ---- | -------- |
| Directory tree | `src/Oxygen/Vortex/` (subdirs with `.gitkeep`) | Yes — repo inspection |
| Parent CMake wiring | `src/Oxygen/CMakeLists.txt` | Yes — `add_subdirectory("Vortex")` now present once |
| CMakeLists.txt | `src/Oxygen/Vortex/CMakeLists.txt` | Yes — declares `Oxygen.Vortex`, links deps, C++23 |
| Module anchor source | `src/Oxygen/Vortex/ModuleAnchor.cpp` | Yes — minimal translation unit added so the scaffolded library can generate |
| Export header | `src/Oxygen/Vortex/api_export.h` | Yes — exists |
| Test CMake | `src/Oxygen/Vortex/Test/CMakeLists.txt` | Yes — `Oxygen.Vortex.LinkTest` is enabled and links against `oxygen::vortex` |
| Link smoke source | `src/Oxygen/Vortex/Test/Link_test.cpp` | Yes — minimal consumer includes `Oxygen/Vortex/api_export.h` and exits 0 |

### What Is Missing

None for Phase 0 exit. The standard preset build path, the generated target, and
the alias consumer have all been proven. Remaining work begins in Phase 1.

### Validation Log

| Date | Command | Result |
| ---- | ------- | ------ |
| (initial) | `cmake --build --preset windows-debug --target Oxygen.Vortex --parallel 4` | FAIL — "unknown target 'Oxygen.Vortex'" |
| 2026-04-13 | `rg -n 'add_subdirectory\\("Vortex"\\)' src/Oxygen/CMakeLists.txt` | PASS — parent CMake now wires `Vortex` once |
| 2026-04-13 | `cmake --preset windows-default` | PASS — configure/generate now succeeds with `oxygen-vortex` in the generated project graph |
| 2026-04-13 | `D:/dev/ninja/ninja.exe -C out/build-ninja -f build-Debug.ninja -t targets all \| Select-String 'vortex'` | PASS — generated Debug Ninja graph contains `oxygen-vortex`, `Oxygen.Vortex-d.dll`, and Vortex source-tree targets |
| 2026-04-13 | `cmake --build --preset windows-debug --target help` | FAIL — regeneration blocked in `_deps/ccache.cmake-subbuild` by `ninja: error: failed recompaction: Permission denied` |
| 2026-04-13 | `rg -n 'Oxygen\\.Vortex\\.LinkTest\|oxygen::vortex\|Link_test\\.cpp' src/Oxygen/Vortex/Test/CMakeLists.txt src/Oxygen/Vortex/Test/Link_test.cpp` | PASS — minimal alias smoke target and source are present |
| 2026-04-13 | `cmake --preset windows-default` | PASS — configure/generate succeeds after enabling the link smoke target |
| 2026-04-13 | `D:/dev/ninja/ninja.exe -C out/build-ninja -f build-Debug.ninja oxygen-vortex Oxygen.Vortex.LinkTest` | PASS — both the Vortex library and the alias consumer build in Debug |
| 2026-04-13 | `ctest --test-dir out/build-ninja -C Debug -R '^Oxygen\\.Vortex\\.LinkTest$' --output-on-failure` | PASS — `Oxygen.Vortex.LinkTest` passes |
| 2026-04-13 | `Remove-Item out/build-ninja/_deps/ccache.cmake-subbuild/.ninja_log{,.restat}; cmake --build --preset windows-debug --target oxygen-vortex Oxygen.Vortex.LinkTest` | PASS — standard preset build path works after resetting the generated ccache subbuild logs |

### Resume Point

Phase 0 is complete. Resume with Phase 1 substrate migration once the Phase 1
design and execution work starts.

---

## Phase 1 — Substrate Migration

**Status:** `done`

### What Exists

- A repaired 11-plan Phase 1 execution set under
  `.planning/workstreams/vortex/phases/01-substrate-migration/`.
- Plan coverage now matches the source-of-truth Vortex design package:
  - `01-04` lands only the prerequisite ABI bundle required by resources
  - `01-05` covers `Resources/*` and closes step `1.3`
  - `01-06` covers only the remaining ScenePrep-only data/config files
  - `01-07` covers ScenePrep execution plus the selected substrate-only
    internal utilities
  - `01-08` covers only the public half of step `1.7`
  - `01-09` closes the private half of step `1.7`
  - `01-10` covers the step-`1.6` pass bases, the remaining root-support
    files, the stripped orchestrator, and the final post-orchestrator
    dependency-edge proof
- Every Phase 1 plan now has 2 tasks plus the required `<read_first>` and
  `<acceptance_criteria>` blocks.
- Step `1.1` is now fully migrated under `src/Oxygen/Vortex/Types/`,
  including the `01-01` frame-binding slice plus
  `EnvironmentViewData.h`, `ViewColorData.h`, `ViewConstants.h`,
  `ViewConstants.cpp`, and `ViewFrameBindings.h`.
- Step `1.2` is now fully migrated under `src/Oxygen/Vortex/Upload/`,
  including upload staging, atlas buffering, inline transfer retirement,
  upload planning, coordinator orchestration, policy, helpers, and tracker
  support.
- Repaired `01-04` is now complete: the prerequisite ABI bundle needed by
  `Resources/*` lives under Vortex ownership and `oxygen-vortex` builds with
  that bundle alone while the resource implementation remains deferred.
- Repaired `01-05` is now complete: the full `Resources/*` slice builds under
  `src/Oxygen/Vortex/Resources/`, and the linked Vortex DLL still carries no
  `Oxygen.Renderer` dependency edge.
- Repaired `01-06` is now complete: the remaining ScenePrep-only data/config
  contracts build under `src/Oxygen/Vortex/ScenePrep/`, while
  `ScenePrep/Extractors.h`, `ScenePrep/Finalizers.h`, and
  `ScenePrepPipeline.cpp/.h` remain explicitly deferred to `01-07`.
- Repaired `01-07` is now complete: the deferred ScenePrep execution files and
  selected substrate-only internal utilities build under Vortex ownership, and
  the linked Vortex DLL still carries no `oxygen-renderer` / `Oxygen.Renderer`
  dependency edge.
- Repaired `01-08` is now complete for its owned public slice: the step `1.7`
  root vocabulary (`CompositionView.h`, `RendererCapability.h`,
  `RenderMode.h`) and `SceneRenderer/DepthPrePassPolicy.h` now live under
  Vortex ownership.
- Repaired `01-09` is now complete: the private composition infrastructure
  lives under `src/Oxygen/Vortex/Internal/` and
  `src/Oxygen/Vortex/SceneRenderer/Internal/`, and `oxygen-vortex` builds with
  the full step-`1.7` rehome in place.
- Repaired `01-10` is now complete for its owned code slice: the Vortex root
  support files, pass bases, and stripped renderer shell all build under
  Vortex ownership, and the final post-orchestrator Debug Ninja target query
  still shows no `oxygen-renderer` / `Oxygen.Renderer` dependency edge.
- Repaired `01-11` is now complete: `Oxygen.Vortex.LinkTest` constructs
  `oxygen::vortex::Renderer` with an empty capability set, drives the stripped
  frame-hook sequence successfully, and the targeted legacy substrate
  regression suite passes in the Debug build tree.

### Steps (from PLAN.md §3)

| Step | Task | Status | Evidence |
| ---- | ---- | ------ | -------- |
| 1.1 | Cross-cutting types (14 headers) | `done` | `01-01` migrated the frame-binding slice; `01-02` landed the remaining type headers, built `oxygen-vortex`, and verified no `Oxygen.Renderer` dependency edge |
| 1.2 | Upload subsystem (14 files) | `done` | `01-03` migrated the full `Upload/` slice, built `oxygen-vortex`, and proved the linked Vortex DLL has no `oxygen-renderer` / `Oxygen.Renderer` dependency edge |
| 1.3 | Resources subsystem (7 files) | `done` | `01-05` landed the full `Resources/*` slice, built `oxygen-vortex`, and proved the linked Vortex DLL still has no `Oxygen.Renderer` dependency edge |
| 1.4 | ScenePrep subsystem (15 files) | `done` | `01-07` landed `ScenePrep/Extractors.h`, `ScenePrep/Finalizers.h`, and `ScenePrepPipeline.cpp/.h`, built `oxygen-vortex`, and proved the linked Vortex DLL still has no `oxygen-renderer` / `Oxygen.Renderer` dependency edge |
| 1.5 | Internal utilities (7 files) | `done` | `01-07` landed the selected substrate-only `Internal/*` slice, built `oxygen-vortex`, and proved the linked Vortex DLL still has no `oxygen-renderer` / `Oxygen.Renderer` dependency edge |
| 1.6 | Pass base classes (3 files) | `done` | `01-10` landed `Passes/RenderPass`, `GraphicsRenderPass`, and `ComputeRenderPass` together with the Vortex-owned root contracts, then rebuilt `oxygen-vortex` successfully |
| 1.7 | View assembly + composition | `done` | `01-08` landed the public headers and `01-09` landed the private `Internal/` plus `SceneRenderer/Internal/` files, then rebuilt `oxygen-vortex` successfully |
| 1.8 | Renderer orchestrator | `done` | `01-10` landed the stripped Vortex renderer shell, rebuilt `oxygen-vortex`, and recorded the final post-orchestrator Debug Ninja query proving `bin/Debug/Oxygen.Vortex-d.dll` still has no `Oxygen.Renderer` dependency edge |
| 1.9 | Smoke test | `done` | `01-11` upgraded `Oxygen.Vortex.LinkTest` into a real renderer smoke path, ran it through `ctest`, then ran the targeted legacy substrate regression suite successfully in the same Debug build tree |

### Resume Point

Phase 1 is complete. Resume with Phase 2 implementation when the
`SceneTextures` and `SceneRenderer` shell work begins.

---

## Phase 2 — SceneTextures + SceneRenderer Shell

**Status:** `not_started`

### Design Prerequisites

| Deliverable | Status |
| ----------- | ------ |
| D.1 SceneTextures four-part contract | `not_started` |
| D.2 SceneRenderBuilder bootstrap | `not_started` |
| D.3 SceneRenderer shell dispatch | `not_started` |

### Work Items (from PLAN.md §4)

| ID | Task | Status | Evidence |
| -- | ---- | ------ | -------- |
| 2.1–2.4 | SceneTextures four-part contract | `not_started` | — |
| 2.5 | ShadingMode enum | `not_started` | — |
| 2.6 | SceneRenderBuilder | `not_started` | — |
| 2.7 | SceneRenderer shell (23-stage skeleton) | `not_started` | — |
| 2.8 | Wire SceneRenderer into Renderer | `not_started` | — |
| 2.9 | PostRenderCleanup | `not_started` | — |
| 2.10 | ResolveSceneColor | `not_started` | — |
| 2.11 | Stages directory structure | `not_started` | — |
| 2.12 | Validate first active `SceneTextures` subset (`SceneColor`, `SceneDepth`, `PartialDepth`, `GBufferA-D`, `Stencil`, `Velocity`, `CustomDepth`) | `not_started` | — |

### Resume Point

Phase 1 + design deliverables D.1–D.3 must be completed first.

---

## Phase 3 — Deferred Core

**Status:** `not_started`

### Design Prerequisites

| Deliverable | Status |
| ----------- | ------ |
| D.4 Depth prepass LLD | `not_started` |
| D.5 Base pass LLD | `not_started` |
| D.6 Deferred lighting LLD | `not_started` |
| D.7 Shader contracts LLD | `not_started` |
| D.8 InitViews LLD | `not_started` |

### Resume Point

Phase 2 + design deliverables D.4–D.8 must be completed first.

---

## Phase 4 — Migration-Critical Services + First Migration

**Status:** `not_started`

### Per-Service Status

| Service | Deliverable | Design Status | Impl Status |
| ------- | ----------- | ------------- | ----------- |
| 4A LightingService | D.9 | `not_started` | `not_started` |
| 4B PostProcessService | D.10 | `not_started` | `not_started` |
| 4C ShadowService | D.11 | `not_started` | `not_started` |
| 4D EnvironmentLightingService | D.12 | `not_started` | `not_started` |
| 4E Examples/Async migration | D.13 | `not_started` | `not_started` |
| 4F Composition/presentation validation | — | — | `not_started` |

### Resume Point

Phase 3 must be completed first. 4A–4D are parallelizable. 4E requires all
four services. 4F follows 4E.

---

## Phase 5 — Remaining Services + Runtime Scenarios

**Status:** `not_started`

### Per-Item Status

| Item | Deliverable | Design Status | Impl Status |
| ---- | ----------- | ------------- | ----------- |
| 5A DiagnosticsService | D.14 | `not_started` | `not_started` |
| 5B TranslucencyModule | D.15 | `not_started` | `not_started` |
| 5C OcclusionModule | D.16 | `not_started` | `not_started` |
| 5D Multi-view / per-view mode | D.17 | `not_started` | `not_started` |
| 5E Offscreen / facade validation | D.18 | `not_started` | `not_started` |
| 5F Feature-gated runtime variants (`depth-only`, `shadow-only`, `no-environment`, `no-shadowing`, `no-volumetrics`, `diagnostics-only`) | — | — | `not_started` |

### Resume Point

Phase 4 must be completed first. 5A–5E are parallelizable. 5F requires all
services.

---

## Phase 6 — Legacy Deprecation

**Status:** `not_started`

### Resume Point

Phase 5 must be completed first.

---

## Architectural Resume Notes

When implementation resumes, keep these baseline facts explicit:

- The active Vortex source-of-truth package is:
  `PRD.md`, `ARCHITECTURE.md`, `DESIGN.md`, `PROJECT-LAYOUT.md`, `PLAN.md`,
  and this file.
- DESIGN.md is an **early draft** — it covers illustrative shapes (SceneRenderer
  class structure, SceneTextures allocation, GBuffer format, frame dispatch,
  subsystem contracts, base pass, deferred lighting, substrate adaptation,
  shader organization) but is NOT complete LLD. Missing areas include:
  SceneTextureSetupMode, SceneTextureBindings, SceneTextureExtracts,
  InitViewsModule, SceneRenderBuilder, velocity distribution, extraction/handoff,
  per-subsystem LLD.
- Each phase in PLAN.md identifies specific design deliverables that must be
  completed before implementation begins.
- The current legacy renderer is still the live implementation and the current
  source of reusable substrate.
- Referenced historical documents `vortex-initial-design.md` and
  `parity-analysis.md` do not exist in the repo; the current five-document
  package supersedes them.
- The active production path is `Oxygen.Renderer` + `ForwardPipeline`.
- Use frame 10 as the RenderDoc baseline capture point.
