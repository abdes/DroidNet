---
phase: 03-deferred-core
plan: "04"
subsystem: testing
tags: [vortex, deferred-core, init-views, testing, publication]
requires:
  - phase: 03-deferred-core/03
    provides: real InitViews module shell integration at Stage 2
provides:
  - dedicated deferred-core proof surface for InitViews publication
  - locked active-view prepared-frame rebinding behavior
  - regression coverage tying InitViews publication to the renderer stage flow
affects: [phase-03-depth-prepass-shell, phase-03-base-pass-shell]
tech-stack:
  added: []
  patterns:
    - deferred-core proof surfaces land before later stage work depends on their assumptions
    - Stage 2 prepared-frame handoff is asserted through renderer-owned context binding
key-files:
  created:
    - src/Oxygen/Vortex/Test/SceneRendererDeferredCore_test.cpp
    - .planning/workstreams/vortex/phases/03-deferred-core/03-04-SUMMARY.md
  modified:
    - src/Oxygen/Vortex/Test/CMakeLists.txt
    - src/Oxygen/Vortex/Test/SceneRendererPublication_test.cpp
    - src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp
    - .planning/workstreams/vortex/STATE.md
    - .planning/workstreams/vortex/ROADMAP.md
    - .planning/workstreams/vortex/REQUIREMENTS.md
key-decisions:
  - "InitViews publication proof lives in a dedicated deferred-core test target instead of being diluted across generic shell coverage."
  - "SceneRenderer owns the active-view prepared-frame handoff, so the fix stayed in Stage 2 binding rather than adding a parallel publication path."
patterns-established:
  - "Proof plans may auto-fix small blocking production seams when a red test exposes a missing renderer handoff."
requirements-completed: []
requirements-advanced: [DEFR-01]
duration: "~15min"
completed: 2026-04-15
---

# Phase 03 Plan 04: InitViews Publication Proof Summary

**InitViews publication and active-view rebinding are now protected by a dedicated deferred-core test target, with the Stage 2 prepared-frame handoff wired through SceneRenderer**

## Performance

- **Duration:** ~15 min
- **Completed:** 2026-04-15T12:07:23.7084214+04:00
- **Tasks:** 1
- **Files created:** 2

## Accomplishments

- Added `SceneRendererDeferredCore_test.cpp` with the first dedicated Phase 3 proof surface for InitViews publication and active-view rebinding.
- Registered the new deferred-core test program in `src/Oxygen/Vortex/Test/CMakeLists.txt` and tightened the existing publication test with an explicit pre-Stage-2 `prepared_frame == nullptr` assertion.
- Fixed the Stage 2 handoff in `SceneRenderer.cpp` so `RenderContext.current_view.prepared_frame` is rebound to the prepared frame emitted by InitViews before later deferred-core stages execute.

## Task Commits

- `837eee9d6` - red TDD commit adding InitViews publication and rebinding proof
- `fbd972a90` - green TDD commit binding the active prepared frame onto the renderer cursor

## Files Created/Modified

- `src/Oxygen/Vortex/Test/SceneRendererDeferredCore_test.cpp` - new deferred-core proof target covering per-view publication and active-view rebinding.
- `src/Oxygen/Vortex/Test/CMakeLists.txt` - registers `Oxygen.Vortex.SceneRendererDeferredCore.Tests`.
- `src/Oxygen/Vortex/Test/SceneRendererPublication_test.cpp` - adds the explicit pre-InitViews prepared-frame null assertion for the render-context selection seam.
- `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp` - binds the active prepared frame after InitViews executes.

## Verification Evidence

- The plan acceptance grep bundle passed for `InitViewsPublishesPreparedSceneFrameForEverySceneView`, `InitViewsKeepsTheActiveViewPreparedFrameBoundToCurrentView`, and the deferred-core test-target registration.
- `cmake --build --preset windows-debug --target oxygen-vortex Oxygen.Vortex.SceneRendererDeferredCore.Tests Oxygen.Vortex.SceneRendererPublication.Tests --parallel 4` passed.
- Direct execution of `out/build-ninja/bin/Debug/Oxygen.Vortex.SceneRendererDeferredCore.Tests.exe` passed both deferred-core tests.
- Direct execution of `out/build-ninja/bin/Debug/Oxygen.Vortex.SceneRendererPublication.Tests.exe` passed all 5 publication tests.
- `ctest --test-dir out/build-ninja -C Debug -R "Oxygen\.Vortex\.(SceneRendererDeferredCore|SceneRendererPublication)" --output-on-failure` passed.

## Decisions Made

- Kept the proof in a dedicated deferred-core target so later Phase 3 regressions can point at one named surface instead of broad shell coverage.
- Repaired the renderer-owned Stage 2 binding seam rather than exposing new production accessors for the tests.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Bound the active prepared frame in `SceneRenderer.cpp`**
- **Found during:** Task 1 (InitViews publication proof)
- **Issue:** The new deferred-core red tests showed that InitViews was publishing prepared-scene payloads without rebinding `RenderContext.current_view.prepared_frame`, so the proof surface could not pass honestly.
- **Fix:** Cleared stale prepared-frame bindings at Stage 2 start and rebound the active cursor to the prepared frame returned by InitViews for the selected view.
- **Files modified:** `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp`
- **Verification:** Rebuilt `oxygen-vortex` plus the two test binaries, then ran the deferred-core binary, publication binary, and the exact plan `ctest` command successfully.
- **Committed in:** `fbd972a90`

---

**Total deviations:** 1 auto-fixed (1 blocking)
**Impact on plan:** The deviation was the minimum production change needed for the new proof surface to validate the intended Stage 2 handoff. No scope drift into later deferred-core stages.

## Issues Encountered

None after the blocking Stage 2 handoff seam was repaired.

## User Setup Required

None.

## Next Phase Readiness

- Ready for `03-05-PLAN.md`.
- `DEFR-01` remains pending because the phase still needs the depth-prepass shell, base-pass shell, and later GBuffer publication proof before opaque deferred rendering can be claimed.

## Self-Check

PASSED - the summary file exists, task commits `837eee9d6` and `fbd972a90` exist in history, and the recorded verification commands passed.

---
*Phase: 03-deferred-core*
*Completed: 2026-04-15*
