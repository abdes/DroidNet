---
phase: 01
slug: substrate-migration
status: ready
nyquist_compliant: true
wave_0_complete: true
created: 2026-04-13
---

# Phase 01 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

> **Planning repair (2026-04-13):** the Phase 1 micro-plan set was repaired
> around the confirmed `01-04` and `01-08` blockers. `01-04` lands the
> prerequisite ABI bundle, `01-05` owns `Resources/*`, `01-08` now lands only
> the public step-`1.7` headers, and `01-10` now combines the step-`1.6`
> pass-base migration with the later-wave root contracts plus the final
> post-orchestrator `FOUND-03` dependency-edge proof.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | CMake + CTest + targeted shell verification |
| **Config file** | `CMakePresets.json`, `tools/presets/WindowsPresets.json` |
| **Quick run command** | `cmake --build --preset windows-debug --target oxygen-vortex --parallel 4` |
| **Full suite command** | `cmake --build --preset windows-debug --target oxygen-vortex Oxygen.Vortex.LinkTest Oxygen.Vortex.RendererCapability.Tests Oxygen.Vortex.RendererCompositionQueue.Tests --parallel 4 && ctest --test-dir out/build-ninja -C Debug -R "^Oxygen\\.Vortex\\." --output-on-failure` |
| **Estimated runtime** | ~120 seconds |

---

## Sampling Rate

- **After every task commit:** Run `cmake --build --preset windows-debug --target oxygen-vortex --parallel 4`
- **After every plan wave:** Run `cmake --build --preset windows-debug --target oxygen-vortex Oxygen.Vortex.LinkTest Oxygen.Vortex.RendererCapability.Tests Oxygen.Vortex.RendererCompositionQueue.Tests --parallel 4 && ctest --test-dir out/build-ninja -C Debug -R "^Oxygen\\.Vortex\\." --output-on-failure`
- **Before `/gsd-verify-work`:** Full suite must be green
- **Max feedback latency:** 120 seconds

---

## Planning Repair Summary

- `ROADMAP.md` and the Phase 1 plan set now agree on the step breakdown from
  the current completed state (`01-01` through `01-07` are already done).
- `01-04-PLAN.md` lands only the prerequisite ABI bundle required by the
  resources slice.
- `01-05-PLAN.md` migrates `Resources/*` and closes step `1.3`.
- `01-06-PLAN.md` covers the remaining ScenePrep-only data/config files.
- `01-07-PLAN.md` covers ScenePrep execution plus the selected substrate-only
  internal-utility slice.
- `01-08-PLAN.md` now lands only the public step-`1.7` headers and keeps step
  `1.6` deferred until the later root-contract wave.
- `01-10-PLAN.md` now combines the step-`1.6` pass bases with the remaining
  root-support files, stripped orchestrator, and final linked-artifact
  dependency-edge proof for `FOUND-03`.

Superseding update (2026-04-13):

- the historical `01-08` through `01-13` repair path remains accurate as a
  record of how the original seam and hermeticity gaps were closed
- a later comprehensive architecture/LLD compliance review reopened Phase 1
  after finding unresolved renderer-core and ABI/dependency-boundary
  violations
