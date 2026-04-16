# Vortex Renderer Implementation Status

Status: `blocked — Phase 03 now has a working retained runtime branch with green live VortexBasic proof, but the closure/docs remediation lane is still open and Phase 04 stays locked until the remaining Phase 03 blockers are closed truthfully`

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
| 2 | SceneTextures + SceneRenderer Shell | `done` | — |
| 3 | Deferred Core | `blocked` | final multi-review/remediation gate plus remaining Phase 3 doc/code contract cleanup |
| 4 | Migration-Critical Services + First Migration | `not_started` | Lighting/PostProcess/Shadow/Environment activation + Async/DemoShell Vortex migration |
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

### 2026-04-16 — Phase 3 deferred-core closeout blocked

- Commands used for verification:
  - `powershell -NoProfile -File tools/vortex/Run-DeferredCoreFrame10Capture.ps1 -Output out/build-ninja/analysis/vortex/deferred-core/frame10`
  - `powershell -NoProfile -File tools/vortex/Analyze-DeferredCoreCapture.ps1 -CapturePath out/build-ninja/analysis/vortex/deferred-core/frame10/deferred-core-frame10.inputs.json -ReportPath out/build-ninja/analysis/vortex/deferred-core/frame10/deferred-core-frame10.report.txt`
  - `powershell -NoProfile -File tools/vortex/Assert-DeferredCoreCaptureReport.ps1 -ReportPath out/build-ninja/analysis/vortex/deferred-core/frame10/deferred-core-frame10.report.txt`
- Result:
  - The Phase 03 closeout gate did not pass.
  - Report: F:\projects\DroidNet\projects\Oxygen.Engine\out\build-ninja\analysis\vortex\deferred-core\frame10\deferred-core-frame10.report.txt
- Missing delta: stage_2_order=fail, stage_12_order=fail, stencil_local_lights=fail
- RenderDoc runtime validation remains deferred to Phase 04 and is not part of this failure.

### 2026-04-16 — Historical 03-15 frame-10 closeout pack failed and is now superseded

- Commands used for verification:
  - `powershell -NoProfile -File tools/vortex/Run-DeferredCoreFrame10Capture.ps1 -Output out/build-ninja/analysis/vortex/deferred-core/frame10`
  - `powershell -NoProfile -File tools/vortex/Analyze-DeferredCoreCapture.ps1 -CapturePath out/build-ninja/analysis/vortex/deferred-core/frame10/deferred-core-frame10.inputs.json -ReportPath out/build-ninja/analysis/vortex/deferred-core/frame10/deferred-core-frame10.report.txt`
  - `powershell -NoProfile -File tools/vortex/Assert-DeferredCoreCaptureReport.ps1 -ReportPath out/build-ninja/analysis/vortex/deferred-core/frame10/deferred-core-frame10.report.txt`
- Result:
  - The historical 03-15 frame-10 closeout gate did not pass.
  - Report: F:\projects\DroidNet\projects\Oxygen.Engine\out\build-ninja\analysis\vortex\deferred-core\frame10\deferred-core-frame10.report.txt
- Missing delta: stage_2_order=fail, stage_3_order=fail, stage_9_order=fail, stage_12_order=fail, gbuffer_contents=fail, scene_color_lit=fail, stencil_local_lights=fail
- Status of this evidence:
  - This frame-10 closeout pack is no longer the authoritative Phase 03 closure surface.
  - The current authoritative runtime proof path is the live `VortexBasic` validation flow (`tools/vortex/Run-VortexBasicRuntimeValidation.ps1`).
  - Do not use this historical frame-10 result as the current Phase 03 closure verdict.

### 2026-04-16 — Phase 03 cleanup lane docs synced to the retained runtime branch

- Changed files this session:
  - `design/vortex/lld/scene-renderer-shell.md`
  - `design/vortex/lld/base-pass.md`
  - `design/vortex/lld/scene-textures.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-VALIDATION.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-VERIFICATION.md`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - repo-grounded doc/code comparison against the retained `03-21` runtime branch
  - latest cleanup-lane proof artifact review:
    - `out/build-ninja/analysis/vortex/vortexbasic/runtime/task21-04-logcleanup-proof.validation.txt`
- Result:
  - the LLDs now describe the retained Stage 10 republish boundary, the early-Z color-only clear rule, and the explicit Stage 21 resolved-artifact handoff used by composition
  - the Phase 03 validation docs now describe the current VortexBasic wrapper truthfully: structural + product analyzers together, with point/spot product checks treated as part of the durable runtime gate
  - the implementation ledger now matches the cleanup-lane reality instead of the earlier blocked-state assumptions alone
- Code / validation delta:
  - no new implementation code changed in this documentation-sync session
  - the latest cleanup-lane runtime proof remains green on the current build, but Phase 03 is still open pending the remaining cleanup-lane task(s) and the final multi-review gate
- Remaining blocker:
  - finish the remaining `03-21` work, then run the required final multi-subagent Phase 03 review/remediation loop before any closure claim

### 2026-04-16 — Phase 03 cleanup lane added after the live-runtime remediation checkpoint

- Changed files this session:
  - `.planning/workstreams/vortex/STATE.md`
  - `.planning/workstreams/vortex/ROADMAP.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-21-PLAN.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-VALIDATION.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-VERIFICATION.md`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - `git commit -m "chore: preserve the current Phase 03 live-runtime remediation snapshot" ...`
  - repo-grounded staged-tree review against the current `03-18` through `03-20` remediation branch
- Result:
  - The full live-runtime remediation branch is preserved in temporary checkpoint commit `5a3926cd3`.
  - Phase 03 now carries an explicit `03-21` cleanup lane so the retained fixes can be cleaned without sacrificing the working `VortexBasic` scene or the RenderDoc proof surface.
  - The RenderDoc analyzers are preserved by design; the cleanup lane treats them as durable proof assets that need consolidation and hardening rather than deletion.
- Code / validation delta:
  - no new runtime or build verification was claimed from this planning-only update
  - `03-21` now defines the required per-remediation proof gate: scene still renders, VortexBasic RenderDoc proof stays green, and code review approves before commit
- Remaining blocker:
  - execute `03-18` through `03-21` truthfully before Phase 03 can be treated as clean or closed

### 2026-04-15 — Phase 03 returned to blocked gap-closure status after live runtime validation

- Changed files this session:
  - `.planning/workstreams/vortex/STATE.md`
  - `.planning/workstreams/vortex/ROADMAP.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-18-PLAN.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-19-PLAN.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-20-PLAN.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-VERIFICATION.md`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - debugger-assisted `VortexBasic` runtime validation with D3D12 debug-layer error/call-stack capture
  - review intake from the `Lagrange` audit lane over resource-state / registration / lifetime management
