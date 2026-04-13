---
phase: 01-substrate-migration
plan: 12
subsystem: renderer
tags: [vortex, renderer, hermeticity, frame-plan]
requires:
  - phase: 01-substrate-migration
    provides: 01-11 smoke/regression evidence and the 01-VERIFICATION gap report that identified the remaining FramePlanBuilder seam
provides:
  - Vortex-owned shader-debug and pass-config planning contracts under `src/Oxygen/Vortex/SceneRenderer/`
  - A FramePlanBuilder planning slice with no `Oxygen/Renderer/*` includes
  - Corrected implementation-ledger status that hands final FOUND-03 proof work to 01-13
affects: [01-13, FOUND-03, SceneRenderer]
tech-stack:
  added: []
  patterns: [Vortex-owned planning contracts, proof-carrying implementation ledger corrections]
key-files:
  created:
    - src/Oxygen/Vortex/SceneRenderer/ShaderDebugMode.h
    - src/Oxygen/Vortex/SceneRenderer/ShaderPassConfig.h
    - src/Oxygen/Vortex/SceneRenderer/ToneMapPassConfig.h
    - .planning/workstreams/vortex/phases/01-substrate-migration/01-12-SUMMARY.md
  modified:
    - src/Oxygen/Vortex/CMakeLists.txt
    - src/Oxygen/Vortex/SceneRenderer/Internal/FramePlanBuilder.h
    - src/Oxygen/Vortex/SceneRenderer/Internal/FramePlanBuilder.cpp
    - design/vortex/IMPLEMENTATION-STATUS.md
key-decisions:
  - "Rehomed shader-debug and pass-config planning contracts into Vortex-owned headers instead of wrapper headers that would preserve the legacy seam."
  - "Corrected the stale Phase 1 complete claim in the implementation ledger and handed the final hermeticity guard plus verification refresh to 01-13."
patterns-established:
  - "SceneRenderer planning contracts that Vortex owns live under `src/Oxygen/Vortex/SceneRenderer/`, not under `Oxygen/Renderer/*`."
  - "When verification disproves an earlier completion claim, update the ledger status immediately in the next gap-closure plan instead of carrying the stale state forward."
requirements-completed: [FOUND-03]
duration: 3 min
completed: 2026-04-13
---

# Phase 01 Plan 12: FramePlanBuilder Seam Closure Summary

**Vortex-owned shader-debug and pass-config planning contracts with a renderer-free FramePlanBuilder seam**

## Performance

- **Duration:** 3 min
- **Started:** 2026-04-13T16:49:38+04:00
- **Completed:** 2026-04-13T16:52:33+04:00
- **Tasks:** 2
- **Files modified:** 8

## Accomplishments
- Added Vortex-owned `ShaderDebugMode`, `ShaderPassConfig`, and `ToneMapPassConfig` headers for the scene-renderer planning slice.
- Rewired `FramePlanBuilder` and the Vortex CMake target so the planning slice no longer includes `Oxygen/Renderer/*`.
- Rebuilt `oxygen-vortex` and `Oxygen.Vortex.LinkTest`, then corrected the implementation ledger to keep Phase 1 open until `01-13` adds the hermeticity guard and refreshes verification.

## Task Commits

Each task was committed atomically:

1. **Task 1: Rehome the remaining shader-debug and pass-config contracts into Vortex-owned headers** - `c35102116` (fix)
2. **Task 2: Rebuild Vortex and record the seam-removal evidence** - `10238844e` (docs)

**Plan metadata:** recorded in the summary docs commit for this plan

## Files Created/Modified
- `src/Oxygen/Vortex/SceneRenderer/ShaderDebugMode.h` - Vortex-local shader debug enum and helper predicates for planning decisions.
- `src/Oxygen/Vortex/SceneRenderer/ShaderPassConfig.h` - Vortex-local shader pass planning contract.
- `src/Oxygen/Vortex/SceneRenderer/ToneMapPassConfig.h` - Vortex-local tone-map planning contract.
- `src/Oxygen/Vortex/SceneRenderer/Internal/FramePlanBuilder.h` - consumes the Vortex-owned planning contracts instead of legacy renderer types.
- `src/Oxygen/Vortex/SceneRenderer/Internal/FramePlanBuilder.cpp` - removes the last `Oxygen/Renderer/*` include seam from the live Vortex planning slice.
- `src/Oxygen/Vortex/CMakeLists.txt` - exports the new Vortex-owned scene-renderer headers.
- `design/vortex/IMPLEMENTATION-STATUS.md` - records the seam-removal evidence, corrects the stale Phase 1 completion claim, and hands final proof work to `01-13`.

## Decisions Made
- Reused the existing legacy contract shapes as direct Vortex-owned copies so `FramePlanBuilder` could stay source-compatible without keeping a wrapper seam back to `Oxygen/Renderer`.
- Treated the stale `01-11` “Phase 1 complete” claim as a correctness issue because `01-VERIFICATION.md` had already disproved it, so the ledger now stays `in_progress` until `01-13` reruns the full proof suite.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

- The repo’s commit hook rejected the first Task 1 message because the subject line was not in Conventional Commits format. Retrying with a conventional subject resolved it without changing the staged files.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- `01-13` is ready to add the Vortex-side hermeticity guard and rerun the full Phase 1 proof suite.
- Phase 2 remains blocked until `01-13` updates `01-VERIFICATION.md` and closes the final `FOUND-03` evidence gap.

## Self-Check: PASSED

- Found `.planning/workstreams/vortex/phases/01-substrate-migration/01-12-SUMMARY.md`
- Found commit `c35102116`
- Found commit `10238844e`

---
*Phase: 01-substrate-migration*
*Completed: 2026-04-13*
