---
phase: 03-deferred-core
plan: "10"
subsystem: scene-renderer
tags: [vortex, deferred-core, base-pass, velocity, publication]
requires:
  - phase: 03-deferred-core/09
    provides: Stage 10 promotion timing and the write-only Stage 9 GBuffer boundary
provides:
  - explicit BasePassModule velocity-completion state for the Stage 9 dynamic-geometry seam
  - SceneRenderer Stage 9 velocity publication that survives the Stage 10 promotion boundary
  - automated proof for dynamic velocity completion plus final velocity-binding validity
affects: [phase-03-gbuffer-debug, phase-03-deferred-lighting]
tech-stack:
  added: []
  patterns:
    - Base-pass ownership proofs expose Stage 9 completion separately from Stage 10 publication timing
    - velocity publication remains a Stage 9 product even when SceneColor and GBuffers wait for Stage 10
key-files:
  created: []
  modified:
    - src/Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassModule.h
    - src/Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassModule.cpp
    - src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp
    - src/Oxygen/Vortex/Test/SceneRendererDeferredCore_test.cpp
    - src/Oxygen/Vortex/Test/SceneRendererPublication_test.cpp
key-decisions:
  - "BasePassModule now exposes dynamic-velocity completion explicitly instead of leaving Stage 9 ownership implicit."
  - "Stage 9 republishes velocity independently, while Stage 10 remains the first publication boundary for SceneColor and GBuffers."
patterns-established:
  - "Stage-owner proof and publication-boundary proof stay separate even when both cover the same resource family."
  - "Shell-level completion seams are keyed to authored stage contracts, not incidental draw-count details."
requirements-completed: [DEFR-01]
duration: "7 min"
completed: 2026-04-15
---

# Phase 03 Plan 10: Velocity Completion Summary

**Stage 9 now exposes dynamic-geometry velocity completion and keeps the velocity binding alive across the Stage 10 promotion boundary**

## Performance

- **Duration:** 7 min
- **Started:** 2026-04-15T14:00:01+04:00
- **Completed:** 2026-04-15T14:06:57+04:00
- **Tasks:** 1
- **Files modified:** 5

## Accomplishments

- Added the required deferred-core proof `BasePassCompletesVelocityForDynamicGeometry`.
- Made `BasePassModule` report Stage 9 dynamic-velocity completion explicitly.
- Proved the final velocity binding stays valid after Stage 9 and Stage 10 without reopening the 03-09 GBuffer timing contract.

## TDD Cycle

### RED

- Added the failing Stage 9 velocity-completion proof in `SceneRendererDeferredCore_test.cpp`.
- Added the publication proof in `SceneRendererPublication_test.cpp` that locks the Stage 9/10 velocity-binding boundary.
- Verified RED by rebuilding the real gtest targets:
  `cmake --build --preset windows-debug --target Oxygen.Vortex.SceneRendererDeferredCore.Tests Oxygen.Vortex.SceneRendererPublication.Tests --parallel 4`
- Failure reason: `BasePassModule` had no explicit dynamic-velocity completion seam, so the new proof could not compile.

### GREEN

- Added `BasePassModule::HasCompletedVelocityForDynamicGeometry()`.
- Made `SceneRenderer` republish velocity at Stage 9 only when base pass completion actually occurred.
- Re-ran the targeted gtest slice, then the full plan verification chain successfully.

### REFACTOR

- Narrowed the publication proof back to the authored Stage 9/10 seam instead of overreaching into full-frame extraction behavior.
- No separate refactor commit was needed.

## Task Commits

1. **Task 1: Finish velocity completion and prove static plus dynamic coverage**
   `b50e07e15` - `test(03-10): force Stage 9 velocity completion to become observable`
   `ef3b32dca` - `feat(03-10): prevent velocity from remaining a depth-prepass-only artifact`

## Files Created/Modified

- `src/Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassModule.h` - adds the explicit Stage 9 dynamic-velocity completion accessor and state.
- `src/Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassModule.cpp` - marks the Stage 9 completion seam when deferred velocity writing is enabled.
- `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp` - republishes velocity at Stage 9 only when base-pass completion occurred, while leaving SceneColor/GBuffer publication at Stage 10.
- `src/Oxygen/Vortex/Test/SceneRendererDeferredCore_test.cpp` - proves Stage 9 dynamic-geometry velocity completion through the named plan-required test.
- `src/Oxygen/Vortex/Test/SceneRendererPublication_test.cpp` - proves velocity stays published across the Stage 9/10 boundary.

