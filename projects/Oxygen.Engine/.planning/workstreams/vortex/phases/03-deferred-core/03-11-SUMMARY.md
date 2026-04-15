---
phase: 03-deferred-core
plan: "11"
subsystem: scene-renderer
tags: [vortex, deferred-core, gbuffer, debug-views, shaders]
requires:
  - phase: 03-deferred-core/10
    provides: Stage 9 velocity completion plus the truthful Stage 10 publication boundary for deferred products
provides:
  - Vortex base-pass debug-view shader variants for BaseColor, WorldNormals, Roughness, and Metalness inspection
  - engine shader catalog registrations for the deferred-core GBuffer debug-view shader family
  - explicit SceneRenderer Stage 10 enforcement that keeps deferred-light and debug-view consumers behind published GBuffer bindings
affects: [phase-03-deferred-lighting, phase-03-proof-sweep]
tech-stack:
  added: []
  patterns:
    - Deferred debug variants consume published ViewFrameBindings and SceneTextureBindings instead of ad-hoc texture slots
    - Stage 10 remains the first valid GBuffer debug-input boundary for later deferred consumers
key-files:
  created:
    - src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Stages/BasePass/BasePassDebugView.hlsl
  modified:
    - src/Oxygen/Graphics/Direct3D12/Shaders/EngineShaderCatalog.h
    - src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp
    - src/Oxygen/Vortex/Test/SceneRendererDeferredCore_test.cpp
key-decisions:
  - "Reused the existing Vortex ShaderDebugMode define names for BaseColor, WorldNormals, Roughness, and Metalness instead of inventing a parallel debug vocabulary."
  - "SceneRenderer now treats Stage 10 publication as the explicit availability gate for GBuffer debug inspection and later deferred-light consumers."
patterns-established:
  - "Phase 3 debug-view proof stays inside SceneRendererDeferredCore_test rather than a separate ad-hoc harness."
  - "New Vortex shader variants register through per-mode RequiredDefineShaderFileSpec entries in EngineShaderCatalog."
requirements-completed: [DEFR-01]
duration: "8 min"
completed: 2026-04-15
---

# Phase 03 Plan 11: GBuffer Debug Visualization Summary

**Vortex now carries a dedicated BasePass debug-view shader family plus automated proof that published GBuffers can be inspected for BaseColor, WorldNormals, Roughness, and Metalness**

## Performance

- **Duration:** 8 min
- **Started:** 2026-04-15T14:24:55+04:00
- **Completed:** 2026-04-15T14:33:11+04:00
- **Tasks:** 1
- **Files modified:** 4

## Accomplishments

- Added `BasePassDebugView.hlsl` with four semantic Vortex GBuffer inspection variants.
- Registered the new debug-view shader family in `EngineShaderCatalog.h` so ShaderBake compiles it with the rest of the deferred-core shader tree.
- Locked the renderer-side availability seam to Stage 10 publication and proved the final tree with `GBufferDebugViewsAreAvailable`.

## TDD Cycle

### RED

- Added the failing deferred-core proof `GBufferDebugViewsAreAvailable`.
- Verified RED with the real test target:
  `cmake --build --preset windows-debug --target Oxygen.Vortex.SceneRendererDeferredCore.Tests --parallel 4`
  then
  `ctest --test-dir out/build-ninja -C Debug -R "^Oxygen\.Vortex\.SceneRendererDeferredCore\.Tests$" --output-on-failure`.
- Failure reason: `BasePassDebugView.hlsl` did not exist, and `EngineShaderCatalog.h` had no Vortex GBuffer debug-view registrations.

### GREEN

- Added `BasePassDebugView.hlsl` and registered the four required debug variants in `EngineShaderCatalog.h`.
- Tightened `SceneRenderer` so Stage 10 explicitly guarantees the published GBuffer inputs that deferred-light and debug-view consumers depend on.
- Re-ran the shader build, targeted gtest slice, plan `rg` verification, and changed-file `oxytidy` pass successfully.