- Result:
  - Phase 03 can no longer be claimed complete.
  - Live `VortexBasic` validation exposed barrier mismatches on persistent scene textures, proving that deferred-core stages are still guessing incoming resource states instead of consuming an authoritative graphics-layer state contract.
  - The Phase 03 roadmap now carries a three-plan gap-closure track:
    - `03-18` tracker-centered persistent state continuity
    - `03-19` deterministic registration / framebuffer-view ownership / GPU-safe retirement
    - `03-20` live runtime D3D12 + capture-backed proof closure
  - Runtime proof is no longer deferred to Phase 04 because `VortexBasic` now exists as the live Phase 03 validation surface.
- Code / validation delta:
  - no implementation code was claimed complete from this planning correction
  - non-runtime deferred-core tests remain green, but runtime deferred-core proof is still blocked
- Remaining blocker:
  - execute `03-18` through `03-20` and do not reopen Phase 04 until the live runtime proof is clean

### 2026-04-15 — Phase 3 remediation closed with real Stage 3/9/12 rendering and proof rerun

- Changed files this session:
  - `src/Oxygen/Graphics/Common/Internal/FramebufferImpl.cpp`
  - `src/Oxygen/Graphics/Common/Internal/ToStringConverters.cpp`
  - `src/Oxygen/Graphics/Common/PipelineState.cpp`
  - `src/Oxygen/Graphics/Common/PipelineState.h`
  - `src/Oxygen/Graphics/Direct3D12/Detail/Converters.cpp`
  - `src/Oxygen/Graphics/Direct3D12/Shaders/EngineShaderCatalog.h`
  - `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Lighting/DeferredLightingCommon.hlsli`
  - `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Lighting/DeferredLightPoint.hlsl`
  - `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Lighting/DeferredLightSpot.hlsl`
  - `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.h`
  - `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp`
  - `src/Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassModule.cpp`
  - `src/Oxygen/Vortex/SceneRenderer/Stages/DepthPrepass/DepthPrepassModule.h`
  - `src/Oxygen/Vortex/SceneRenderer/Stages/DepthPrepass/DepthPrepassModule.cpp`
  - `src/Oxygen/Vortex/Test/SceneRendererDeferredCore_test.cpp`
  - `tools/vortex/AnalyzeDeferredCoreCapture.py`
  - `tools/vortex/Run-DeferredCoreFrame10Capture.ps1`
  - `.planning/workstreams/vortex/STATE.md`
  - `.planning/workstreams/vortex/ROADMAP.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-VERIFICATION.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-17-SUMMARY.md`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - `cmake --build --preset windows-debug --target oxygen-vortex oxygen-graphics-direct3d12 Oxygen.Vortex.SceneRendererDeferredCore.Tests Oxygen.Vortex.SceneRendererPublication.Tests --parallel 4`
  - `ctest --test-dir out/build-ninja -C Debug -R "^Oxygen\\.Vortex\\.(SceneRendererDeferredCore|SceneRendererPublication)\\.Tests$" --output-on-failure`
  - `powershell -NoProfile -File tools/vortex/Verify-DeferredCoreCloseout.ps1 -Output out/build-ninja/analysis/vortex/deferred-core/frame10`
- Result:
  - Stage 3 now records a real depth/partial-velocity pass and copies SceneDepth into PartialDepth.
  - Stage 9 now records a real base pass and keeps depth testing aligned with the active reverse-Z convention.
  - Stage 12 now records actual fullscreen and stencil-bounded deferred-light draws, publishes per-light constants, and uses dedicated stencil-mark pixel entrypoints plus procedural local-light volume generation.
  - The deferred-core/publication suites pass, and the repo-owned closeout runner produces a passing report again with the revised render-path checks.
- Code / validation delta:
  - `DEFR-01` is now satisfied in code and proof.
  - `DEFR-02` is now satisfied in code and proof.
  - Runtime RenderDoc capture remains intentionally deferred to Phase 04, but it is no longer a Phase 03 blocker.
- Remaining blocker:
  - None for Phase 03 exit. Resume with Phase 04 planning and the first migrated runtime surface.

### 2026-04-15 — Phase 3 deferred-core proof pack

- Changed files this session:
  - `tools/vortex/Run-DeferredCoreFrame10Capture.ps1`
  - `tools/vortex/AnalyzeDeferredCoreCapture.py`
  - `tools/vortex/Analyze-DeferredCoreCapture.ps1`
  - `tools/vortex/Assert-DeferredCoreCaptureReport.ps1`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - `powershell -NoProfile -File tools/vortex/Run-DeferredCoreFrame10Capture.ps1 -Output out/build-ninja/analysis/vortex/deferred-core/frame10`
  - `powershell -NoProfile -File tools/vortex/Analyze-DeferredCoreCapture.ps1 -CapturePath out/build-ninja/analysis/vortex/deferred-core/frame10/deferred-core-frame10.inputs.json -ReportPath out/build-ninja/analysis/vortex/deferred-core/frame10/deferred-core-frame10.report.txt`
  - `powershell -NoProfile -File tools/vortex/Assert-DeferredCoreCaptureReport.ps1 -ReportPath out/build-ninja/analysis/vortex/deferred-core/frame10/deferred-core-frame10.report.txt`
- Result:
  - Phase 3 closeout is proven by source/test/log-backed analysis of the current deferred-core tree.
  - RenderDoc runtime validation deferred to Phase 04 when Async and DemoShell migrate to Vortex.
  - Analyzer report: F:\projects\DroidNet\projects\Oxygen.Engine\out\build-ninja\analysis\vortex\deferred-core\frame10\deferred-core-frame10.report.txt
- Code / validation delta:
  - The proof pack now closes Stage 2/3/9/12 ordering, GBuffer publication, SceneColor accumulation, and stencil-bounded local-light behavior without claiming a runtime capture that does not exist yet.
- Remaining blocker:
  - Phase 04 must migrate Async and DemoShell to Vortex before the first truthful frame-10 RenderDoc runtime capture is claimed.

### 2026-04-15 — Phase 3 deferred-core closeout blocked

- Commands used for verification:
  - `powershell -NoProfile -File tools/vortex/Run-DeferredCoreFrame10Capture.ps1 -Output out/build-ninja/analysis/vortex/deferred-core/frame10`
  - `powershell -NoProfile -File tools/vortex/Analyze-DeferredCoreCapture.ps1 -CapturePath out/build-ninja/analysis/vortex/deferred-core/frame10/deferred-core-frame10.inputs.json -ReportPath out/build-ninja/analysis/vortex/deferred-core/frame10/deferred-core-frame10.report.txt`
  - `powershell -NoProfile -File tools/vortex/Assert-DeferredCoreCaptureReport.ps1 -ReportPath out/build-ninja/analysis/vortex/deferred-core/frame10/deferred-core-frame10.report.txt`
- Result:
  - The Phase 03 closeout gate did not pass.
  - Report: F:\projects\DroidNet\projects\Oxygen.Engine\out\build-ninja\analysis\vortex\deferred-core\frame10\deferred-core-frame10.report.txt
- Missing delta: stage_2_order=fail, stage_3_order=fail, stage_9_order=fail, stage_12_order=fail, gbuffer_contents=fail, scene_color_lit=fail, stencil_local_lights=fail
- RenderDoc runtime validation remains deferred to Phase 04 and is not part of this failure.

