---
phase: 03-deferred-core
plan: "08"
subsystem: scene-renderer
tags: [vortex, deferred-core, base-pass, stage-9, shaders]
requires:
  - phase: 03-deferred-core/07
    provides: module-owned Stage 9 shell integration and the preserved Stage 10 promotion boundary
provides:
  - Prepared-scene-driven BasePassMeshProcessor sorting for Stage 9 deferred draws
  - reusable Stage 9 material/shader adapter includes for real BasePassGBuffer entrypoints
  - shared position-reconstruction helper seed for later deferred-light consumption
affects: [phase-03-stage10-proof, phase-03-deferred-lighting]
tech-stack:
  added: []
  patterns:
    - Stage 9 consumes PreparedSceneFrame when draw payloads exist while still publishing blank deferred attachments for valid deferred frames
    - Vortex base-pass shaders route material evaluation through reusable adapter includes instead of stage-local pixel-shader logic
key-files:
  created:
    - src/Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassMeshProcessor.h
    - src/Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassMeshProcessor.cpp
    - src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Materials/MaterialTemplateAdapter.hlsli
    - src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Shared/PositionReconstruction.hlsli
  modified:
    - src/Oxygen/Vortex/CMakeLists.txt
    - src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp
    - src/Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassModule.h
    - src/Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassModule.cpp
    - src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Stages/BasePass/BasePassGBuffer.hlsl
    - src/Oxygen/Vortex/Test/SceneRendererDeferredCore_test.cpp
    - src/Oxygen/Vortex/Test/SceneRendererPublication_test.cpp
key-decisions:
  - "BasePassMeshProcessor groups Stage 9 work by material and geometry while falling back to per-view render-item ordering when draw metadata is not yet published."
  - "Stage 9 publication stays deferred-only, but blank SceneColor/GBuffer attachments remain publishable for valid deferred frames even when there are no prepared draws."
patterns-established:
  - "Stage-local mesh processing now lives behind BasePassModule, while shared shader helpers carry the reusable material/reconstruction contracts for later deferred-light plans."
requirements-completed: []
requirements-advanced: [DEFR-01]
duration: "9 min"
completed: 2026-04-15
---

# Phase 03 Plan 08: Base-Pass Processing Summary

**Prepared-scene-driven Stage 9 base-pass mesh processing with reusable Vortex GBuffer shader adapters and deferred publication proof**

## Performance

- **Duration:** 9 min
- **Started:** 2026-04-15T09:31:59.6134581Z
- **Completed:** 2026-04-15T09:40:44.4108881Z
- **Tasks:** 1
- **Files modified:** 11

## Accomplishments

- Added `BasePassMeshProcessor` and wired `BasePassModule` to consume `PreparedSceneFrame` for Stage 9 deferred draw-command construction.
- Replaced the Stage 9 shader-only direct material path with `MaterialTemplateAdapter.hlsli` and kept `BasePassGBufferVS` / `BasePassGBufferPS` as real compiled entrypoints.
- Seeded `PositionReconstruction.hlsli` for later deferred-light work and aligned publication tests with the new deferred-only Stage 9 contract.

## Task Commits

- `15e4853d0` - `feat(03-08): route Stage 9 through a real base-pass surface`

## Files Created/Modified

- `src/Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassMeshProcessor.h` - declares the Stage 9 deferred draw-command surface and published command payload.
- `src/Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassMeshProcessor.cpp` - builds material-sorted Stage 9 draw commands from prepared-scene metadata or render-item fallbacks.
- `src/Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassModule.h` - adds mesh-processor ownership and Stage 9 publication-state reporting.
- `src/Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassModule.cpp` - consumes prepared-scene payloads when present and exposes deferred-only Stage 9 publication readiness.
- `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp` - gates Stage 9/10 promotion on `BasePassModule` publication rather than unconditional shell execution.
- `src/Oxygen/Vortex/CMakeLists.txt` - registers the new Stage 9 mesh-processor sources.
- `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Stages/BasePass/BasePassGBuffer.hlsl` - routes the pixel path through the new base-pass material adapter while preserving the compiled VS/PS entrypoints.
- `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Materials/MaterialTemplateAdapter.hlsli` - centralizes Stage 9 alpha-clip/material evaluation glue for Vortex material shaders.
- `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Shared/PositionReconstruction.hlsli` - seeds reusable clip/view/world reconstruction helpers for later deferred-light work.
- `src/Oxygen/Vortex/Test/SceneRendererDeferredCore_test.cpp` - keeps the deferred-core proof target compiling against the Stage 9 deepening work.
- `src/Oxygen/Vortex/Test/SceneRendererPublication_test.cpp` - updates publication expectations to run under explicit deferred capability/mode.

