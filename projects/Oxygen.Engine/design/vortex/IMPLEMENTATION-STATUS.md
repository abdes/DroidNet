# Vortex Renderer Implementation Status

Status: `in_progress — Phase 1 step 1.1 is complete; 01-03 starts step 1.2 upload migration`

This document is the **running resumability ledger** for the Vortex renderer.
It records what is actually in the repo, what has been verified, what is still
missing, and exactly where to resume. All claims must be evidence-backed.

Related:

- [PRD.md](./PRD.md) — stable product requirements
- [ARCHITECTURE.md](./ARCHITECTURE.md) — stable architecture
- [DESIGN.md](./DESIGN.md) — evolving LLD (early draft)
- [PROJECT-LAYOUT.md](./PROJECT-LAYOUT.md) — authoritative file layout
- [PLAN.md](./PLAN.md) — active execution plan

## Ledger Rules

1. **Evidence, not intention.** Every entry records what exists in code and
   what validation was run, not what was planned or discussed.
2. **No-false-completion.** A phase is `done` only when: code exists,
   required docs are updated, and validation evidence is recorded here.
3. **Missing-delta explicit.** If build or tests were not run, the phase
   stays `in_progress` with the missing validation delta listed.
4. **Scope-drift trigger.** If scope changes or the current design is found
   incomplete, update design docs before claiming further progress.
5. **Per-session update.** Each implementation session must update this file
   with: changed files, commands run, results, and remaining blockers.

## Phase Summary

| Phase | Name | Status | Blocker |
| ----- | ---- | ------ | ------- |
| 0 | Scaffold and Build Integration | `done` | — |
| 1 | Substrate Migration | `in_progress` | Step 1.2 upload migration is next (`01-03`) |
| 2 | SceneTextures + SceneRenderer Shell | `not_started` | Phase 1 + design deliverables |
| 3 | Deferred Core | `not_started` | Phase 2 + 5 LLD documents |
| 4 | Migration-Critical Services + First Migration | `not_started` | Phase 3 + per-service LLDs |
| 5 | Remaining Services + Runtime Scenarios | `not_started` | Phase 4 + per-service/scenario LLDs |
| 6 | Legacy Deprecation | `not_started` | Phase 5 |
| 7 | Future Capabilities (post-release) | `not_started` | Phase 6 |

## Design Deliverable Tracker

Each design deliverable required by PLAN.md is tracked here. A phase's
implementation cannot begin until its design prerequisites are met.

| ID | Deliverable | Required By | Status | Location |
| -- | ----------- | ----------- | ------ | -------- |
| D.1 | SceneTextures four-part contract | Phase 2 | `not_started` | TBD (DESIGN.md update or standalone LLD) |
| D.2 | SceneRenderBuilder bootstrap | Phase 2 | `not_started` | TBD |
| D.3 | SceneRenderer shell dispatch | Phase 2 | `not_started` | TBD |
| D.4 | Depth prepass LLD | Phase 3 | `not_started` | `design/vortex/lld/depth-prepass.md` |
| D.5 | Base pass LLD | Phase 3 | `not_started` | `design/vortex/lld/base-pass.md` |
| D.6 | Deferred lighting LLD | Phase 3 | `not_started` | `design/vortex/lld/deferred-lighting.md` |
| D.7 | Shader contracts LLD | Phase 3 | `not_started` | `design/vortex/lld/shader-contracts.md` |
| D.8 | InitViews LLD | Phase 3 | `not_started` | `design/vortex/lld/init-views.md` |
| D.9 | LightingService LLD | Phase 4A | `not_started` | `design/vortex/lld/lighting-service.md` |
| D.10 | PostProcessService LLD | Phase 4B | `not_started` | `design/vortex/lld/post-process-service.md` |
| D.11 | ShadowService LLD | Phase 4C | `not_started` | `design/vortex/lld/shadow-service.md` |
| D.12 | EnvironmentLightingService LLD | Phase 4D | `not_started` | `design/vortex/lld/environment-service.md` |
| D.13 | Migration playbook | Phase 4E | `not_started` | `design/vortex/lld/migration-playbook.md` |
| D.14 | DiagnosticsService LLD | Phase 5A | `not_started` | `design/vortex/lld/diagnostics-service.md` |
| D.15 | TranslucencyModule LLD | Phase 5B | `not_started` | `design/vortex/lld/translucency.md` |
| D.16 | OcclusionModule LLD | Phase 5C | `not_started` | `design/vortex/lld/occlusion.md` |
| D.17 | Multi-view composition LLD | Phase 5D | `not_started` | `design/vortex/lld/multi-view-composition.md` |
| D.18 | Offscreen rendering LLD | Phase 5E | `not_started` | `design/vortex/lld/offscreen-rendering.md` |

