---
phase: 03-deferred-core
plan: "14"
subsystem: validation
tags: [vortex, deferred-core, validation, tests, publication]
requires:
  - phase: 03-deferred-core/13
    provides: Stage 12 deferred-light CPU proof and the final published deferred-input boundary
provides:
  - Automated Phase 3 proof coverage pinned to the current deferred-core C++ surface and the 03-14 validation command
  - Explicit Stage 3 and Stage 10 publication-proof names that match the validation contract
  - A truthful pre-RenderDoc gate for the later capture and ledger closeout plan
affects: [phase-03-proof-sweep, phase-03-renderdoc-closeout]
tech-stack:
  added: []
  patterns:
    - Deferred-core proof surfaces can read 03-VALIDATION.md and source files to lock build/test/tidy claims to the current C++ tree
    - Stage publication proofs use semantic Stage 3 and Stage 10 names instead of generic milestone wording
key-files:
  created: []
  modified:
    - src/Oxygen/Vortex/Test/SceneRendererDeferredCore_test.cpp
    - src/Oxygen/Vortex/Test/SceneRendererPublication_test.cpp
key-decisions:
  - "Locked the 03-14 proof sweep by auditing the validation contract from test code instead of widening the plan write scope to edit 03-VALIDATION.md."
  - "Renamed the publication proofs to explicit Stage 3 and Stage 10 timing names so the validation contract and the executable proof surface use the same vocabulary."
patterns-established:
  - "Phase 3 proof-sweep drift is guarded by a deferred-core test that checks the validation command, stage-module files, and publication-proof names together."
  - "Publication timing remains proven in SceneRendererPublication_test.cpp with names that describe the deferred boundary they lock."
requirements-completed: [DEFR-02]
duration: 4 min
completed: 2026-04-15
---

# Phase 03 Plan 14: Automated Proof Sweep Summary

**The Phase 3 automated proof sweep is now pinned to the current deferred-core C++ tree and uses Stage 3/10 publication-proof names that match the validation contract before RenderDoc closeout inherits it**

## Performance

- **Duration:** 4 min
- **Started:** 2026-04-15T15:20:40+04:00
- **Completed:** 2026-04-15T15:24:18+04:00
- **Tasks:** 1
- **Files modified:** 2

## Accomplishments

- Added `AutomatedProofSweepMatchesCurrentPhase3CppSurface` to `SceneRendererDeferredCore_test.cpp` so the 03-14 gate audits the validation command, the current Phase 3 stage/module sources, and the publication-proof vocabulary together.
- Renamed the `SceneRendererPublication_test.cpp` Stage 3/10 proofs to `Stage3PublicationKeepsSceneColorAndGBuffersInvalidUntilStage10` and `Stage10PublicationPromotesSceneColorGBuffersAndKeepsVelocityAlive`.
- Kept the proof sweep separate from the later RenderDoc and ledger work while making the build/test/tidy claims traceable to the current Vortex C++ surface.

## TDD Cycle

### RED

- Added the failing audit proof `AutomatedProofSweepMatchesCurrentPhase3CppSurface`.
- Verified RED with:
  `cmake --build --preset windows-debug --target Oxygen.Vortex.SceneRendererDeferredCore Oxygen.Vortex.SceneRendererPublication --parallel 4`
  then
  `ctest --test-dir out/build-ninja -C Debug --output-on-failure -R "Oxygen\.Vortex\.(SceneRendererDeferredCore|SceneRendererPublication)"`.
- Failure reason: `SceneRendererPublication_test.cpp` still used generic Stage 3/10 milestone names, and the first audit draft overstated the direct depth-prepass metadata token instead of the current `AcceptedDrawView` plus `draw_metadata_bytes` path.

### GREEN

- Renamed the Stage 3/10 publication tests to the validation-contract vocabulary.
- Corrected the audit to accept the current depth-prepass metadata path while still requiring `AcceptedDrawView`.
- Re-ran the narrow build/test slice successfully, then ran the plan's exact full verification chain successfully.

### REFACTOR

- No separate refactor commit was needed.

## Task Commits