### 2026-04-15 — Phase 3 completion claim invalidated by post-execution review

- Changed files this session:
  - `.planning/workstreams/vortex/STATE.md`
  - `.planning/workstreams/vortex/ROADMAP.md`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-15-SUMMARY.md`
  - `tools/vortex/Run-DeferredCoreFrame10Capture.ps1`
- Commands used for verification:
  - advisory review over the final Phase 03 range
  - `cmake --build --preset windows-debug --target oxygen-vortex oxygen-graphics-direct3d12 Oxygen.Vortex.SceneRendererDeferredCore.Tests Oxygen.Vortex.SceneRendererPublication.Tests --parallel 4`
  - `out/build-ninja/bin/Debug/Oxygen.Vortex.SceneRendererDeferredCore.Tests.exe`
  - `out/build-ninja/bin/Debug/Oxygen.Vortex.SceneRendererPublication.Tests.exe`
  - `powershell -ExecutionPolicy Bypass -File tools\cli\oxytidy.ps1 src\Oxygen\Vortex\SceneRenderer\SceneRenderer.cpp -Configuration Debug -IncludeTests -SummaryOnly`
- Result:
  - all 15 Phase 03 plans were executed, but the phase must not be treated as complete
  - review found that Stage 12 still proves telemetry rather than truthful SceneColor accumulation / stencil-bounded local-light behavior
  - review found that the current closeout analyzer overstates key claims through token/name checks instead of artifact/state-level proof
  - review found that the current velocity-completion claim is still a shell seam, not a proven skinned/WPO path
  - the closeout runner now rebuilds the Vortex test executables before `ctest`, but that does not remove the larger Phase 03 blockers
- Code / validation delta:
  - Phase 03 is now explicitly `blocked` again
  - Phase 04 remains locked until review remediation is planned and landed
  - `STATE.md` and `ROADMAP.md` are now brought under managed repo state and reflect the real branch status
- Remaining blocker:
  - plan and implement the Phase 03 remediation work that closes the blocking review findings truthfully

### 2026-04-15 — Phase 3 execution advanced through 03-05 and the ledger now resumes at 03-06

- Changed files this session:
  - `src/Oxygen/Vortex/CMakeLists.txt`
  - `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.h`
  - `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp`
  - `src/Oxygen/Vortex/SceneRenderer/Stages/InitViews/InitViewsModule.h`
  - `src/Oxygen/Vortex/SceneRenderer/Stages/InitViews/InitViewsModule.cpp`
  - `src/Oxygen/Vortex/SceneRenderer/Stages/DepthPrepass/DepthPrepassModule.h`
  - `src/Oxygen/Vortex/SceneRenderer/Stages/DepthPrepass/DepthPrepassModule.cpp`
  - `src/Oxygen/Vortex/Test/CMakeLists.txt`
  - `src/Oxygen/Vortex/Test/SceneRendererDeferredCore_test.cpp`
  - `src/Oxygen/Vortex/Test/SceneRendererPublication_test.cpp`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-03-SUMMARY.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-04-SUMMARY.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-05-SUMMARY.md`
  - `.planning/workstreams/vortex/STATE.md`
  - `.planning/workstreams/vortex/ROADMAP.md`
  - `.planning/workstreams/vortex/REQUIREMENTS.md`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - `cmake --build --preset windows-debug --target oxygen-vortex Oxygen.Vortex.SceneRendererShell.Tests --parallel 4`
  - `ctest --preset test-debug --output-on-failure -R SceneRendererShell`
  - `cmake --build --preset windows-debug --target oxygen-vortex Oxygen.Vortex.SceneRendererDeferredCore.Tests Oxygen.Vortex.SceneRendererPublication.Tests --parallel 4`
  - `ctest --test-dir out/build-ninja -C Debug -R "Oxygen\.Vortex\.(SceneRendererDeferredCore|SceneRendererPublication)" --output-on-failure`
  - `cmake --build --preset windows-debug --target oxygen-vortex Oxygen.Vortex.SceneRendererShell.Tests Oxygen.Vortex.SceneRendererDeferredCore.Tests Oxygen.Vortex.SceneRendererPublication.Tests --parallel 4`
  - `ctest --test-dir out/build-ninja -C Debug -R "Oxygen\.Vortex\.(SceneRendererShell|SceneRendererDeferredCore|SceneRendererPublication)" --output-on-failure`
- Result:
  - `03-03` turned Stage 2 into a real `InitViewsModule` with persistent ScenePrep state and per-view prepared-scene storage.
  - `03-04` added a dedicated deferred-core proof surface and forced the missing Stage 2 active-view prepared-frame rebinding fix in `SceneRenderer.cpp`.
  - `03-05` turned Stage 3 into a real `DepthPrepassModule` shell and propagated `depth_prepass_completeness` through `RenderContext.current_view`.
  - the focused shell/publication/deferred-core verification set is green, and the vortex workstream ledger now resumes at `03-06`.
- Code / validation delta:
  - implementation code changed materially in the renderer shell, Stage 2, Stage 3, and the deferred-core proof surface
  - validation evidence exists for shell/publication/deferred-core test coverage, but there is still no proof yet for real depth-prepass draw processing, Stage 3 publication products, base-pass output, or deferred lighting
  - Phase 3 is therefore `in_progress`, not `done`
- Remaining blocker:
  - execute `03-06` to land real depth-prepass mesh processing and Stage 3 publication proof before the base pass depends on those outputs

### 2026-04-15 — Phase 3 replanned into 15 smaller execute-ready steps after replacing the coarse plan set

- Changed files this session:
  - `.planning/workstreams/vortex/ROADMAP.md`
  - `.planning/workstreams/vortex/STATE.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-01-PLAN.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-02-PLAN.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-03-PLAN.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-04-PLAN.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-05-PLAN.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-06-PLAN.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-07-PLAN.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-08-PLAN.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-09-PLAN.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-10-PLAN.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-11-PLAN.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-12-PLAN.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-13-PLAN.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-14-PLAN.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-15-PLAN.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-RESEARCH.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-VALIDATION.md`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - `node "$HOME/.codex/get-shit-done/bin/gsd-tools.cjs" phase-plan-index 3 --ws vortex`
  - `node "$HOME/.codex/get-shit-done/bin/gsd-tools.cjs" verify plan-structure <each 03-0X-PLAN.md>`
  - repeated checker passes against the rebuilt Phase 03 plan set until no blockers remained
- Result:
  - the earlier coarse 5-plan Phase 03 set was discarded as too broad to execute safely
  - Phase 03 is now split into 15 smaller sequential plans covering:
    shared shader contracts, initial depth/base shader seeding, InitViews implementation and proof, depth-prepass implementation and proof, base-pass implementation and proof, velocity completion, GBuffer debug visualization, deferred-light shader family, Stage 12 CPU deferred lighting, automated proof sweep, and automated RenderDoc/ledger closeout
  - the final checker pass cleared blocking issues and warnings, and the plan set is now execution-ready