---

## Documentation Sync Log

### 2026-04-13 — Phase 1 plan 01-02 completed the remaining step-1.1 type migration

- Changed files this session:
  - `src/Oxygen/Vortex/CMakeLists.txt`
  - `src/Oxygen/Vortex/Types/EnvironmentViewData.h`
  - `src/Oxygen/Vortex/Types/ViewColorData.h`
  - `src/Oxygen/Vortex/Types/ViewConstants.cpp`
  - `src/Oxygen/Vortex/Types/ViewConstants.h`
  - `src/Oxygen/Vortex/Types/ViewFrameBindings.h`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - `rg -n 'Oxygen/Renderer/|OXGN_RNDR_' src/Oxygen/Vortex/Types/EnvironmentViewData.h src/Oxygen/Vortex/Types/LightCullingConfig.h src/Oxygen/Vortex/Types/SyntheticSunData.h src/Oxygen/Vortex/Types/ViewColorData.h src/Oxygen/Vortex/Types/ViewConstants.cpp src/Oxygen/Vortex/Types/ViewConstants.h src/Oxygen/Vortex/Types/ViewFrameBindings.h`
  - `cmake --build --preset windows-debug --target oxygen-vortex --parallel 4`
  - `cmake --preset windows-default`
  - `D:/dev/ninja/ninja.exe -C out/build-ninja -f build-Debug.ninja -t query oxygen-vortex`
- Result:
  - the remaining step-`1.1` files now live under `src/Oxygen/Vortex/Types/`:
    `EnvironmentViewData.h`, `ViewColorData.h`, `ViewConstants.h`,
    `ViewConstants.cpp`, and `ViewFrameBindings.h`
  - `ViewConstants.cpp` is now part of the `oxygen-vortex` private source list
  - `oxygen-vortex` builds successfully in Debug after the remaining type
    migration
  - the generated Debug Ninja target query for `oxygen-vortex` shows no
    `oxygen-renderer` / `Oxygen.Renderer` dependency edge
- Code / validation delta:
  - step `1.1` is now **complete**
  - no Vortex/legacy link-test or runtime validation was run in this plan
- Remaining blocker:
  - begin step `1.2` with `01-03` to migrate the upload foundation and staging
    slice

### 2026-04-13 — Phase 1 plan 01-01 started with the step-1.1 frame-binding slice

- Changed files this session:
  - `src/Oxygen/Vortex/CMakeLists.txt`
  - `src/Oxygen/Vortex/Types/CompositingTask.h`
  - `src/Oxygen/Vortex/Types/DebugFrameBindings.h`
  - `src/Oxygen/Vortex/Types/DrawFrameBindings.h`
  - `src/Oxygen/Vortex/Types/EnvironmentFrameBindings.h`
  - `src/Oxygen/Vortex/Types/LightCullingConfig.h`
  - `src/Oxygen/Vortex/Types/LightingFrameBindings.h`
  - `src/Oxygen/Vortex/Types/ShadowFrameBindings.h`
  - `src/Oxygen/Vortex/Types/SyntheticSunData.h`
  - `src/Oxygen/Vortex/Types/VsmFrameBindings.h`
- Commands used for verification:
  - `rg -n 'Oxygen/Renderer/|OXGN_RNDR_' src/Oxygen/Vortex/Types/CompositingTask.h src/Oxygen/Vortex/Types/DebugFrameBindings.h src/Oxygen/Vortex/Types/DrawFrameBindings.h src/Oxygen/Vortex/Types/EnvironmentFrameBindings.h src/Oxygen/Vortex/Types/LightingFrameBindings.h src/Oxygen/Vortex/Types/ShadowFrameBindings.h src/Oxygen/Vortex/Types/VsmFrameBindings.h`
  - `cmake --build --preset windows-debug --target oxygen-vortex --parallel 4`
  - `cmake --preset windows-default`
  - `rg -n "oxygen-renderer|Oxygen\\.Renderer" out/build-ninja/src/Oxygen/Vortex/CMakeFiles/Export out/build-ninja/src/Oxygen/Vortex/CMakeFiles/oxygen-vortex.dir/Debug/CXXDependInfo.json`
