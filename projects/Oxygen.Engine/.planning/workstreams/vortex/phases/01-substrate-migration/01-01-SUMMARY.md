---
phase: 01-substrate-migration
plan: "01"
subsystem: types
tags: [vortex, substrate, types, cmake, build]

# Dependency graph
requires:
  - phase: 00-02
    provides: Vortex module build proof and alias baseline
provides:
  - First step-1.1 Vortex-owned frame-binding type slice under `src/Oxygen/Vortex/Types/`
  - Debug build proof for `oxygen-vortex` after the migration
  - Evidence-backed Phase 1 ledger state that keeps step 1.1 in progress
affects: [phase-1-progress, vortex-types, vortex-build]

# Tech tracking
tech-stack:
  added: []
  patterns: [mechanical substrate migration, Vortex-local type ownership]

key-files:
  created:
    - src/Oxygen/Vortex/Types/CompositingTask.h
    - src/Oxygen/Vortex/Types/DebugFrameBindings.h
    - src/Oxygen/Vortex/Types/DrawFrameBindings.h
    - src/Oxygen/Vortex/Types/EnvironmentFrameBindings.h
    - src/Oxygen/Vortex/Types/LightCullingConfig.h
    - src/Oxygen/Vortex/Types/LightingFrameBindings.h
    - src/Oxygen/Vortex/Types/ShadowFrameBindings.h
    - src/Oxygen/Vortex/Types/SyntheticSunData.h
    - src/Oxygen/Vortex/Types/VsmFrameBindings.h
  modified:
    - src/Oxygen/Vortex/CMakeLists.txt
    - design/vortex/IMPLEMENTATION-STATUS.md

key-decisions:
  - "Copied LightCullingConfig and SyntheticSunData into Vortex immediately because LightingFrameBindings could not satisfy the no-legacy-include rule otherwise"
  - "Used generated Vortex export/depend metadata rather than raw ninja query text as the proof that oxygen-vortex has no Oxygen.Renderer dependency edge"

patterns-established:
  - "Phase 1 type migrations stay mechanical: namespace/include rewrites only, with Vortex-local payload types added only when required to remove legacy seams"

requirements-completed: [FOUND-02, FOUND-03]

# Metrics
duration: 5min
completed: 2026-04-13
---

# Phase 01 Plan 01: Migrate Cross-Cutting Types Summary

**The first Vortex-owned step-1.1 type slice now lives under `src/Oxygen/Vortex/Types/`, builds in Debug, and remains explicitly incomplete until `01-02` lands the remaining type headers.**

## Performance

- **Duration:** 5 min
- **Started:** 2026-04-13T12:46:37+04:00
- **Completed:** 2026-04-13T12:51:42+04:00
- **Tasks:** 2
- **Files modified:** 11

## Accomplishments

- Migrated the frame-binding headers into `Oxygen.Vortex` with mechanical namespace/include rewrites and wired them into the Vortex target.
- Added Vortex-local `LightCullingConfig.h` and `SyntheticSunData.h` so `LightingFrameBindings.h` no longer depends on `Oxygen/Renderer/` headers.
- Built `oxygen-vortex` in Debug and updated the implementation ledger to mark Phase 1 and step 1.1 as in progress instead of merely planned.

## Task Commits

Each task was committed atomically:

1. **Task 1: Migrate the frame-binding half of step 1.1** - `46e859736` (`feat`)
2. **Task 2: Build the first step-1.1 slice and record honest progress** - `09eec256b` (`docs`)

## Files Created/Modified

- `src/Oxygen/Vortex/CMakeLists.txt` - registers the migrated Vortex type headers in the module target.
- `src/Oxygen/Vortex/Types/*.h` - introduces the first Vortex-owned cross-cutting type slice for Phase 1 step 1.1.
- `design/vortex/IMPLEMENTATION-STATUS.md` - records the migration/build evidence and keeps step 1.1 explicitly in progress.

## Decisions Made

- Kept the migration mechanical and deferred all non-slice cleanup to later Phase 1 plans.
- Treated the `LightingFrameBindings.h` payload includes as a blocking correctness issue and migrated those dependent payload types inline rather than leaving a legacy Renderer seam behind.
- Recorded Phase 1 as `in_progress` rather than `done` or `planned` because only the first half of step 1.1 is landed.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Added Vortex-local lighting payload types**
- **Found during:** Task 1
- **Issue:** `LightingFrameBindings.h` could not meet the plan's "no `Oxygen/Renderer/` include paths" acceptance criterion because it owned by-value `LightCullingConfig` and `SyntheticSunData` payloads that only existed under `Oxygen/Renderer/Types/`.
- **Fix:** Copied `LightCullingConfig.h` and `SyntheticSunData.h` into `src/Oxygen/Vortex/Types/` and rewired `LightingFrameBindings.h` to include the Vortex-local versions.
- **Files modified:** `src/Oxygen/Vortex/CMakeLists.txt`, `src/Oxygen/Vortex/Types/LightCullingConfig.h`, `src/Oxygen/Vortex/Types/LightingFrameBindings.h`, `src/Oxygen/Vortex/Types/SyntheticSunData.h`
- **Verification:** legacy-include leak grep passed; `cmake --build --preset windows-debug --target oxygen-vortex --parallel 4` passed afterward.
- **Committed in:** `46e859736`

---

**Total deviations:** 1 auto-fixed (blocking)
**Impact on plan:** No architectural scope creep. The added headers were the minimum required to satisfy the migration boundary and remove the legacy include seam.

## Issues Encountered

- Raw `ninja -t query oxygen-vortex` output was too noisy to use as trustworthy dependency-edge proof in this workspace, so the validation was grounded against generated Vortex export and dependency metadata instead.

## User Setup Required

None.

## Next Phase Readiness

- `01-01` is complete and evidence-backed.
- Phase 1 remains in progress.
- Continue with `01-02` to land the remaining step-1.1 type headers before proceeding deeper into the substrate migration.

## Self-Check: PASSED

- Summary file exists at `.planning/workstreams/vortex/phases/01-substrate-migration/01-01-SUMMARY.md`.
- Task commit `46e859736` exists in git history.
- Task commit `09eec256b` exists in git history.

---
*Phase: 01-substrate-migration*
*Completed: 2026-04-13*
