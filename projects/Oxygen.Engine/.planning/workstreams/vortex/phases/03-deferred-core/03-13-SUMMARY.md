---
phase: 03-deferred-core
plan: "13"
subsystem: scene-renderer
tags: [vortex, deferred-core, lighting, scene-renderer, tests]
requires:
  - phase: 03-deferred-core/12
    provides: Lighting-domain deferred-light shaders and the truthful Stage 10 deferred-input boundary
provides:
  - Inline Stage 12 CPU deferred-light telemetry for published binding consumption and light-type routing
  - Deferred-core proof coverage for GBuffer consumption, SceneColor accumulation, and stencil-bounded local-light execution
  - Visible scene-light traversal that distinguishes directional lights from point/spot local-light work
affects: [phase-03-proof-sweep, phase-04-lighting-service]
tech-stack:
  added: []
  patterns:
    - SceneRenderer Stage 12 proves CPU orchestration through explicit telemetry before GPU lighting output is claimed
    - Direct SceneRenderer tests seed the existing PublishViewFrameBindings seam after OnFrameStart so Stage 12 runs against published routing metadata
key-files:
  created: []
  modified:
    - src/Oxygen/Vortex/SceneRenderer/SceneRenderer.h
    - src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp
    - src/Oxygen/Vortex/Test/SceneRendererDeferredCore_test.cpp
key-decisions:
  - "Used explicit Stage 12 telemetry in SceneRenderer to prove CPU deferred-light orchestration without overstating GPU lighting completeness."
  - "Seeded published view-frame bindings through the existing PublishViewFrameBindings seam in the direct deferred-core unit fixture instead of adding a runtime fallback path."
patterns-established:
  - "Stage 12 CPU proofs lock on published binding slots plus directional/local-light counters before LightingService owns the pass."
  - "Visible scene-light traversal in SceneRenderer distinguishes fullscreen directional work from stencil-bounded local-light work."
requirements-completed: [DEFR-02]
duration: 6 min
completed: 2026-04-15
---

# Phase 03 Plan 13: Deferred-Light CPU Path Summary

**Inline Stage 12 deferred-light CPU orchestration now consumes published GBuffer/view bindings, targets SceneColor, and proves stencil-bounded local-light routing**

## Performance

- **Duration:** 6 min
- **Started:** 2026-04-15T15:04:58+04:00
- **Completed:** 2026-04-15T15:11:04+04:00
- **Tasks:** 1
- **Files modified:** 3

## Accomplishments

- Replaced the Stage 12 stub with CPU-side deferred-light orchestration that validates deferred mode, published view binding slots, and published scene-texture inputs before scanning visible scene lights.
- Added `SceneRenderer::DeferredLightingState` so the deferred-core proof surface can assert published GBuffer consumption, SceneColor targeting, and directional versus local-light routing without claiming GPU lighting output that does not exist yet.
- Added the required automated proofs: `DeferredLightingConsumesPublishedGBuffers`, `DeferredLightingAccumulatesIntoSceneColor`, and `DeferredLightingUsesStencilBoundedLocalLights`.

## TDD Cycle

### RED

- Added the three Stage 12 deferred-light proofs to `SceneRendererDeferredCore_test.cpp`.
- Verified RED with `cmake --build --preset windows-debug --target Oxygen.Vortex.SceneRendererDeferredCore.Tests --parallel 4`.
- Failure reason: `SceneRenderer` had no `GetLastDeferredLightingState()` seam and `RenderDeferredLighting(...)` still behaved like a stub.

### GREEN

- Added Stage 12 deferred-light telemetry plus visible scene-light traversal in `SceneRenderer`.
- Seeded the existing `PublishViewFrameBindings()` seam in the direct deferred-core helper after `OnFrameStart()` so Stage 12 runs against published routing metadata in this unit test path.
- Re-ran the targeted build/test slice, then the plan’s exact verification chain successfully.

### REFACTOR

- No separate refactor commit was needed.

## Task Commits

1. **Task 1: Wire the Stage 12 CPU path and prove local-light stencil behavior**
   `74d6355a2` - `test(03-13): make Stage 12 deferred-light orchestration observable`
   `e8514e8c5` - `feat(03-13): route Stage 12 through published deferred-light inputs`

## Files Created/Modified

- `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.h` - adds `DeferredLightingState` plus the public getter used by the deferred-core proof surface.
- `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp` - implements Stage 12 CPU deferred-light telemetry, published-input validation, and visible scene-light traversal for directional/point/spot routing.
- `src/Oxygen/Vortex/Test/SceneRendererDeferredCore_test.cpp` - adds the three required Stage 12 proofs and seeds the existing published-binding seam for the direct SceneRenderer unit path.