## Decisions Made

- Kept the velocity publication fix in `SceneRenderer` instead of moving velocity to Stage 10, because 03-09 already locked Stage 10 as a SceneColor/GBuffer promotion boundary.
- Treated Stage 9 completion as an explicit shell-level contract, because Phase 03 is still proving authored stage ownership rather than real GPU velocity sub-pass output.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

- `ctest` alone initially exercised stale binaries, so RED verification was rerun against the explicit `Oxygen.Vortex.SceneRendererDeferredCore.Tests` and `Oxygen.Vortex.SceneRendererPublication.Tests` build targets before continuing.

## User Setup Required

None - no external service configuration required.

## Verification Evidence

- `rg -n "BasePassCompletesVelocityForDynamicGeometry" src/Oxygen/Vortex/Test/SceneRendererDeferredCore_test.cpp` passed.
- `cmake --build --preset windows-debug --target Oxygen.Vortex.SceneRendererDeferredCore.Tests Oxygen.Vortex.SceneRendererPublication.Tests --parallel 4` failed in RED with the missing `BasePassModule::HasCompletedVelocityForDynamicGeometry` seam, then passed after implementation.
- `cmake --build --preset windows-debug --target oxygen-vortex oxygen-graphics-direct3d12 --parallel 4` passed.
- `ctest --test-dir out/build-ninja -C Debug -R "Oxygen\.Vortex\.(SceneRendererDeferredCore|SceneRendererPublication)" --output-on-failure` passed with `2/2` tests green.
- `powershell -ExecutionPolicy Bypass -File tools\cli\oxytidy.ps1 src\Oxygen\Vortex\SceneRenderer\Stages\BasePass\BasePassModule.h src\Oxygen\Vortex\SceneRenderer\Stages\BasePass\BasePassModule.cpp src\Oxygen\Vortex\SceneRenderer\SceneRenderer.cpp src\Oxygen\Vortex\Test\SceneRendererDeferredCore_test.cpp src\Oxygen\Vortex\Test\SceneRendererPublication_test.cpp -Configuration Debug -IncludeTests -SummaryOnly` passed with `0 warnings, 0 errors`.

## Post-Review Remediation

- Addressed the follow-on review after `03-10`: Stage 9 now refuses to publish base-pass products when the current view lacks a prepared-scene payload, and `SceneRenderer` only instantiates `BasePassModule` when `kDeferredShading` is available.
- Aligned the stage-module constructor signatures with the LLDs by threading `SceneTexturesConfig` through both `BasePassModule` and `DepthPrepassModule`, even though the current Phase 3 implementations still treat the config as contract-only input.
- Propagated the velocity policy into `BasePassMeshProcessor` so `writes_velocity` mirrors the active Stage 9 config instead of being hardcoded true.
- Resolved F-06 by moving typed draw-metadata access into `PreparedSceneFrame`, adding `AcceptedDrawView` as the shared partition-aware filtering surface, and migrating both mesh processors to that non-owning view instead of open-coding `reinterpret_cast` plus partition/range plumbing.
- Added review-specific regression coverage: `BasePassRequiresPreparedFrameBeforePublishingProducts`, `BasePassMeshProcessorHonorsVelocityPolicy`, and extra no-deferred-capability assertions in `DepthPrepassStaysDisabledWithoutDeferredShadingCapability`.
- Added unit coverage for the shared draw view itself: `PreparedSceneFrameExposesTypedDrawMetadata`, `PartitionedIterationSkipsRejectedPartitions`, `FlatIterationFiltersByDrawFlags`, and `EmptyMetadataYieldsNoAcceptedDraws`.
- Re-ran the live test executables directly after the fix because `ctest` discovery briefly surfaced stale expectation output:
  `out/build-ninja/bin/Debug/Oxygen.Vortex.SceneRendererPublication.Tests.exe` and
  `out/build-ninja/bin/Debug/Oxygen.Vortex.SceneRendererDeferredCore.Tests.exe`
  both passed.

## Next Phase Readiness

- Ready for `03-11-PLAN.md`.
- Stage 9 now owns velocity publication explicitly, so the next plan can add GBuffer debug visualization without revisiting the velocity/publication seam.

## Self-Check

PASSED - `03-10-SUMMARY.md` exists on disk, and task commits `b50e07e15` and `ef3b32dca` are present in history.

---
*Phase: 03-deferred-core*
*Completed: 2026-04-15*