1. **Task 1: Tighten the automated proof pack and applicable tidy sweep**
   `487e73cc7` - `test(03-14): expose drift in the Phase 3 proof sweep`
   `c6b4f6534` - `feat(03-14): lock the proof sweep to the current Phase 3 tree`

## Files Created/Modified

- `src/Oxygen/Vortex/Test/SceneRendererDeferredCore_test.cpp` - adds the Phase 3 proof-sweep audit that reads `03-VALIDATION.md` and the current deferred-core source files.
- `src/Oxygen/Vortex/Test/SceneRendererPublication_test.cpp` - renames the Stage 3 and Stage 10 publication proofs to match the validation contract vocabulary.

## Decisions Made

- Kept the validation document authoritative and locked it from the proof surface instead of widening this plan to rewrite the contract.
- Used publication-test renames, not comments or summary-only wording, so the executable proof surface carries the same language as the validation gate.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

- The first RED commit attempt hit a transient Git index-lock / PowerShell quoting issue. The commit was retried safely with a here-string commit message after verifying the lock was gone.
- The exact `oxytidy` sweep returned `2 warnings, 0 errors` in untouched `src/Oxygen/Vortex/SceneRenderer/Stages/InitViews/InitViewsModule.cpp` (`performance-move-const-arg`). Those warnings predate this plan's write scope, the command still exited `0`, and the plan did not modify that file.

## User Setup Required

None - no external setup or secrets were required.

## Verification Evidence

- RED verification:
  `cmake --build --preset windows-debug --target Oxygen.Vortex.SceneRendererDeferredCore Oxygen.Vortex.SceneRendererPublication --parallel 4` passed, then
  `ctest --test-dir out/build-ninja -C Debug --output-on-failure -R "Oxygen\.Vortex\.(SceneRendererDeferredCore|SceneRendererPublication)"` failed with `1/2` tests red because `AutomatedProofSweepMatchesCurrentPhase3CppSurface` caught the missing Stage 3/10 proof names.
- GREEN verification:
  `cmake --build --preset windows-debug --target Oxygen.Vortex.SceneRendererDeferredCore Oxygen.Vortex.SceneRendererPublication --parallel 4; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }; ctest --test-dir out/build-ninja -C Debug --output-on-failure -R "Oxygen\.Vortex\.(SceneRendererDeferredCore|SceneRendererPublication)"` passed with `2/2` tests green.
- Plan verification command passed exactly as written:
  `cmake --build --preset windows-debug --target oxygen-vortex oxygen-graphics-direct3d12 --parallel 4`
  `ctest --test-dir out/build-ninja -C Debug --output-on-failure -R '^Oxygen\.Vortex\.'`
  `tools/cli/oxytidy.ps1 ... -IncludeTests -Configuration Debug`
  Results: build passed, `31/31` Vortex tests passed, `oxytidy` analyzed `8` translation units and reported `2 warnings, 0 errors`.
- Acceptance spot checks passed:
  `rg -n "AutomatedProofSweepMatchesCurrentPhase3CppSurface|SceneRendererPublication_test.cpp|InitViewsModule.cpp|DepthPrepassMeshProcessor.cpp|BasePassMeshProcessor.cpp|tools/cli/oxytidy.ps1|\^Oxygen\\\\.Vortex\\\\." src/Oxygen/Vortex/Test/SceneRendererDeferredCore_test.cpp .planning/workstreams/vortex/phases/03-deferred-core/03-VALIDATION.md`
  `rg -n "Stage3PublicationKeepsSceneColorAndGBuffersInvalidUntilStage10|Stage10PublicationPromotesSceneColorGBuffersAndKeepsVelocityAlive" src/Oxygen/Vortex/Test/SceneRendererPublication_test.cpp`

## Next Phase Readiness

- Ready for `03-15-PLAN.md`.
- The automated Phase 3 proof sweep now locks the validation contract to the current deferred-core C++ surface, so the RenderDoc capture and ledger closeout plan can inherit an honest automated pre-gate.

## Self-Check

PASSED - `03-14-SUMMARY.md` exists on disk, and task commits `487e73cc7`
and `c6b4f6534` are present in history.

---
*Phase: 03-deferred-core*
*Completed: 2026-04-15*