## Decisions Made

- Kept Stage 12 ownership inline in `SceneRenderer` and exposed CPU telemetry instead of pretending the new deferred-light shader family already proves runtime lighting output.
- Reused `PublishViewFrameBindings()` in the direct test helper rather than teaching `SceneRenderer` a second, test-only binding synthesis path.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Added the concrete scene traversal include required by the new Stage 12 light scan**
- **Found during:** Task 1 (Wire the Stage 12 CPU path and prove local-light stencil behavior)
- **Issue:** `Scene.h` only forward-declared `SceneTraversal`, so the new inline visitor code in `SceneRenderer.cpp` failed to compile.
- **Fix:** Included `Oxygen/Scene/SceneTraversal.h` alongside the traversal/light headers in `SceneRenderer.cpp`.
- **Files modified:** `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp`
- **Verification:** `cmake --build --preset windows-debug --target Oxygen.Vortex.SceneRendererDeferredCore.Tests --parallel 4` passed after the include was added.
- **Committed in:** `e8514e8c5`

**2. [Rule 3 - Blocking] Seeded published view bindings in the direct deferred-core fixture**
- **Found during:** Task 1 (Wire the Stage 12 CPU path and prove local-light stencil behavior)
- **Issue:** `SceneRendererDeferredCore_test.cpp` drives `SceneRenderer` directly, so the renderer-owned publication step never populated `ViewFrameBindings`; Stage 12 correctly refused to run.
- **Fix:** Published a synthetic non-invalid `ViewFrameBindings` snapshot through the existing `PublishViewFrameBindings()` seam immediately after `OnFrameStart()` in the fixture helper.
- **Files modified:** `src/Oxygen/Vortex/Test/SceneRendererDeferredCore_test.cpp`
- **Verification:** `ctest --test-dir out/build-ninja -C Debug -R 'Oxygen\.Vortex\.SceneRendererDeferredCore' --output-on-failure` passed with all 15 deferred-core tests green.
- **Committed in:** `e8514e8c5`

---

**Total deviations:** 2 auto-fixed (2 blocking)
**Impact on plan:** Both fixes stayed inside the authored Stage 12 proof surface and were required to make the new deferred-light contract compile and execute honestly. No scope creep.

## Issues Encountered

- A stale Git index lock briefly blocked the RED commit after an earlier parallel `git add`/`git commit` attempt; rerunning the commit steps serially resolved it.
- The repository’s commit hook requires Conventional Commit subjects in addition to the repo’s Lore trailers, so the TDD commits used `test(03-13): ...` and `feat(03-13): ...` subjects with Lore bodies.

## User Setup Required

None - no external service configuration required.

## Verification Evidence

- `cmake --build --preset windows-debug --target Oxygen.Vortex.SceneRendererDeferredCore.Tests --parallel 4`
  RED: failed before implementation because `GetLastDeferredLightingState()` did not exist.
  GREEN: passed after the Stage 12 implementation landed.
- `ctest --test-dir out/build-ninja -C Debug -R 'Oxygen\.Vortex\.SceneRendererDeferredCore' --output-on-failure`
  Passed: `1/1` test executable green, `15` individual tests passed.
- Plan verification command passed exactly as written:
  `rg -n 'RenderDeferredLighting\(' ...`
  `rg -n 'DeferredLightingConsumesPublishedGBuffers|DeferredLightingAccumulatesIntoSceneColor|DeferredLightingUsesStencilBoundedLocalLights' ...`
  `cmake --build --preset windows-debug --target oxygen-vortex oxygen-graphics-direct3d12 --parallel 4`
  `ctest --test-dir out/build-ninja -C Debug -R 'Oxygen\.Vortex\.SceneRendererDeferredCore' --output-on-failure`
- `powershell -ExecutionPolicy Bypass -File tools\cli\oxytidy.ps1 src\Oxygen\Vortex\SceneRenderer\SceneRenderer.h src\Oxygen\Vortex\SceneRenderer\SceneRenderer.cpp src\Oxygen\Vortex\Test\SceneRendererDeferredCore_test.cpp -Configuration Debug -IncludeTests -SummaryOnly`
  Passed with `0 warnings, 0 errors`.

## Next Phase Readiness

- Ready for `03-14-PLAN.md`.
- Stage 12 now exposes proofable published-input consumption and directional/local-light routing, so the next plan can widen the automated Phase 3 sweep without reopening deferred-light ownership.

## Self-Check

PASSED - `03-13-SUMMARY.md` exists on disk, and task commits `74d6355a2` and
`e8514e8c5` are present in history.

---
*Phase: 03-deferred-core*
*Completed: 2026-04-15*