- `01-14-PLAN.md` is now the active remediation plan
- no Phase 2 work may begin until `01-14` passes and Phase 1 is re-verified

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 01-01-01 | 01 | 1 | FOUND-02 | T-01-01 / — | Vortex-owned type migration preserves buildability without legacy include/export leakage | build + grep | `cmake --build --preset windows-debug --target oxygen-vortex --parallel 4` | ✅ | ⬜ pending |
| 01-02-01 | 02 | 2 | FOUND-02 | T-01-01 / — | Upload/staging migration remains mechanically isolated and buildable | build + grep | `cmake --build --preset windows-debug --target oxygen-vortex --parallel 4` | ✅ | ⬜ pending |
| 01-03-01 | 03 | 3 | FOUND-02 | T-01-01 / — | Upload completion stays in Vortex ownership with no legacy module seam | build + grep | `cmake --build --preset windows-debug --target oxygen-vortex --parallel 4` | ✅ | ⬜ pending |
| 01-04-01 | 04 | 4 | FOUND-02 | T-01-01 / — | Prerequisite ABI bundle lands before resources and preserves buildability without wrapper seams | build + grep | `cmake --build --preset windows-debug --target oxygen-vortex --parallel 4` | ✅ | ⬜ pending |
| 01-05-01 | 05 | 5 | FOUND-02 | T-01-01 / — | Resource binder/uploader migration consumes the Vortex-local ABI bundle and closes step 1.3 | build + grep | `cmake --build --preset windows-debug --target oxygen-vortex --parallel 4` | ✅ | ⬜ pending |
| 01-06-01 | 06 | 6 | FOUND-02 | T-01-02 / — | Remaining ScenePrep-only data/config migration remains substrate-only | build + grep | `cmake --build --preset windows-debug --target oxygen-vortex --parallel 4` | ✅ | ⬜ pending |
| 01-07-01 | 07 | 7 | FOUND-02 | T-01-02 / — | ScenePrep execution plus selected internals compile in Vortex | build + grep | `cmake --build --preset windows-debug --target oxygen-vortex --parallel 4` | ✅ | ⬜ pending |
| 01-08-01 | 08 | 8 | FOUND-02 | T-01-03 / — | Public step-1.7 headers move into Vortex-owned placements while step 1.6 remains deferred until `01-10` | build + grep | `cmake --build --preset windows-debug --target oxygen-vortex --parallel 4` | ✅ | ⬜ pending |
| 01-09-01 | 09 | 9 | FOUND-02 | T-01-04 / — | Composition infrastructure rehome removes `Renderer/Pipeline/` dependency | build + grep | `cmake --build --preset windows-debug --target oxygen-vortex --parallel 4` | ✅ | ⬜ pending |
| 01-10-01 | 10 | 10 | FOUND-02, FOUND-03 | T-01-05 / — | Step 1.6 pass bases land only after the Vortex-owned root contracts exist, and the final root/orchestrator slice still leaves the linked Vortex artifact free of any `Oxygen.Renderer` dependency edge | build + grep + target-query | `cmake --build --preset windows-debug --target oxygen-vortex --parallel 4 && cmake --preset windows-default && powershell -NoProfile -Command \"$ninja = (Select-String -Path 'out/build-ninja/CMakeCache.txt' -Pattern '^CMAKE_MAKE_PROGRAM:FILEPATH=(.+)$').Matches[0].Groups[1].Value; $query = & $ninja -C out/build-ninja -f build-Debug.ninja -t query bin/Debug/Oxygen.Vortex-d.dll 2>&1; if ($query -match 'oxygen-renderer|Oxygen\\.Renderer') { Write-Error 'oxygen-vortex target still depends on Oxygen.Renderer'; exit 1 }\"` | ✅ | ⬜ pending |
| 01-11-01 | 11 | 11 | FOUND-02 | T-01-07 / — | Vortex smoke path constructs renderer and exercises frame hooks | integration | `cmake --build --preset windows-debug --target oxygen-vortex Oxygen.Vortex.LinkTest --parallel 4 && ctest --test-dir out/build-ninja -C Debug -R "^Oxygen\\.Vortex\\.LinkTest$" --output-on-failure` | ✅ | ⬜ pending |
| 01-11-02 | 11 | 11 | FOUND-03 | T-01-07 / — | Legacy renderer substrate regressions remain green after the final post-orchestrator dependency-edge proof has already passed in `01-10` | regression | `cmake --build --preset windows-debug --target Oxygen.Renderer.LinkTest --parallel 4 && ctest --test-dir out/build-ninja -C Debug -R "^Oxygen\\.Renderer\\.LinkTest$" --output-on-failure` | ✅ | ⬜ pending |
| 01-14-01 | 14 | 14 | FOUND-02 | T-01-08 / — | Renderer Core preserves composition planning, queueing, target resolution, and compositing execution instead of silently dropping queued work | build + Vortex regression | `cmake --build --preset windows-debug --target oxygen-vortex --parallel 4 && ctest --test-dir out/build-ninja -C Debug -R '^Oxygen\\.Vortex\\.(LinkTest|.*Composition.*)$' --output-on-failure` | ✅ | ✅ passed |
| 01-14-02 | 14 | 14 | FOUND-02, FOUND-03 | T-01-09 / — | Phase 1 capability/API hygiene remains truthful: `kDeferredShading` exists, default capability reporting is substrate-only, no public `Internal/` include leak remains, no Phase-1-illegal public planning contracts remain, and `oxygen-vortex` has no `Oxygen.ImGui` dependency edge | build + grep + target-query | `powershell -NoProfile -Command \"rg -n 'kDeferredShading' src/Oxygen/Vortex/RendererCapability.h | Out-Null; $bad = rg -n 'Internal/RenderContextPool.h' src/Oxygen/Vortex/Renderer.h; if ($LASTEXITCODE -ne 1) { Write-Error 'Renderer.h still includes Internal/RenderContextPool.h'; exit 1 }; $bad = rg -n 'SceneRenderer/ShaderDebugMode.h|SceneRenderer/ShaderPassConfig.h|SceneRenderer/ToneMapPassConfig.h' src/Oxygen/Vortex/CMakeLists.txt; if ($LASTEXITCODE -ne 1) { Write-Error 'Phase-1-illegal public SceneRenderer contracts still exported'; exit 1 }; $bad = rg -n 'oxygen::imgui' src/Oxygen/Vortex/CMakeLists.txt; if ($LASTEXITCODE -ne 1) { Write-Error 'oxygen-vortex still links oxygen::imgui'; exit 1 }; cmake --build --preset windows-debug --target oxygen-vortex --parallel 4; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }; $ninja = (Select-String -Path 'out/build-ninja/CMakeCache.txt' -Pattern '^CMAKE_MAKE_PROGRAM:FILEPATH=(.+)$').Matches[0].Groups[1].Value; $query = & $ninja -C out/build-ninja -f build-Debug.ninja -t query bin/Debug/Oxygen.Vortex-d.dll 2>&1; if ($query -match 'oxygen-renderer|Oxygen\\.Renderer|Oxygen\\.ImGui|oxygen-imgui') { Write-Error 'oxygen-vortex target still depends on a forbidden module edge'; exit 1 }\"` | ✅ | ✅ passed |
| 01-14-03 | 14 | 14 | FOUND-02, FOUND-03 | T-01-10 / — | The reopened proof suite is stronger than the original sign-off: Vortex-side regressions, hermeticity smoke, and final target-edge proof all pass together before the phase is re-closed | full suite | `powershell -NoProfile -Command \"rg -n '^#include <Oxygen/Renderer/|Oxygen/Renderer/' src/Oxygen/Vortex; if ($LASTEXITCODE -ne 1) { Write-Error 'Vortex still contains a legacy renderer seam'; exit 1 }; cmake --build --preset windows-debug --target oxygen-vortex Oxygen.Vortex.LinkTest Oxygen.Vortex.RendererCapability.Tests Oxygen.Vortex.RendererCompositionQueue.Tests --parallel 4; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }; ctest --test-dir out/build-ninja -C Debug -R '^Oxygen\\.Vortex\\.' --output-on-failure; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }; $ninja = (Select-String -Path 'out/build-ninja/CMakeCache.txt' -Pattern '^CMAKE_MAKE_PROGRAM:FILEPATH=(.+)$').Matches[0].Groups[1].Value; $query = & $ninja -C out/build-ninja -f build-Debug.ninja -t query bin/Debug/Oxygen.Vortex-d.dll 2>&1; if ($query -match 'oxygen-renderer|Oxygen\\.Renderer|Oxygen\\.ImGui|oxygen-imgui') { Write-Error 'oxygen-vortex target still depends on a forbidden module edge'; exit 1 }\"` | ✅ | ✅ passed |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

Existing infrastructure covers all phase requirements.

---

## Manual-Only Verifications

All phase behaviors have automated verification.

---

## Validation Sign-Off

- [x] All tasks have `<automated>` verify or Wave 0 dependencies
- [x] Sampling continuity: no 3 consecutive tasks without automated verify
- [x] Wave 0 covers all MISSING references
- [x] No watch-mode flags
- [x] Feedback latency < 120s
- [x] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