### REFACTOR

- Folded the `oxytidy` feedback into the GREEN work by switching the new helper logic to `std::ranges::all_of`.
- No separate refactor commit was needed.

## Task Commits

1. **Task 1: Add GBuffer debug shader variants and proof**
   `b655687b3` - `test(03-11): guard the deferred-core milestone against losing GBuffer debug proof`
   `c3ade64dd` - `feat(03-11): restore the Phase 3 GBuffer debug-view surface`

## Files Created/Modified

- `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Stages/BasePass/BasePassDebugView.hlsl` - fullscreen Vortex debug-view shader that reads published GBuffers and visualizes BaseColor, WorldNormals, Roughness, or Metalness.
- `src/Oxygen/Graphics/Direct3D12/Shaders/EngineShaderCatalog.h` - registers the new Vortex debug-view VS and four required debug PS variants for ShaderBake.
- `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp` - keeps deferred-light/debug consumers behind published Stage 10 GBuffer bindings.
- `src/Oxygen/Vortex/Test/SceneRendererDeferredCore_test.cpp` - proves Stage 10 GBuffer publication plus final shader/catalog presence through `GBufferDebugViewsAreAvailable`.

## Decisions Made

- Reused the existing Vortex debug define names so the new shader family aligns with the renderer’s established debug-mode vocabulary.
- Kept the renderer-side availability change inside `SceneRenderer` rather than widening public APIs, because Stage 10 publication is already the authored Phase 3 truth boundary.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

- The initial RED proof assumed `OXYGEN_VORTEX_SOURCE_DIR` was available in this gtest target. The test was corrected to derive source-tree paths from `__FILE__` so the failure landed on the missing shader/catalog work instead of a harness macro gap.
- The repository hook enforces Conventional Commits in addition to the local Lore body/trailers, so the Lore commits were emitted with conventional `test(03-11):` / `feat(03-11):` intent lines.

## User Setup Required

None - no external service configuration required.

## Verification Evidence

- `cmake --build --preset windows-debug --target Oxygen.Vortex.SceneRendererDeferredCore.Tests --parallel 4` passed in RED, confirming the new proof compiled before execution.
- `ctest --test-dir out/build-ninja -C Debug -R "^Oxygen\.Vortex\.SceneRendererDeferredCore\.Tests$" --output-on-failure` failed in RED with the missing `BasePassDebugView.hlsl` file and missing catalog-token assertions, then passed in GREEN with `1/1` tests green.
- `cmake --build --preset windows-debug --target oxygen-graphics-direct3d12 Oxygen.Vortex.SceneRendererDeferredCore.Tests --parallel 4` passed; ShaderBake compiled the new `BasePassDebugViewVS` plus four required debug PS variants and repacked `bin/Oxygen/Debug/dev/shaders.bin` to 188 modules.
- `rg -n 'GBufferDebugViewsAreAvailable' src/Oxygen/Vortex/Test/SceneRendererDeferredCore_test.cpp` passed.
- `rg -n 'BasePassDebugView|GBufferDebug' src/Oxygen/Graphics/Direct3D12/Shaders/EngineShaderCatalog.h src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Stages/BasePass/BasePassDebugView.hlsl` passed.
- `powershell -ExecutionPolicy Bypass -File tools\cli\oxytidy.ps1 src\Oxygen\Vortex\SceneRenderer\SceneRenderer.cpp src\Oxygen\Vortex\Test\SceneRendererDeferredCore_test.cpp -Configuration Debug -IncludeTests -SummaryOnly` passed with `0 warnings, 0 errors`.

## Next Phase Readiness

- Ready for `03-12-PLAN.md`.
- Phase 3 once again has an explicit GBuffer debug-view surface and a test that prevents the shader/catalog path from being dropped by later replanning.

## Self-Check

PASSED - `03-11-SUMMARY.md` exists on disk, and task commits `b655687b3` and
`c3ade64dd` are present in history.

---
*Phase: 03-deferred-core*
*Completed: 2026-04-15*
