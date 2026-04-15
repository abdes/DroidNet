---
phase: 03-deferred-core
plan: "15"
subsystem: validation
tags: [vortex, deferred-core, validation, phase-closeout, scripts]
requires:
  - phase: 03-deferred-core/14
    provides: Automated Phase 3 proof coverage pinned to the current deferred-core tree
provides:
  - Single-command Phase 3 closeout runner for source/test/log-backed proof
  - Explicit Phase 04 deferral of runtime RenderDoc validation until Async and DemoShell migrate to Vortex
  - Ledger-backed proof-pack entry proving Stage 2/3/9/12 ordering, GBuffer contents, SceneColor accumulation, and stencil-bounded local lights
affects: [phase-04-migration, runtime-renderdoc-validation, deferred-core-ledger]
tech-stack:
  added: []
  patterns:
    - Phase closeout commands collapse into repo-owned one-line scripts instead of inline shell chains
    - Runtime RenderDoc claims are deferred until a truthful Vortex runtime surface exists
key-files:
  created:
    - tools/vortex/Run-DeferredCoreFrame10Capture.ps1
    - tools/vortex/AnalyzeDeferredCoreCapture.py
    - tools/vortex/Analyze-DeferredCoreCapture.ps1
    - tools/vortex/Assert-DeferredCoreCaptureReport.ps1
    - tools/vortex/Verify-DeferredCoreCloseout.ps1
  modified:
    - design/vortex/PLAN.md
    - design/vortex/IMPLEMENTATION-STATUS.md
    - .planning/workstreams/vortex/phases/03-deferred-core/03-15-PLAN.md
    - .planning/workstreams/vortex/phases/03-deferred-core/03-VALIDATION.md
key-decisions:
  - "Deferred runtime RenderDoc validation to Phase 04 so Phase 3 does not claim evidence from a non-Vortex runtime surface."
  - "Collapsed the validation surface into tools/vortex/Verify-DeferredCoreCloseout.ps1 so the operator command is one line and shell behavior stays inside repo-owned scripts."
patterns-established:
  - "Phase closeout scripts can own positive and negative-path proof internally, then expose a single documented command."
  - "Implementation-status proof packs can close a phase with source/test/log evidence while explicitly carrying a deferred runtime-capture requirement forward."
requirements-completed: [DEFR-02]
duration: 33 min
completed: 2026-04-15
---

# Phase 03 Plan 15: Deferred-Core Closeout Summary

**Phase 3 now closes through a single repo-owned deferred-core proof runner, while the first truthful runtime RenderDoc capture is explicitly deferred to Phase 4 Async and DemoShell migration**

## Performance

- **Duration:** 33 min
- **Started:** 2026-04-15T15:44:00+04:00
- **Completed:** 2026-04-15T16:17:02+04:00
- **Tasks:** 1
- **Files modified:** 9

## Accomplishments

- Added `tools/vortex/Verify-DeferredCoreCloseout.ps1` as the one-line Phase 3 closeout entrypoint that runs the proof collector, analyzer, positive assert, synthetic negative assert, and ledger checks.
- Added source/test/log-backed deferred-core analysis scripts that emit the required `stage_2_order`, `stage_3_order`, `stage_9_order`, `stage_12_order`, `gbuffer_contents`, `scene_color_lit`, and `stencil_local_lights` keys.
- Updated the Vortex ledger and plan docs so Phase 3 closes honestly, while runtime RenderDoc validation moves to Phase 4 when Async and DemoShell actually run on Vortex.

## Task Commits

1. **Task 1: Add the automated Phase 03 closeout gate and defer RenderDoc runtime proof to Phase 04**
   `f673b56ac` - `feat(03-15): close phase 3 with a single deferred-core proof runner`

## Files Created/Modified

- `tools/vortex/Run-DeferredCoreFrame10Capture.ps1` - gathers the build/test/tidy proof inputs and writes the closeout manifest.
- `tools/vortex/AnalyzeDeferredCoreCapture.py` - scores the current deferred-core tree into explicit pass/fail report keys.
- `tools/vortex/Analyze-DeferredCoreCapture.ps1` - thin PowerShell wrapper over the analyzer.
- `tools/vortex/Assert-DeferredCoreCaptureReport.ps1` - validates required keys and records either a proof pack or exact missing delta in the ledger.
- `tools/vortex/Verify-DeferredCoreCloseout.ps1` - single-command operator surface for the full closeout workflow.
- `design/vortex/PLAN.md` - moves Phase 3 from a runtime RenderDoc exit gate to source/test/log proof and carries runtime capture forward to Phase 4.
- `design/vortex/IMPLEMENTATION-STATUS.md` - records the Phase 3 proof pack and marks Phase 3 complete under the revised contract.
- `.planning/workstreams/vortex/phases/03-deferred-core/03-15-PLAN.md` - updates the micro-plan to the revised closeout contract and one-line verify command.
- `.planning/workstreams/vortex/phases/03-deferred-core/03-VALIDATION.md` - updates the Phase 3 validation map to the one-line closeout runner.

## Decisions Made

- Deferred runtime RenderDoc proof to Phase 4 so the ledger does not claim a live Vortex capture before Async and DemoShell migrate.
- Replaced the long inline verification chain with a repo-owned one-line script because the shell/native-command edge cases were obscuring validation instead of helping it.

## Deviations from Plan

- User-directed scope correction: runtime RenderDoc validation was deferred to Phase 4 migration, and the Phase 3 closeout gate was rebuilt around source/test/log proof.
- Impact on plan: Phase 3 still closes `DEFR-02` honestly, but the first runtime capture now belongs to the Phase 4 migration milestone instead of Phase 3.

## Issues Encountered

- The first collector implementation used `Start-Process` with redirected output and left a stranded runner process after the user-aborted verification attempt. The runner was rewritten to invoke commands directly and record their real exit codes.
- The analyzer initially failed on a UTF-8 BOM in the manifest written by PowerShell. The Python loader was updated to read `utf-8-sig`.
- The original inline negative-path verification relied on shell behavior that treated the intentional synthetic failure as a hard stop. The negative-path handling was moved into the one-line verifier so it can observe the expected failure internally.

## User Setup Required

None - no external service configuration required.

## Verification Evidence

- `powershell -NoProfile -File tools/vortex/Verify-DeferredCoreCloseout.ps1 -Output out/build-ninja/analysis/vortex/deferred-core/frame10`
  Passed.
- Internal results from that run:
  `cmake --build --preset windows-debug --target oxygen-vortex oxygen-graphics-direct3d12 --parallel 4` exited `0`.
  `ctest --test-dir out/build-ninja -C Debug --output-on-failure -R "^Oxygen\.Vortex\."` exited `0` with `31/31` Vortex tests passed.
  `tools/cli/oxytidy.ps1 ... -SummaryOnly` exited `0` and reported `2 warnings, 0 errors`; both warnings were in untouched `src/Oxygen/Vortex/SceneRenderer/Stages/InitViews/InitViewsModule.cpp`.
  `deferred-core-frame10.report.txt` reported all seven required keys as `pass`.
  The synthetic failing report was rejected as expected by `Assert-DeferredCoreCaptureReport.ps1`.

## Next Phase Readiness

- Phase 3 is closed under the revised validation contract.
- Phase 4 now owns the first truthful runtime RenderDoc capture, because Async and DemoShell migration is the point where Vortex has a real runtime surface to capture.

## Self-Check

PASSED - `03-15-SUMMARY.md` exists on disk, and task commit `f673b56ac` is
present in history.

---
*Phase: 03-deferred-core*
*Completed: 2026-04-15*
