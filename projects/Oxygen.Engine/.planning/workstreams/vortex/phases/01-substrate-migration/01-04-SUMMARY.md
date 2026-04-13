---
phase: 01-substrate-migration
plan: "04"
subsystem: substrate
tags: [vortex, abi, sceneprep, cmake]
requires:
  - phase: 01-03
    provides: upload foundation and prior Vortex-local type migration needed before the resource prerequisites could build
provides:
  - Vortex-local prerequisite type headers for the resource slice
  - Vortex-local PreparedSceneFrame and ScenePrep ABI headers
  - oxygen-vortex build proof with Resources/* still deferred
affects: [01-05, resources, sceneprep]
tech-stack:
  added: []
  patterns: [mechanical substrate migration, Vortex-owned ABI bundle before subsystem migration]
key-files:
  created:
    - src/Oxygen/Vortex/PreparedSceneFrame.h
    - src/Oxygen/Vortex/PreparedSceneFrame.cpp
    - src/Oxygen/Vortex/ScenePrep/GeometryRef.h
    - src/Oxygen/Vortex/ScenePrep/Handles.h
    - src/Oxygen/Vortex/ScenePrep/MaterialRef.h
    - src/Oxygen/Vortex/ScenePrep/RenderItemData.h
    - src/Oxygen/Vortex/Types/ConventionalShadowDrawRecord.h
    - src/Oxygen/Vortex/Types/DrawMetadata.h
    - src/Oxygen/Vortex/Types/MaterialShadingConstants.h
    - src/Oxygen/Vortex/Types/PassMask.h
    - src/Oxygen/Vortex/Types/ProceduralGridMaterialConstants.h
  modified:
    - src/Oxygen/Vortex/CMakeLists.txt
    - design/vortex/IMPLEMENTATION-STATUS.md
key-decisions:
  - "Kept Resources/* on disk but out of oxygen-vortex so 01-04 stays a prerequisite-only ABI plan."
  - "Split the CMake source-list changes across two task commits so Task 1 recorded only the type slice and Task 2 recorded the ScenePrep/root ABI bundle."
patterns-established:
  - "Land prerequisite ABI surfaces in Vortex before wiring the dependent subsystem sources."
requirements-completed: [FOUND-02, FOUND-03]
duration: 2 min
completed: 2026-04-13
---

# Phase 01 Plan 04: Prerequisite ABI Bundle Summary

**Vortex-local prerequisite ABI bundle for Resources/*, covering type headers, PreparedSceneFrame, and ScenePrep render-item vocabulary**

## Performance

- **Duration:** 2 min
- **Started:** 2026-04-13T10:22:27Z
- **Completed:** 2026-04-13T10:24:16Z
- **Tasks:** 2
- **Files modified:** 13

## Accomplishments
- Migrated the five resource-facing type headers into `src/Oxygen/Vortex/Types/` with only mechanical namespace, include-path, and export-surface rewrites.
- Migrated `PreparedSceneFrame` plus the prerequisite `ScenePrep` ABI headers into Vortex and wired the full bundle into `oxygen-vortex`.
- Built `oxygen-vortex` successfully and updated the implementation ledger so repaired `01-05` is the explicit next owner of `Resources/*`.

## Task Commits

Each task was committed atomically:

1. **Task 1: Migrate the prerequisite Vortex type headers** - `ca33549aa` (feat)
2. **Task 2: Migrate the prerequisite ScenePrep/root ABI slice and record the handoff to `01-05`** - `8004d0074` (feat)

## Files Created/Modified
- `src/Oxygen/Vortex/Types/PassMask.h` - Vortex-local render-pass flag vocabulary consumed by resource-facing metadata.
- `src/Oxygen/Vortex/Types/DrawMetadata.h` - Per-draw ABI record used by resource upload and draw emission code.
- `src/Oxygen/Vortex/Types/MaterialShadingConstants.h` - Core material shading constants ABI for the future material binder.
- `src/Oxygen/Vortex/Types/ProceduralGridMaterialConstants.h` - Procedural-grid material extension constants.
- `src/Oxygen/Vortex/Types/ConventionalShadowDrawRecord.h` - Shader-facing conventional shadow draw record.
- `src/Oxygen/Vortex/PreparedSceneFrame.h` - Vortex-owned prepared-frame payload and partition-range vocabulary.
- `src/Oxygen/Vortex/PreparedSceneFrame.cpp` - Translation unit for the prepared-frame substrate surface.
- `src/Oxygen/Vortex/ScenePrep/GeometryRef.h` - Stable geometry identity surface for future geometry uploads.
- `src/Oxygen/Vortex/ScenePrep/Handles.h` - Versioned transform, material, and geometry handles.
- `src/Oxygen/Vortex/ScenePrep/MaterialRef.h` - Stable material provenance/reference payload.
- `src/Oxygen/Vortex/ScenePrep/RenderItemData.h` - Render-item ABI shared by future scene prep and resource slices.
- `src/Oxygen/Vortex/CMakeLists.txt` - Wires the prerequisite ABI bundle into `oxygen-vortex` while leaving `Resources/*` deferred.
- `design/vortex/IMPLEMENTATION-STATUS.md` - Records 01-04 completion evidence and points execution to repaired `01-05`.

## Decisions Made
- Kept `Resources/*` uncommitted and out of the Vortex target even though those files are already on disk, because repaired `01-05` is the first plan allowed to own that implementation slice.
- Used a two-step CMake update so the task commits remained truthful to the plan boundary instead of collapsing both ABI slices into one commit.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered
- `cmake --build` emitted the expected IDE warning that `src/Oxygen/Vortex/Resources/*` files exist on disk but are not part of `oxygen-vortex`. This was intentional for 01-04 and served as additional evidence that the 01-05 files were not pulled into this plan.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness
- Repaired `01-05` can now migrate `Resources/*` on top of Vortex-owned prerequisite ABI surfaces.
- The `Resources/*` files remain on disk and uncommitted in the worktree; they must stay out of the 01-04 history.

## Self-Check: PASSED
- Found summary file: `.planning/workstreams/vortex/phases/01-substrate-migration/01-04-SUMMARY.md`
- Found task commit: `ca33549aa`
- Found task commit: `8004d0074`

---
*Phase: 01-substrate-migration*
*Completed: 2026-04-13*
