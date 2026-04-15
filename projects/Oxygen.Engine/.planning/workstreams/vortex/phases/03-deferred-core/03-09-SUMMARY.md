---
phase: 03-deferred-core
plan: "09"
subsystem: scene-renderer
tags: [vortex, deferred-core, base-pass, stage-10, publication]
requires:
  - phase: 03-deferred-core/08
    provides: Stage 9 deferred draw processing and the pending Stage 10 promotion seam
provides:
  - Stage 10 promotion proof for SceneColor and the four active GBuffer bindings
  - explicit Phase 3 rejection proof for forward shading during base-pass execution
  - renderer timing change that keeps Stage 9 write-only and Stage 10 bindless-publication-only
affects: [phase-03-velocity, phase-03-deferred-lighting]
tech-stack:
  added: []
  patterns:
    - SceneRenderer publishes SceneColor and GBuffer bindings only from Stage 10
    - deferred-core proof surfaces separate Stage 9 writes from Stage 10 promotion timing
key-files:
  created: []
  modified:
    - src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp
    - src/Oxygen/Vortex/Test/SceneRendererDeferredCore_test.cpp
    - src/Oxygen/Vortex/Test/SceneRendererPublication_test.cpp
key-decisions:
  - "Stage 9 remains a write-only deferred attachment boundary; Stage 10 is the first bindless publication point for SceneColor and GBuffers."
  - "Phase 3 forward shading stays an explicit invalid mode rather than silently degrading through the deferred base-pass path."
patterns-established:
  - "Stage proof plans lock timing at the publication seam, not just the producing stage."
  - "Deferred-core tests treat bindless validity as a promoted product contract rather than a side effect of attachment writes."
requirements-completed: [DEFR-01]
duration: "4 min"
completed: 2026-04-15
---

# Phase 03 Plan 09: Stage 10 Promotion Proof Summary

**Stage 10 now exclusively promotes SceneColor and the active GBuffers, with TDD proof that Stage 9 stays write-only and forward mode remains invalid in Phase 3**

## Performance

- **Duration:** 4 min
- **Started:** 2026-04-15T13:47:19+04:00
- **Completed:** 2026-04-15T13:50:56+04:00
- **Tasks:** 1
- **Files modified:** 3

## Accomplishments

- Added the deferred-core proof names required by the plan: `BasePassPromotesGBuffersAtStage10` and `BasePassRejectsForwardModeDuringPhase3`.
- Changed the publication-timing proof so Stage 9 keeps `scene_color_srv`, `scene_color_uav`, and `gbuffer_srvs[0..3]` invalid until Stage 10 runs.
- Moved bindless publication of `SceneColor` and the active GBuffers from `ApplyStage9BasePassState()` to `ApplyStage10RebuildState()`.

## TDD Cycle

### RED

- Added failing proof coverage in `SceneRendererDeferredCore_test.cpp` and `SceneRendererPublication_test.cpp`.
- Verified the failure with:
  `ctest --test-dir out/build-ninja -C Debug -R '^Oxygen\.Vortex\.(SceneRendererDeferredCore|SceneRendererPublication)\.Tests$' --output-on-failure`
- Failure reason: Stage 9 was still publishing `scene_color_srv`, `scene_color_uav`, and `gbuffer_srvs[0..3]` before the Stage 10 rebuild boundary.

### GREEN

- Made `ApplyStage9BasePassState()` stop publishing bindless scene-texture state.
- Made `ApplyStage10RebuildState()` publish `kGBuffers`, `kSceneColor`, and `kStencil` together after `RebuildWithGBuffers()`.
- Re-ran the deferred-core/publication test slice and then the full plan verification chain successfully.

### REFACTOR

- None. The minimal `SceneRenderer.cpp` timing change satisfied the proof without additional cleanup.

## Task Commits

1. **Task 1: Lock Stage 10 promotion and GBuffer publication with tests**
   `c1161eadd` - `test(03-09): prevent Stage 9 from masquerading as the GBuffer publication boundary`
   `d0e324a5d` - `feat(03-09): keep GBuffer publication behind the Stage 10 promotion boundary`

## Files Created/Modified

- `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp` - keeps Stage 9 write-only and moves bindless SceneColor/GBuffer publication to Stage 10.
- `src/Oxygen/Vortex/Test/SceneRendererDeferredCore_test.cpp` - adds the named deferred-core proofs for Stage 10 promotion and forward-mode rejection.
- `src/Oxygen/Vortex/Test/SceneRendererPublication_test.cpp` - proves SceneColor/GBuffer bindings stay invalid before Stage 10 and become valid after it.

## Decisions Made

- Kept the runtime correction inside `SceneRenderer` instead of weakening the new tests, because the LLD makes Stage 10 the canonical promotion boundary.
- Treated forward shading as an explicit Phase 3 invalid path and proved it through the real render flow instead of a stage-state helper shortcut.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

None.

## User Setup Required

None - no external service configuration required.

## Verification Evidence

- `rg -n 'BasePassPromotesGBuffersAtStage10|BasePassRejectsForwardModeDuringPhase3' src/Oxygen/Vortex/Test/SceneRendererDeferredCore_test.cpp` passed.
- `rg -n 'gbuffer_srvs\\[0\\.\\.3\\]|scene_color_srv|scene_color_uav|Stage 10|gbuffer_srvs' src/Oxygen/Vortex/Test/SceneRendererPublication_test.cpp` passed.
- `cmake --build --preset windows-debug --target oxygen-vortex oxygen-graphics-direct3d12 --parallel 4` passed.
- `ctest --test-dir out/build-ninja -C Debug -R 'Oxygen\\.Vortex\\.(SceneRendererDeferredCore|SceneRendererPublication)' --output-on-failure` passed with `2/2` tests green.
- `powershell -ExecutionPolicy Bypass -File tools\cli\oxytidy.ps1 src\Oxygen\Vortex\SceneRenderer\SceneRenderer.cpp src\Oxygen\Vortex\Test\SceneRendererDeferredCore_test.cpp src\Oxygen\Vortex\Test\SceneRendererPublication_test.cpp -Configuration Debug -IncludeTests -SummaryOnly` passed with `0 warnings, 0 errors`.

## Next Phase Readiness

- Ready for `03-10-PLAN.md`.
- `DEFR-01` is now closed on this branch; the next plan can focus on dynamic-geometry velocity completion without reopening the Stage 10 publication contract.

## Self-Check

PASSED - `03-09-SUMMARY.md` exists on disk, and task commits `c1161eadd` and `d0e324a5d` are present in history.

---
*Phase: 03-deferred-core*
*Completed: 2026-04-15*
