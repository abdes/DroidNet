---
phase: 03-deferred-core
plan: "03"
subsystem: scene-renderer
tags: [vortex, deferred-core, init-views, sceneprep, scene-renderer]
requires:
  - phase: 03-deferred-core/02
    provides: initial Vortex shader entrypoints and truthful deferred-core execution state
provides:
  - a real InitViewsModule owned by SceneRenderer stage 2
  - persistent ScenePrep pipeline/state for frame collection and per-view refinement
  - the first SceneRenderer stage-2 call into InitViews before depth-prepass work
affects: [phase-03-initviews-proof, phase-03-depth-prepass-shell]
tech-stack:
  added: []
  patterns:
    - stage-owned ScenePrep orchestration instead of inline shell logic
    - per-view prepared-scene storage retained inside InitViewsModule for later publication proof
key-files:
  created:
    - src/Oxygen/Vortex/SceneRenderer/Stages/InitViews/InitViewsModule.h
    - src/Oxygen/Vortex/SceneRenderer/Stages/InitViews/InitViewsModule.cpp
    - .planning/workstreams/vortex/phases/03-deferred-core/03-03-SUMMARY.md
  modified:
    - src/Oxygen/Vortex/CMakeLists.txt
    - src/Oxygen/Vortex/SceneRenderer/SceneRenderer.h
    - src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp
    - .planning/workstreams/vortex/STATE.md
    - .planning/workstreams/vortex/ROADMAP.md
    - .planning/workstreams/vortex/REQUIREMENTS.md
key-decisions:
  - "SceneRenderer now owns InitViews as a dedicated stage module instead of keeping ScenePrep dispatch inline."
  - "InitViews retains persistent ScenePrep state and per-view prepared-frame backing storage so later proof work can bind publication on top of one stable module."
patterns-established:
  - "Deferred-core shell stages graduate from placeholder comments to dedicated modules before deeper proof and publication work lands."
requirements-completed: []
requirements-advanced: [DEFR-01]
duration: "~15min"
completed: 2026-04-15
---

# Phase 03 Plan 03: InitViews Module Summary

**Stage 2 of the Vortex deferred-core shell is now a real InitViews module that owns ScenePrep orchestration and runs before the depth-prepass seam**

## Performance

- **Duration:** ~15 min
- **Completed:** 2026-04-15T11:54:28.5868212+04:00
- **Tasks:** 1
- **Files created:** 2

## Accomplishments

- Added `InitViewsModule` under the stage directory with a persistent `ScenePrepPipeline`, `ScenePrepState`, and per-view prepared-scene backing storage.
- Wired `SceneRenderer` to own `init_views_` and call `init_views_->Execute(ctx, scene_textures_)` at Stage 2 instead of leaving the stage as a placeholder comment.
- Registered the new module sources in `Oxygen.Vortex` so the shell builds cleanly with the dedicated InitViews stage surface in place.

## Task Commits

- `82d0e888d` - turn Stage 2 into the InitViews scene-prep module for plan `03-03`

## Files Created/Modified

- `src/Oxygen/Vortex/SceneRenderer/Stages/InitViews/InitViewsModule.h` - public Stage 2 module contract with `Execute(...)` and `GetPreparedSceneFrame(...)`.
- `src/Oxygen/Vortex/SceneRenderer/Stages/InitViews/InitViewsModule.cpp` - ScenePrep frame collection, per-view refinement, finalization, and prepared-frame retention.
- `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.h` - `SceneRenderer` now owns `std::unique_ptr<InitViewsModule> init_views_`.
- `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp` - constructs InitViews when scene-preparation capability is available and invokes it at Stage 2.
- `src/Oxygen/Vortex/CMakeLists.txt` - exports and builds the new InitViews module files.

## Verification Evidence

- The plan `03-03` acceptance grep bundle passed for the `Execute(RenderContext& ctx, SceneTextures& scene_textures)` declaration, the Stage 2 call site, and the `BeginFrameCollection(...)`, `PrepareView(...)`, and `FinalizeView(...)` helper usage.
- `cmake --build --preset windows-debug --target oxygen-vortex Oxygen.Vortex.SceneRendererShell.Tests --parallel 4` completed successfully.
- `ctest --preset test-debug --output-on-failure -R SceneRendererShell` passed all 11 matching shell tests.
- `git diff --check` reported no content defects; only the usual Windows LF->CRLF warnings appeared for touched source files.

## Decisions Made

- Kept publication binding and active-view rebinding out of this slice so `03-04` can prove that boundary explicitly instead of hiding it inside the shell integration.
- Reused the existing ScenePrep pipeline contract directly rather than inventing a second Stage 2 collection mechanism.

## Deviations from Plan

None - plan executed as written.

## Issues Encountered

None.

## User Setup Required

None.

## Next Phase Readiness

- Ready for `03-04-PLAN.md`.
- `DEFR-01` remains pending because this slice only makes Stage 2 real; later plans still need depth-prepass/base-pass publication and GBuffer proof before opaque deferred rendering can be claimed.

## Self-Check

PASSED - the summary file exists, task commit `82d0e888d` exists in history, and the recorded verification commands passed.

---
*Phase: 03-deferred-core*
*Completed: 2026-04-15*