- Result:
  - the first frame-binding half of step `1.1` now lives under
    `src/Oxygen/Vortex/Types/`
  - `LightingFrameBindings.h` now uses Vortex-local
    `LightCullingConfig.h` and `SyntheticSunData.h` so the migrated slice
    carries no `Oxygen/Renderer/` include seam
  - `oxygen-vortex` builds successfully in Debug after the type migration
  - the generated Vortex export/depend info shows no `oxygen-renderer` /
    `Oxygen.Renderer` reference
- Code / validation delta:
  - step `1.1` is **still in progress**; the remaining type headers are
    deferred to `01-02`
  - no broader link-test or runtime validation was run in this plan
- Remaining blocker:
  - execute `01-02` to land the remaining step-1.1 type headers before the
    rest of Phase 1 continues

### 2026-04-13 — Phase 1 plan set repaired

- Changed files this session:
  - `.planning/workstreams/vortex/phases/01-substrate-migration/*-PLAN.md`
  - `.planning/workstreams/vortex/ROADMAP.md`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-VALIDATION.md`
  - `.planning/workstreams/vortex/STATE.md`
  - `design/vortex/IMPLEMENTATION-STATUS.md`
- Commands used for verification:
  - structural inspection of all Phase 1 plan files
  - per-plan checks for `wave`, `depends_on`, `requirements`, task count,
    `<read_first>`, and `<acceptance_criteria>`
- Result:
  - the full 11-plan Phase 1 set now matches roadmap steps `1.1` through `1.9`
  - `01-04` through `01-06` now cover resources, ScenePrep, and selected
    internals instead of continuing upload work
  - `01-08` now closes only step `1.7`
  - `01-09` and `01-10` keep step `1.8` split between support-file migration
    and orchestrator migration
- Code / validation delta:
  - no implementation code changed
  - no build or tests were run during plan repair
  - Phase 1 is planned and ready to execute, not complete
- Remaining blocker:
  - execute the repaired Phase 1 plan set and collect build/test evidence

### 2026-04-13 — Phase 1 execute-phase blocker recorded

- Changed files this session:
  - `design/vortex/IMPLEMENTATION-STATUS.md`
  - `.planning/workstreams/vortex/ROADMAP.md`
  - `.planning/workstreams/vortex/STATE.md`
  - `.planning/workstreams/vortex/phases/01-substrate-migration/01-VALIDATION.md`
- Commands used for verification:
  - `node "$HOME/.codex/get-shit-done/bin/gsd-tools.cjs" init execute-phase 1 --ws vortex`
  - `node "$HOME/.codex/get-shit-done/bin/gsd-tools.cjs" phase-plan-index 1 --ws vortex`
  - `Get-Content .planning/workstreams/vortex/phases/01-substrate-migration/01-04-PLAN.md`
  - `Get-Content .planning/workstreams/vortex/phases/01-substrate-migration/01-05-PLAN.md`
  - `Get-Content .planning/workstreams/vortex/phases/01-substrate-migration/01-06-PLAN.md`
  - `Get-Content .planning/workstreams/vortex/phases/01-substrate-migration/01-08-PLAN.md`
  - `Get-Content .planning/workstreams/vortex/ROADMAP.md`
  - `Get-Content .planning/workstreams/vortex/phases/01-substrate-migration/01-VALIDATION.md`
- Result:
  - execute-phase preflight found scope drift between the intended Phase 1 scope and the generated `.planning` micro-plan set
  - `ROADMAP.md` and `01-VALIDATION.md` still require resources, ScenePrep, and internal-utility migration work, but the current `01-04` through `01-06` plan files instead continue the upload migration
  - `01-08-PLAN.md` attempts to record steps `1.4` through `1.7` as complete even though the missing resources / ScenePrep / internal-utility slices are not planned anywhere in the current phase directory
- Code / validation delta:
  - no implementation code changed
  - no build or tests were run because execution was blocked before a trustworthy task boundary existed
  - Phase 1 remains incomplete and must not be reported as having started beyond preflight discovery
- Ledger impact:
  - Phase 1 status is now `blocked`
  - execution must not resume from the current plan set until the missing step coverage is repaired
- Remaining blocker:
  - regenerate or repair the Phase 1 `.planning/workstreams/vortex/phases/01-substrate-migration/*-PLAN.md` files so they explicitly cover steps `1.3`, `1.4`, and `1.5` before implementation resumes

### 2026-04-13 — PLAN.md synchronization

- Changed files this session:
  - `design/vortex/PLAN.md`
- Commands used for verification:
  - repo inspection via `rg`
  - targeted section rereads
  - `git diff -- design/vortex/PLAN.md`
- Result:
  - PLAN.md now explicitly covers Phase 2 activation/validation for `Stencil`
    and `CustomDepth`
  - Phase 5 feature-gated runtime validation now includes `no-shadowing` and
    `no-volumetrics`
  - Phase 7 now maps `MegaLights-class lighting extensions`,
    `Heterogeneous volumes`, and `Hair strands` to explicit future activation
    slots
- Code / validation delta:
  - no implementation code changed
  - no build or tests were run
  - no phase status changed
- Ledger impact:
  - Phase 2 work-item list below must include 2.12 from the updated plan
  - Phase 5 runtime-variant scope below must reflect the expanded PRD §6.6 set
- Remaining blocker:
  - Phase 0 is still incomplete; no implementation progress is claimed from this
    documentation-only session

---

## Phase 0 — Scaffold and Build Integration

**Status:** `done`

### What Exists

| Item | Path | Verified |
| ---- | ---- | -------- |
| Directory tree | `src/Oxygen/Vortex/` (subdirs with `.gitkeep`) | Yes — repo inspection |
| Parent CMake wiring | `src/Oxygen/CMakeLists.txt` | Yes — `add_subdirectory("Vortex")` now present once |
| CMakeLists.txt | `src/Oxygen/Vortex/CMakeLists.txt` | Yes — declares `Oxygen.Vortex`, links deps, C++23 |
| Module anchor source | `src/Oxygen/Vortex/ModuleAnchor.cpp` | Yes — minimal translation unit added so the scaffolded library can generate |
| Export header | `src/Oxygen/Vortex/api_export.h` | Yes — exists |
| Test CMake | `src/Oxygen/Vortex/Test/CMakeLists.txt` | Yes — `Oxygen.Vortex.LinkTest` is enabled and links against `oxygen::vortex` |
| Link smoke source | `src/Oxygen/Vortex/Test/Link_test.cpp` | Yes — minimal consumer includes `Oxygen/Vortex/api_export.h` and exits 0 |

### What Is Missing

None for Phase 0 exit. The standard preset build path, the generated target, and
the alias consumer have all been proven. Remaining work begins in Phase 1.

### Validation Log

| Date | Command | Result |
| ---- | ------- | ------ |
| (initial) | `cmake --build --preset windows-debug --target Oxygen.Vortex --parallel 4` | FAIL — "unknown target 'Oxygen.Vortex'" |
| 2026-04-13 | `rg -n 'add_subdirectory\\("Vortex"\\)' src/Oxygen/CMakeLists.txt` | PASS — parent CMake now wires `Vortex` once |
| 2026-04-13 | `cmake --preset windows-default` | PASS — configure/generate now succeeds with `oxygen-vortex` in the generated project graph |
| 2026-04-13 | `D:/dev/ninja/ninja.exe -C out/build-ninja -f build-Debug.ninja -t targets all \| Select-String 'vortex'` | PASS — generated Debug Ninja graph contains `oxygen-vortex`, `Oxygen.Vortex-d.dll`, and Vortex source-tree targets |
| 2026-04-13 | `cmake --build --preset windows-debug --target help` | FAIL — regeneration blocked in `_deps/ccache.cmake-subbuild` by `ninja: error: failed recompaction: Permission denied` |
| 2026-04-13 | `rg -n 'Oxygen\\.Vortex\\.LinkTest\|oxygen::vortex\|Link_test\\.cpp' src/Oxygen/Vortex/Test/CMakeLists.txt src/Oxygen/Vortex/Test/Link_test.cpp` | PASS — minimal alias smoke target and source are present |
| 2026-04-13 | `cmake --preset windows-default` | PASS — configure/generate succeeds after enabling the link smoke target |
| 2026-04-13 | `D:/dev/ninja/ninja.exe -C out/build-ninja -f build-Debug.ninja oxygen-vortex Oxygen.Vortex.LinkTest` | PASS — both the Vortex library and the alias consumer build in Debug |
| 2026-04-13 | `ctest --test-dir out/build-ninja -C Debug -R '^Oxygen\\.Vortex\\.LinkTest$' --output-on-failure` | PASS — `Oxygen.Vortex.LinkTest` passes |
| 2026-04-13 | `Remove-Item out/build-ninja/_deps/ccache.cmake-subbuild/.ninja_log{,.restat}; cmake --build --preset windows-debug --target oxygen-vortex Oxygen.Vortex.LinkTest` | PASS — standard preset build path works after resetting the generated ccache subbuild logs |

### Resume Point

Phase 0 is complete. Resume with Phase 1 substrate migration once the Phase 1
design and execution work starts.

---

## Phase 1 — Substrate Migration

**Status:** `in_progress`

### What Exists

- A repaired 11-plan Phase 1 execution set under
  `.planning/workstreams/vortex/phases/01-substrate-migration/`.
- Plan coverage now matches the source-of-truth Vortex design package:
  - `01-04` covers resources
  - `01-05` covers ScenePrep data/config
  - `01-06` covers ScenePrep execution plus the selected substrate-only
    internal utilities
  - `01-08` closes only step `1.7`
  - `01-09` and `01-10` keep step `1.8` split between support-file migration
    and the stripped orchestrator
- Every Phase 1 plan now has 2 tasks plus the required `<read_first>` and
  `<acceptance_criteria>` blocks.
- Step `1.1` is now fully migrated under `src/Oxygen/Vortex/Types/`,
  including the `01-01` frame-binding slice plus
  `EnvironmentViewData.h`, `ViewColorData.h`, `ViewConstants.h`,
  `ViewConstants.cpp`, and `ViewFrameBindings.h`.

### Steps (from PLAN.md §3)

| Step | Task | Status | Evidence |
| ---- | ---- | ------ | -------- |
| 1.1 | Cross-cutting types (14 headers) | `done` | `01-01` migrated the frame-binding slice; `01-02` landed the remaining type headers, built `oxygen-vortex`, and verified no `Oxygen.Renderer` dependency edge |
| 1.2 | Upload subsystem (14 files) | `not_started` | — |
| 1.3 | Resources subsystem (7 files) | `planned` | Phase 1 plan `01-04` |
| 1.4 | ScenePrep subsystem (15 files) | `planned` | Phase 1 plans `01-05` and `01-06` |
| 1.5 | Internal utilities (7 files) | `planned` | Phase 1 plan `01-06` |
| 1.6 | Pass base classes (3 files) | `not_started` | — |
| 1.7 | View assembly + composition | `not_started` | — |
| 1.8 | Renderer orchestrator | `not_started` | — |
| 1.9 | Smoke test | `not_started` | — |

### Resume Point

Continue with `01-03` to begin step `1.2` (upload foundation and staging),
then resume the Phase 1 sequence.

---

## Phase 2 — SceneTextures + SceneRenderer Shell

**Status:** `not_started`

### Design Prerequisites

| Deliverable | Status |
| ----------- | ------ |
| D.1 SceneTextures four-part contract | `not_started` |
| D.2 SceneRenderBuilder bootstrap | `not_started` |
| D.3 SceneRenderer shell dispatch | `not_started` |

### Work Items (from PLAN.md §4)

| ID | Task | Status | Evidence |
| -- | ---- | ------ | -------- |
| 2.1–2.4 | SceneTextures four-part contract | `not_started` | — |
| 2.5 | ShadingMode enum | `not_started` | — |
| 2.6 | SceneRenderBuilder | `not_started` | — |
| 2.7 | SceneRenderer shell (23-stage skeleton) | `not_started` | — |
| 2.8 | Wire SceneRenderer into Renderer | `not_started` | — |
| 2.9 | PostRenderCleanup | `not_started` | — |
| 2.10 | ResolveSceneColor | `not_started` | — |
| 2.11 | Stages directory structure | `not_started` | — |
| 2.12 | Validate first active `SceneTextures` subset (`SceneColor`, `SceneDepth`, `PartialDepth`, `GBufferA-D`, `Stencil`, `Velocity`, `CustomDepth`) | `not_started` | — |

### Resume Point

Phase 1 + design deliverables D.1–D.3 must be completed first.

---

## Phase 3 — Deferred Core

**Status:** `not_started`

### Design Prerequisites

| Deliverable | Status |
| ----------- | ------ |
| D.4 Depth prepass LLD | `not_started` |
| D.5 Base pass LLD | `not_started` |
| D.6 Deferred lighting LLD | `not_started` |
| D.7 Shader contracts LLD | `not_started` |
| D.8 InitViews LLD | `not_started` |

### Resume Point

Phase 2 + design deliverables D.4–D.8 must be completed first.

---

## Phase 4 — Migration-Critical Services + First Migration

**Status:** `not_started`

### Per-Service Status

| Service | Deliverable | Design Status | Impl Status |
| ------- | ----------- | ------------- | ----------- |
| 4A LightingService | D.9 | `not_started` | `not_started` |
| 4B PostProcessService | D.10 | `not_started` | `not_started` |
| 4C ShadowService | D.11 | `not_started` | `not_started` |
| 4D EnvironmentLightingService | D.12 | `not_started` | `not_started` |
| 4E Examples/Async migration | D.13 | `not_started` | `not_started` |
| 4F Composition/presentation validation | — | — | `not_started` |

### Resume Point

Phase 3 must be completed first. 4A–4D are parallelizable. 4E requires all
four services. 4F follows 4E.

---

## Phase 5 — Remaining Services + Runtime Scenarios

**Status:** `not_started`

### Per-Item Status

| Item | Deliverable | Design Status | Impl Status |
| ---- | ----------- | ------------- | ----------- |
| 5A DiagnosticsService | D.14 | `not_started` | `not_started` |
| 5B TranslucencyModule | D.15 | `not_started` | `not_started` |
| 5C OcclusionModule | D.16 | `not_started` | `not_started` |
| 5D Multi-view / per-view mode | D.17 | `not_started` | `not_started` |
| 5E Offscreen / facade validation | D.18 | `not_started` | `not_started` |
| 5F Feature-gated runtime variants (`depth-only`, `shadow-only`, `no-environment`, `no-shadowing`, `no-volumetrics`, `diagnostics-only`) | — | — | `not_started` |

### Resume Point

Phase 4 must be completed first. 5A–5E are parallelizable. 5F requires all
services.

---

## Phase 6 — Legacy Deprecation

**Status:** `not_started`

### Resume Point

Phase 5 must be completed first.

---

## Architectural Resume Notes

When implementation resumes, keep these baseline facts explicit:

- The active Vortex source-of-truth package is:
  `PRD.md`, `ARCHITECTURE.md`, `DESIGN.md`, `PROJECT-LAYOUT.md`, `PLAN.md`,
  and this file.
- DESIGN.md is an **early draft** — it covers illustrative shapes (SceneRenderer
  class structure, SceneTextures allocation, GBuffer format, frame dispatch,
  subsystem contracts, base pass, deferred lighting, substrate adaptation,
  shader organization) but is NOT complete LLD. Missing areas include:
  SceneTextureSetupMode, SceneTextureBindings, SceneTextureExtracts,
  InitViewsModule, SceneRenderBuilder, velocity distribution, extraction/handoff,
  per-subsystem LLD.
- Each phase in PLAN.md identifies specific design deliverables that must be
  completed before implementation begins.
- The current legacy renderer is still the live implementation and the current
  source of reusable substrate.
- Referenced historical documents `vortex-initial-design.md` and
  `parity-analysis.md` do not exist in the repo; the current five-document
  package supersedes them.
- The active production path is `Oxygen.Renderer` + `ForwardPipeline`.
- Use frame 10 as the RenderDoc baseline capture point.