## Decisions Made

- Sorted Stage 9 commands by material and geometry inside `BasePassMeshProcessor` so later draw submission can reuse a stable, module-owned ordering surface.
- Kept Stage 9 publication inside `BasePassModule` instead of restoring unconditional SceneRenderer-side promotion, while still allowing blank deferred attachments for valid deferred frames.
- Moved the Stage 9 shader glue into reusable Vortex includes so later deferred-light work can depend on shared helpers instead of duplicating stage-local logic.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Updated publication tests to request deferred Stage 9 capability explicitly**
- **Found during:** Task 1: Add the base-pass mesh processor and non-stub HLSL surface
- **Issue:** `SceneRendererPublication_test.cpp` expected SceneColor/GBuffer publication from renderer setups that were not enabling deferred shading, so the verification slice failed once Stage 9 stopped behaving like a pure shell.
- **Fix:** Switched the renderer-backed publication tests to explicit deferred capability/mode so they match the Phase 3 base-pass contract.
- **Files modified:** `src/Oxygen/Vortex/Test/SceneRendererPublication_test.cpp`
- **Verification:** `ctest --test-dir out/build-ninja -C Debug -R "^Oxygen\.Vortex\.(SceneRendererShell|SceneRendererPublication|SceneRendererDeferredCore)\.Tests$" --output-on-failure`
- **Committed in:** `15e4853d0`

---

**Total deviations:** 1 auto-fixed (1 blocking)
**Impact on plan:** Verification now matches the deferred-only Stage 9 contract. No product-scope creep.

## Issues Encountered

- The first `ctest` regex used the plan-era name pattern without the repo-generated `.Tests` suffix, so it matched zero tests. Rerunning with the exact generated test names produced the real verification result and is the command recorded below.

## User Setup Required

None - no external service configuration required.

## Verification Evidence

- Plan grep verification passed:
  `rg -n 'PreparedSceneFrame' src/Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassMeshProcessor.cpp`
  and
  `rg -n 'BasePassGBufferVS|BasePassGBufferPS' src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Stages/BasePass/BasePassGBuffer.hlsl`
- `cmake --build --preset windows-debug --target oxygen-vortex oxygen-graphics-direct3d12 Oxygen.Vortex.SceneRendererDeferredCore --parallel 4` passed.
- `ctest --test-dir out/build-ninja -C Debug -R "^Oxygen\.Vortex\.(SceneRendererShell|SceneRendererPublication|SceneRendererDeferredCore)\.Tests$" --output-on-failure` passed with `3/3` tests green.
- `powershell -ExecutionPolicy Bypass -File tools\cli\oxytidy.ps1 src\Oxygen\Vortex\SceneRenderer\SceneRenderer.cpp src\Oxygen\Vortex\SceneRenderer\Stages\BasePass\BasePassModule.cpp src\Oxygen\Vortex\SceneRenderer\Stages\BasePass\BasePassMeshProcessor.cpp src\Oxygen\Vortex\Test\SceneRendererDeferredCore_test.cpp src\Oxygen\Vortex\Test\SceneRendererPublication_test.cpp -Configuration Debug -IncludeTests -SummaryOnly` reported `0 warnings, 0 errors`.

## Next Phase Readiness

- Ready for `03-09-PLAN.md`.
- `DEFR-01` advanced: Stage 9 now has real mesh/shader processing surfaces, but Stage 10 publication timing proof still needs to land before the requirement can close.

## Self-Check

PASSED - `03-08-SUMMARY.md` exists, and task commit `15e4853d0` exists in history.

---
*Phase: 03-deferred-core*
*Completed: 2026-04-15*