- Code / validation delta:
  - no implementation code changed in this replanning session
  - no build/tests/ShaderBake/RenderDoc execution evidence was collected yet because this session only rebuilt the planning surface
  - Phase 03 remains `not_started` from an implementation standpoint, but planning is complete and execution can start at `03-01`
- Remaining blocker:
  - none at the planning boundary; execute the Phase 03 plans in order starting with `03-01`

### 2026-04-14 — Phase 3 deferred-core planning artifacts created for the vortex workstream

- Changed files this session:
  - `.planning/workstreams/vortex/ROADMAP.md` (local ignored workflow state restored)
  - `.planning/workstreams/vortex/STATE.md` (local ignored workflow state restored and advanced)
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-RESEARCH.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-VALIDATION.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-01-PLAN.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-02-PLAN.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-03-PLAN.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-04-PLAN.md`
  - `.planning/workstreams/vortex/phases/03-deferred-core/03-05-PLAN.md`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - `node "$HOME/.codex/get-shit-done/bin/gsd-tools.cjs" init plan-phase 3 --ws vortex`
  - `node "$HOME/.codex/get-shit-done/bin/gsd-tools.cjs" roadmap get-phase 3 --ws vortex --raw`
  - `node "$HOME/.codex/get-shit-done/bin/gsd-tools.cjs" phase-plan-index 3 --ws vortex`
  - `node "$HOME/.codex/get-shit-done/bin/gsd-tools.cjs" verify plan-structure <each 03-0X-PLAN.md>`
  - requirement-coverage grep over `SHDR-01`, `DEFR-01`, and `DEFR-02`
- Result:
  - the local `vortex` workstream planning surface was restored so GSD could route Phase 3 again
  - Phase 03 now has a research artifact, validation strategy, and five executable plan files aligned to the deferred-core dependency chain:
    shader foundation -> InitViews -> depth prepass -> base pass -> deferred lighting
  - structural validation passed for all five plan files, and the Phase 3 requirement IDs are fully covered by the plan set
  - proper plan-checker verification is still pending; execution must not start from this ledger entry alone
- Code / validation delta:
  - no implementation code changed
  - no build, tests, `ShaderBake`, or RenderDoc validation were run in this planning session because Phase 03 execution has not started yet
  - Phase 03 remains `not_started` from an implementation standpoint, and the plan-checker gate must pass before this planning set is treated as ready to execute
- Remaining blocker:
  - execute `.planning/workstreams/vortex/phases/03-deferred-core/03-01-PLAN.md` through `03-05-PLAN.md` and collect the proof pack defined in those plans

### 2026-04-14 — Phase 3 execute-phase preflight blocked at the planning boundary

- Changed files this session:
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - `node "$HOME/.codex/get-shit-done/bin/gsd-tools.cjs" init execute-phase 3 --ws vortex`
  - `node "$HOME/.codex/get-shit-done/bin/gsd-tools.cjs" phase-plan-index 3 --ws vortex`
  - `node "$HOME/.codex/get-shit-done/bin/gsd-tools.cjs" workstream status vortex --raw`
  - `Get-ChildItem .planning/workstreams/vortex/phases`
  - `rg -n "Phase 3 planning|Phase 3 deferred-core planning|03-PLAN|03-VERIFICATION" design/vortex .planning/workstreams/vortex`
- Result:
  - `gsd-tools` resolves the `vortex` workstream, but Phase `3` still reports `phase_found: false`, `plan_count: 0`, and `Phase not found` in the phase index.
  - The workstream only contains planned phase directories `00`, `01`, and `02`; there is no `.planning/workstreams/vortex/phases/03-*` directory or `03-*-PLAN.md` set to execute.
  - The Vortex ledger and Phase 02 close-out artifacts both point to Phase 03 planning as the next valid step, so execute-phase must not claim Phase 03 work has started.
- Code / validation delta:
  - no implementation code changed
  - no build or tests were run because execution stopped during preflight before a trustworthy Phase 03 task boundary existed
  - Phase 03 remains `not_started`; this session records a blocker only
- Remaining blocker:
  - create the Phase 03 `.planning/workstreams/vortex/phases/03-*` plan set, then rerun `/gsd-execute-phase 3 --ws vortex`

### 2026-04-14 — Phase 2 remediation landed; full Vortex proof is green, human review rerun still required

- Changed files this session:
  - `src/Oxygen/Vortex/SceneRenderer/SceneTextures.h`
  - `src/Oxygen/Vortex/SceneRenderer/SceneTextures.cpp`
  - `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.h`
  - `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp`
  - `src/Oxygen/Vortex/SceneRenderer/ResolveSceneColor.cpp`
  - `src/Oxygen/Vortex/SceneRenderer/PostRenderCleanup.cpp`
  - `src/Oxygen/Vortex/Renderer.h`
  - `src/Oxygen/Vortex/Renderer.cpp`
  - `src/Oxygen/Vortex/RenderContext.h`
  - `src/Oxygen/Vortex/SceneRenderer/Stages/*/.gitkeep`
  - `src/Oxygen/Vortex/Test/Fixtures/RendererPublicationProbe.h`
  - `src/Oxygen/Vortex/Test/Fakes/Graphics.h`
  - `src/Oxygen/Vortex/Test/RenderContext_test.cpp`
  - `src/Oxygen/Vortex/Test/SceneTextures_test.cpp`
  - `src/Oxygen/Vortex/Test/SceneRendererPublication_test.cpp`
  - `src/Oxygen/Vortex/Test/SceneRendererShell_test.cpp`
  - `design/vortex/lld/shader-contracts.md`
- Commands used for verification:
  - `cmake --build --preset windows-debug --target oxygen-vortex Oxygen.Vortex.SceneRendererPublication Oxygen.Vortex.SceneTextures Oxygen.Vortex.SceneRendererShell Oxygen.Vortex.RenderContext.Tests --parallel 4`
  - `ctest --test-dir out/build-ninja -C Debug --output-on-failure -R '^Oxygen\.Vortex\.'`
  - `tools/cli/oxytidy.ps1 src/Oxygen/Vortex/Renderer.cpp src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp src/Oxygen/Vortex/SceneRenderer/SceneTextures.cpp src/Oxygen/Vortex/Test/SceneRendererPublication_test.cpp src/Oxygen/Vortex/Test/SceneRendererShell_test.cpp src/Oxygen/Vortex/Test/SceneTextures_test.cpp src/Oxygen/Vortex/Test/RenderContext_test.cpp -IncludeTests -Configuration Debug`
  - Phase-06 acceptance grep bundle
  - Phase-07 acceptance grep bundle
  - Phase-08 acceptance grep bundle
- Result:
  - `SceneTextures` is back to being a pure product-family owner while `SceneRenderer` owns setup progression, binding refresh, and extract state.
  - Scene-texture routing is descriptor-backed, `scene_color_uav` no longer aliases `scene_color_srv`, and Stage 21 / Stage 23 publish explicit artifact/history textures instead of live attachment aliases.
  - `RenderContext` now materializes every eligible frame view into `frame_views` with an explicit `active_view_index`, and the shell bootstrap extent uses the max scene-view envelope instead of first-view selection.
  - Publication tests are hack-free via `RendererPublicationProbe.h`, the required `SceneRenderer/Stages/` directory tree is committed, and the changed-file oxytidy sweep reports `0 warnings, 0 errors`.
  - The full `Oxygen.Vortex.*` suite passed (`28/28`).
- Code / validation delta:
  - Phase 2 implementation work is now materially present and proof-backed.
  - Automated verification is green at both the targeted-remediation and full-Vortex-suite level.
  - Phase 2 is still **not** ready to close because the human `PHASE2-REVIEW.md` approval gate has not yet been rerun on the remediated branch.
- Remaining blocker:
  - rerun the human Phase 2 review/approval gate (`02-05`) against the remediated tree and either approve close-out or capture any new human findings explicitly

### 2026-04-14 — Phase 2 closed after explicit human review pass

- Changed files this session:
  - `.planning/workstreams/vortex/phases/02-scenetextures-and-scenerenderer-shell/02-REVIEW.md`
  - `.planning/workstreams/vortex/phases/02-scenetextures-and-scenerenderer-shell/02-VERIFICATION.md`
  - `.planning/workstreams/vortex/phases/02-scenetextures-and-scenerenderer-shell/02-05-SUMMARY.md`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - human review verdict recorded as `PASS`
  - `ctest --test-dir out/build-ninja -C Debug --output-on-failure -R '^Oxygen\.Vortex\.'`
  - `tools/cli/oxytidy.ps1 src/Oxygen/Vortex/Renderer.cpp src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp src/Oxygen/Vortex/SceneRenderer/SceneTextures.cpp src/Oxygen/Vortex/Test/SceneRendererPublication_test.cpp src/Oxygen/Vortex/Test/SceneRendererShell_test.cpp src/Oxygen/Vortex/Test/SceneTextures_test.cpp src/Oxygen/Vortex/Test/RenderContext_test.cpp -IncludeTests -Configuration Debug`
- Result:
  - the remediated Phase 02 tree was explicitly approved by human review
  - the full `Oxygen.Vortex.*` suite remains green (`28/28`)
  - the changed-file scoped `oxytidy` run remains clean (`0 warnings, 0 errors`)
  - Phase 02 is now closed and Phase 03 is the next valid work item
- Code / validation delta:
  - no new implementation code changed in this close-out step
  - the remaining non-automated approval gate is now satisfied
- Remaining blocker:
  - none for Phase 2; resume with Phase 3 planning

### 2026-04-13 — Phase 1 re-closed after 01-14 remediation and strengthened proof

- Changed files this session:
  - `src/Oxygen/Vortex/CMakeLists.txt`
  - `src/Oxygen/Vortex/Renderer.h`
  - `src/Oxygen/Vortex/Renderer.cpp`
  - `src/Oxygen/Vortex/RendererCapability.h`
  - `src/Oxygen/Vortex/Internal/CompositingPass.h`
  - `src/Oxygen/Vortex/Internal/CompositingPass.cpp`
  - `src/Oxygen/Vortex/SceneRenderer/Internal/FramePlanBuilder.h`
  - `src/Oxygen/Vortex/SceneRenderer/Internal/ShaderDebugMode.h`
  - `src/Oxygen/Vortex/SceneRenderer/Internal/ShaderPassConfig.h`
  - `src/Oxygen/Vortex/SceneRenderer/Internal/ToneMapPassConfig.h`
  - `src/Oxygen/Vortex/Test/CMakeLists.txt`
  - `src/Oxygen/Vortex/Test/Link_test.cpp`
  - `src/Oxygen/Vortex/Test/RendererCapability_test.cpp`
  - `src/Oxygen/Vortex/Test/RendererCompositionQueue_test.cpp`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-VERIFICATION.md`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-VALIDATION.md`
- Commands used for verification:
  - `cmake --build --preset windows-debug --target oxygen-vortex --parallel 4`
  - `rg -n 'kDeferredShading' src/Oxygen/Vortex/RendererCapability.h`
  - `rg -n 'Internal/RenderContextPool.h' src/Oxygen/Vortex/Renderer.h`
  - `rg -n 'SceneRenderer/ShaderDebugMode.h|SceneRenderer/ShaderPassConfig.h|SceneRenderer/ToneMapPassConfig.h' src/Oxygen/Vortex/CMakeLists.txt`
  - `rg -n 'oxygen::imgui' src/Oxygen/Vortex/CMakeLists.txt`
  - `rg -n '^#include <Oxygen/Renderer/|Oxygen/Renderer/' src/Oxygen/Vortex`
  - `cmake --build --preset windows-debug --target oxygen-vortex Oxygen.Vortex.LinkTest Oxygen.Vortex.RendererCapability.Tests Oxygen.Vortex.RendererCompositionQueue.Tests --parallel 4`
  - `ctest --test-dir out/build-ninja -C Debug -R '^Oxygen\.Vortex\.' --output-on-failure`
  - `powershell -NoProfile -Command "$ninja = (Select-String -Path 'out/build-ninja/CMakeCache.txt' -Pattern '^CMAKE_MAKE_PROGRAM:FILEPATH=(.+)$').Matches[0].Groups[1].Value; & $ninja -C out/build-ninja -f build-Debug.ninja -t query bin/Debug/Oxygen.Vortex-d.dll"`
- Result:
  - `Renderer::OnCompositing()` now drains queued submissions through a Vortex-owned compositing path instead of clearing them unused
  - `RendererCapabilityFamily` now contains the required Phase 1 `kDeferredShading` vocabulary entry, while the default runtime capability set is reduced to truthful substrate families only
  - the temporary `FramePlanBuilder` support contracts now live under `SceneRenderer/Internal/` and are no longer exported as public Phase 1 API
  - `oxygen-vortex` no longer links `oxygen::imgui`, and the final Ninja query shows no `Oxygen.ImGui` edge
  - `Renderer.h` no longer leaks `Internal/RenderContextPool.h`
  - Vortex now carries local regression coverage for composition-queue execution and capability/default hygiene, and the hardened `Link_test` also rejects the reopened boundary regressions
  - the strengthened Vortex-only proof suite passed on the repaired tree: Vortex build, Vortex-local tests (`3/3`), hermeticity scan, and final target-edge proof
- Code / validation delta:
  - Phase 1 returns from `reopened` to `done`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-VERIFICATION.md` is restored from `gaps_found` to `passed`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-VALIDATION.md` marks the `01-14` remediation checks green
  - Phase 2 may resume from the repaired Phase 1 baseline
- Remaining blocker:
  - none at the Phase 1 boundary; next action is Phase 2 execution

### 2026-04-13 — Phase 1 reopened after comprehensive architecture/LLD compliance review

- Changed files this session:
  - `design/vortex/IMPLEMENTATION-STATUS.md`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-VERIFICATION.md`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-VALIDATION.md`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/.continue-here.md`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-14-PLAN.md`
- Commands used for verification:
  - `rg -n '^#include <Oxygen/Renderer/|Oxygen/Renderer/' src/Oxygen/Vortex`
  - `cmake --build --preset windows-debug --target oxygen-vortex Oxygen.Vortex.LinkTest Oxygen.Renderer.LinkTest --parallel 4`
  - `ctest --test-dir out/build-ninja -C Debug -R '^Oxygen\.Vortex\.LinkTest$' --output-on-failure`
  - `ctest --test-dir out/build-ninja -C Debug --output-on-failure -R 'Oxygen\.Renderer\.(LinkTest|CompositionPlanner\.Tests|SceneCameraViewResolver\.Tests|RenderContext\.Tests|RenderContextMaterializer\.Tests|RendererCapability\.Tests|RendererCompositionQueue\.Tests|RendererPublicationSplit\.Tests|RendererFacadePresets\.Tests|SinglePassHarnessFacade\.Tests|RenderGraphHarnessFacade\.Tests|OffscreenSceneFacade\.Tests|GpuTimelineProfiler\.Tests|LightCullingConfig\.Tests|ScenePrep\.Tests|UploadCoordinator\.Tests|RingBufferStaging\.Tests|UploadTracker\.Tests|AtlasBuffer\.Tests|UploadPlanner\.Tests|TransientStructuredBuffer\.Tests|TextureBinder\.Tests|MaterialBinder\.Tests|TransformUploader\.Tests|DrawMetadataEmitter\.Tests)'`
  - `powershell -NoProfile -Command "$ninja = (Select-String -Path 'out/build-ninja/CMakeCache.txt' -Pattern '^CMAKE_MAKE_PROGRAM:FILEPATH=(.+)$').Matches[0].Groups[1].Value; & $ninja -C out/build-ninja -f build-Debug.ninja -t query bin/Debug/Oxygen.Vortex-d.dll"`
  - `rg -n 'DispatchSceneRendererPreRender|DispatchSceneRendererRender|DispatchSceneRendererCompositing|RegisterComposition|pending_compositions_' src/Oxygen/Vortex/Renderer.cpp src/Oxygen/Vortex/Renderer.h`
  - `rg -n 'kDeferredShading|RendererCapabilityFamily' src/Oxygen/Vortex/RendererCapability.h design/vortex/DESIGN.md design/vortex/lld/substrate-migration-guide.md`
  - `rg -n 'oxygen::imgui|RenderContextPool.h|ShaderDebugMode|ShaderPassConfig|ToneMapPassConfig' src/Oxygen/Vortex/CMakeLists.txt src/Oxygen/Vortex/Renderer.h src/Oxygen/Vortex/SceneRenderer`
- Result:
  - the earlier `01-13` hermeticity proof remains valid: the Vortex source tree has no `Oxygen/Renderer/*` include seam, `oxygen-vortex` builds, `Oxygen.Vortex.LinkTest` passes, the targeted 25-test legacy substrate regression suite passes, and the Debug Ninja query still shows no `oxygen-renderer` / `Oxygen.Renderer` dependency edge
  - however, the broader compliance review found five unresolved Phase 1 violations:
    - `Renderer::OnCompositing()` still drops queued composition work instead of preserving the renderer-core composition execution contract
    - `RendererCapabilityFamily` is still missing the required Phase 1 `kDeferredShading` vocabulary entry, and the default runtime capability set over-claims later-domain families
    - `ShaderDebugMode`, `ShaderPassConfig`, and `ToneMapPassConfig` are exported as public SceneRenderer-facing contracts even though Phase 1 only authorized the bounded step-1.7 redistribution slice
    - `oxygen-vortex` still publicly links `oxygen::imgui`, leaking Diagnostics/UI domain ownership into the Phase 1 substrate boundary
    - public `Renderer.h` still includes `Oxygen/Vortex/Internal/RenderContextPool.h`, violating the `Internal/` header rule
  - the previous `Phase 1 is complete` claim is therefore superseded
- Code / validation delta:
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-VERIFICATION.md` is downgraded from `passed` to `gaps_found`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-VALIDATION.md` now defines the remediation verification pack required to close the reopened gaps
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-14-PLAN.md` is now the active repair plan for Phase 1
  - no Phase 2 implementation work may begin while these reopened Phase 1 issues remain
- Remaining blocker:
  - execute `01-14` to repair the renderer-core contract and ABI/dependency hygiene gaps, then rerun the strengthened Phase 1 proof suite before restoring any completion claim

### 2026-04-13 — Phase 1 plan 01-13 closed FOUND-03 with a Vortex-local hermeticity guard and final proof refresh [historical entry later superseded by the reopened-phase review]

- Changed files this session:
  - `src/Oxygen/Vortex/Test/CMakeLists.txt`
  - `src/Oxygen/Vortex/Test/Link_test.cpp`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-VERIFICATION.md`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - `rg -n '^#include <Oxygen/Renderer/|Oxygen/Renderer/' src/Oxygen/Vortex`
  - `cmake --build --preset windows-debug --target oxygen-vortex Oxygen.Vortex.LinkTest --parallel 4`
  - `ctest --test-dir out/build-ninja -C Debug -R '^Oxygen\.Vortex\.LinkTest$' --output-on-failure`
  - `ctest --test-dir out/build-ninja -C Debug --output-on-failure -R 'Oxygen\.Renderer\.(LinkTest|CompositionPlanner\.Tests|SceneCameraViewResolver\.Tests|RenderContext\.Tests|RenderContextMaterializer\.Tests|RendererCapability\.Tests|RendererCompositionQueue\.Tests|RendererPublicationSplit\.Tests|RendererFacadePresets\.Tests|SinglePassHarnessFacade\.Tests|RenderGraphHarnessFacade\.Tests|OffscreenSceneFacade\.Tests|GpuTimelineProfiler\.Tests|LightCullingConfig\.Tests|ScenePrep\.Tests|UploadCoordinator\.Tests|RingBufferStaging\.Tests|UploadTracker\.Tests|AtlasBuffer\.Tests|UploadPlanner\.Tests|TransientStructuredBuffer\.Tests|TextureBinder\.Tests|MaterialBinder\.Tests|TransformUploader\.Tests|DrawMetadataEmitter\.Tests)'`
  - `powershell -NoProfile -Command "$ninja = (Select-String -Path 'out/build-ninja/CMakeCache.txt' -Pattern '^CMAKE_MAKE_PROGRAM:FILEPATH=(.+)$').Matches[0].Groups[1].Value; & $ninja -C out/build-ninja -f build-Debug.ninja -t query bin/Debug/Oxygen.Vortex-d.dll"`
- Result:
  - `Oxygen.Vortex.LinkTest` now scans `src/Oxygen/Vortex` recursively before executing the smoke path, so the Vortex-local test surface fails immediately if any legacy renderer include seam returns
  - the Vortex-wide seam scan still reports no `Oxygen/Renderer/*` match after the `01-12` `FramePlanBuilder` repair
  - `cmake --build --preset windows-debug --target oxygen-vortex Oxygen.Vortex.LinkTest --parallel 4` passed again with the guard compiled into the test surface
  - `ctest --test-dir out/build-ninja -C Debug -R '^Oxygen\.Vortex\.LinkTest$' --output-on-failure` passed with the hermeticity guard active
  - the targeted legacy substrate regression suite still passed (`25/25`)
  - the final Debug Ninja target query for `bin/Debug/Oxygen.Vortex-d.dll` still showed no `oxygen-renderer` / `Oxygen.Renderer` dependency edge
  - `01-VERIFICATION.md` now moves from `gaps_found` to `passed`, and `FOUND-03` is satisfied
- Code / validation delta:
  - Phase 1 now has both the source-level seam fix and a local regression guard against reintroducing `Oxygen/Renderer/*` under `src/Oxygen/Vortex`
  - the build, smoke path, targeted regressions, and linked-artifact dependency proof are all refreshed after the guard landed
  - Phase 1 was reported as **complete** at this point, but that claim is now superseded by the later reopened-phase review above
- Remaining blocker:
  - historical state only; see the reopened-phase blocker entry above for the current control point

### 2026-04-13 — Phase 1 plan 01-12 removed the last FramePlanBuilder source seam

- Changed files this session:
  - `src/Oxygen/Vortex/CMakeLists.txt`
  - `src/Oxygen/Vortex/SceneRenderer/Internal/FramePlanBuilder.cpp`
  - `src/Oxygen/Vortex/SceneRenderer/Internal/FramePlanBuilder.h`
  - `src/Oxygen/Vortex/SceneRenderer/ShaderDebugMode.h`
  - `src/Oxygen/Vortex/SceneRenderer/ShaderPassConfig.h`
  - `src/Oxygen/Vortex/SceneRenderer/ToneMapPassConfig.h`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - `powershell -NoProfile -Command "$bad = rg -n '#include <Oxygen/Renderer/|Oxygen/Renderer/' src/Oxygen/Vortex/SceneRenderer/Internal/FramePlanBuilder.h src/Oxygen/Vortex/SceneRenderer/Internal/FramePlanBuilder.cpp; if ($LASTEXITCODE -ne 1) { Write-Error 'legacy renderer include seam remains in FramePlanBuilder'; exit 1 }; rg -n 'SceneRenderer/ShaderDebugMode.h|SceneRenderer/ShaderPassConfig.h|SceneRenderer/ToneMapPassConfig.h' src/Oxygen/Vortex/CMakeLists.txt | Out-Null"`
  - `cmake --build --preset windows-debug --target oxygen-vortex Oxygen.Vortex.LinkTest --parallel 4`
  - `powershell -NoProfile -Command "$bad = rg -n '#include <Oxygen/Renderer/|Oxygen/Renderer/' src/Oxygen/Vortex; if ($LASTEXITCODE -ne 1) { Write-Error 'legacy renderer include seam still exists under src/Oxygen/Vortex'; exit 1 }; exit 0"`
- Result:
  - `FramePlanBuilder` now consumes Vortex-owned `SceneRenderer/ShaderDebugMode.h`, `ShaderPassConfig.h`, and `ToneMapPassConfig.h` instead of importing `Oxygen/Renderer/*`
  - the Vortex module now exports the rehomed planning contracts from `src/Oxygen/Vortex/SceneRenderer/`
  - `cmake --build --preset windows-debug --target oxygen-vortex Oxygen.Vortex.LinkTest --parallel 4` passed after the seam fix
  - the Vortex-wide include scan now returns no `Oxygen/Renderer/*` matches under `src/Oxygen/Vortex`
  - the stale `01-11` Phase 1 completion claim is superseded by the explicit `01-VERIFICATION.md` gap report; Phase 1 remains `in_progress` until `01-13` adds the hermeticity guard and reruns the full proof suite
- Code / validation delta:
  - the remaining `FOUND-03` source/API seam in `FramePlanBuilder` is now **removed**
  - the Vortex build and smoke target evidence is refreshed after the seam fix
  - Phase 1 remains `in_progress`; `01-13` still owns the Vortex-side hermeticity guard and the final re-verification update
- Remaining blocker:
  - execute `01-13` to add the Vortex-local hermeticity guard, rerun the full proof suite, and update `01-VERIFICATION.md` before claiming Phase 1 complete

### 2026-04-13 — Phase 1 plan 01-11 closed step 1.9 and recorded the smoke/regression evidence

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
  - `01-11` landed the smoke/regression evidence, but the source-level `FOUND-03` seam in `FramePlanBuilder` remained unresolved until the later `01-12` repair identified by `01-VERIFICATION.md`
  - Phase 1 remained `in_progress` pending the seam fix and the later `01-13` hermeticity guard plus re-verification
- Remaining blocker:
  - execute `01-12` to remove the `FramePlanBuilder` seam, then `01-13` to add the hermeticity guard and rerun the full proof suite

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
- Repaired `01-12` is now complete for its owned seam-fix slice:
  `FramePlanBuilder` no longer imports `Oxygen/Renderer/*`, and the remaining
  shader-debug/pass-config planning contracts now live under
  `src/Oxygen/Vortex/SceneRenderer/`.
- Repaired `01-13` is now complete: `Oxygen.Vortex.LinkTest` carries a
  Vortex-local hermeticity guard, the full FOUND-03 proof suite passes again,
  and `01-VERIFICATION.md` is now `passed`.

### Steps (from PLAN.md §3)

| Step | Task | Status | Evidence |
| ---- | ---- | ------ | -------- |
| 1.1 | Cross-cutting types (14 headers) | `done` | `01-01` migrated the frame-binding slice; `01-02` landed the remaining type headers, built `oxygen-vortex`, and verified no `Oxygen.Renderer` dependency edge |
| 1.2 | Upload subsystem (14 files) | `done` | `01-03` migrated the full `Upload/` slice, built `oxygen-vortex`, and proved the linked Vortex DLL has no `oxygen-renderer` / `Oxygen.Renderer` dependency edge |
| 1.3 | Resources subsystem (7 files) | `done` | `01-05` landed the full `Resources/*` slice, built `oxygen-vortex`, and proved the linked Vortex DLL still has no `Oxygen.Renderer` dependency edge |
| 1.4 | ScenePrep subsystem (15 files) | `done` | `01-07` landed `ScenePrep/Extractors.h`, `ScenePrep/Finalizers.h`, and `ScenePrepPipeline.cpp/.h`, built `oxygen-vortex`, and proved the linked Vortex DLL still has no `oxygen-renderer` / `Oxygen.Renderer` dependency edge |
| 1.5 | Internal utilities (7 files) | `done` | `01-07` landed the selected substrate-only `Internal/*` slice, built `oxygen-vortex`, and proved the linked Vortex DLL still has no `oxygen-renderer` / `Oxygen.Renderer` dependency edge |
| 1.6 | Pass base classes (3 files) | `done` | `01-10` landed `Passes/RenderPass`, `GraphicsRenderPass`, and `ComputeRenderPass` together with the Vortex-owned root contracts, then rebuilt `oxygen-vortex` successfully |
| 1.7 | View assembly + composition | `done` | `01-14` moved the temporary planning/config contracts behind `SceneRenderer/Internal/`, preserved the intended private scene-planning boundary, and verified the repaired Vortex/legacy proof pack |
| 1.8 | Renderer orchestrator | `done` | `01-14` restored renderer-core composition execution, added `kDeferredShading`, reduced the default capability set to truthful substrate families, removed the `oxygen::imgui` link edge, and removed the public `Internal/` header leak |
| 1.9 | Smoke test | `done` | `01-14` expanded the Vortex-side proof surface with `RendererCapability` and `RendererCompositionQueue` regressions plus the hardened `Link_test`, then passed the strengthened proof suite |

### Resume Point

Phase 1 is complete again. Resume with Phase 2 planning/execution from the
repaired substrate baseline.

---

## Phase 2 — SceneTextures + SceneRenderer Shell

**Status:** `done`

### Design Prerequisites

| Deliverable | Status |
| ----------- | ------ |
| D.1 SceneTextures four-part contract | `done` |
| D.2 SceneRenderBuilder bootstrap | `done` |
| D.3 SceneRenderer shell dispatch | `done` |

### Work Items (from PLAN.md §4)

| ID | Task | Status | Evidence |
| -- | ---- | ------ | -------- |
| 2.1–2.4 | SceneTextures four-part contract | `done` | `02-06` restored the four-part split, semantic GBuffer naming, and stage-owned setup progression |
| 2.5 | ShadingMode enum | `done` | `02-03` bootstrap/shell proof remains green |
| 2.6 | SceneRenderBuilder | `done` | `02-03` built the shell via `SceneRenderBuilder`, and Phase 2 later closed with explicit human approval |
| 2.7 | SceneRenderer shell (23-stage skeleton) | `done` | `02-03` and `02-08` now prove stage ordering plus multi-view-safe extent policy |
| 2.8 | Wire SceneRenderer into Renderer | `done` | `Renderer.cpp` delegates through the shell while preserving renderer-owned publication/composition seams |
| 2.9 | PostRenderCleanup | `done` | `02-07` now publishes explicit history artifacts instead of live attachment aliases |
| 2.10 | ResolveSceneColor | `done` | `02-07` now publishes explicit resolved artifacts instead of live attachment aliases |
| 2.11 | Stages directory structure | `done` | `02-08` committed `InitViews`, `DepthPrepass`, `Occlusion`, `BasePass`, `Translucency`, `Distortion`, and `LightShaftBloom` stage directories |
| 2.12 | Validate first active `SceneTextures` subset (`SceneColor`, `SceneDepth`, `PartialDepth`, `GBufferA-D`, `Stencil`, `Velocity`, `CustomDepth`) | `done` | Full `Oxygen.Vortex.*` suite passed, scoped `oxytidy` is clean, and the human review rerun passed |

### Resume Point

Phase 2 is complete. Resume with Phase 3 deferred-core planning.

---

## Phase 3 — Deferred Core

**Status:** `blocked`

### Design Prerequisites

| Deliverable | Status |
| ----------- | ------ |
| D.4 Depth prepass LLD | `done` |
| D.5 Base pass LLD | `done` |
| D.6 Deferred lighting LLD | `done` |
| D.7 Shader contracts LLD | `done` |
| D.8 InitViews LLD | `done` |

### What Exists

- `03-01` is complete: the shared Vortex shader-contract layer exists under
  `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Contracts/` and
  `.../Shared/`.
- `03-02` is complete: the first depth/base Vortex shader entrypoints are
  registered in `EngineShaderCatalog.h` and compile through `ShaderBake`.
- `03-03` is complete: Stage 2 now dispatches through
  `src/Oxygen/Vortex/SceneRenderer/Stages/InitViews/InitViewsModule.*`.
- `03-04` is complete: deferred-core tests now prove InitViews publication and
  active-view prepared-frame rebinding.
- `03-05` is complete: Stage 3 now dispatches through
  `src/Oxygen/Vortex/SceneRenderer/Stages/DepthPrepass/DepthPrepassModule.*`
  and publishes `depth_prepass_completeness` onto the active view.
- `03-06` is complete: Stage 3 draw processing and publication proof are
  locked to `PreparedSceneFrame` metadata consumption and Stage 3 timing tests.
- `03-07` and `03-08` are complete: Stage 9 now dispatches through a real
  `BasePassModule`, and `BasePassMeshProcessor` consumes published draw
  metadata through `GetDrawMetadata()` / `AcceptedDrawView`.
- `03-09` and `03-10` are complete: Stage 10 remains the first truthful
  GBuffer promotion boundary, and velocity publication stays alive for dynamic
  geometry.
- `03-11` is complete: Phase 3 carries a registered GBuffer debug-view shader
  path and automated proof surface.
- `03-12` is complete: deferred-light shaders compile through the catalog and
  the local-light shader path now avoids the redundant second depth/world
  reconstruction noted in review.
- `03-13` originally landed Stage 12 telemetry/proof seams only; the later
  remediation replaced that shell with actual deferred-light recording.
- `03-14` is complete: the automated proof sweep locks the validation
  contract to the current deferred-core C++ tree and publication vocabulary.
- `03-15` landed a repo-owned closeout runner under `tools/vortex/`.
- `03-16` and `03-17` landed the deferred-core remediation that put
  real Stage 3, Stage 9, and Stage 12 render paths, added reverse-Z-safe
  depth/stencil handling for the new passes, upgraded the deferred-light family
  with dedicated stencil-mark pixel entrypoints plus procedural local-light
  volume generation, and reran the repo-owned closeout proof successfully.
- `03-18` through `03-20` now exist on the retained runtime branch:
  graphics-layer state continuity is restored, registration/view ownership is
  explicit again, and live `VortexBasic` runtime proof is green on the current
  cleanup-lane build.
- `03-21` is the current open control point. The cleanup lane has already
  landed:
  - hard registry ownership with atomic shared resource/view acquisition
  - explicit Stage 21 resolved scene-color/depth artifacts consumed by
    composition
  - a unified durable VortexBasic RenderDoc wrapper that preserves the probe
    scripts
  - removal of diagnosis-only runtime log spam

### What Is Missing

- Final multi-subagent architectural/LLD/code-quality review over the cleaned
  Phase 03 branch, plus remediation of every finding before any closure claim.
- Align the remaining docs and status surfaces to the current durable point/spot
  local-light product gate already enforced by the runtime validator.

### Resume Point

Phase 3 is blocked. Resume with the remaining `03-21` cleanup-lane work and
the required final multi-subagent review/remediation loop. Do not unlock Phase
4 until that gate is complete.

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
  `parity-analysis.md` do not exist in the repo; the current Vortex design
  package supersedes them.
- The active production path is `Oxygen.Renderer` + `ForwardPipeline`.
- Use frame 10 as the RenderDoc baseline capture point.
