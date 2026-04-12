# Vortex Renderer Implementation Status

Status: `in_progress — Phase 0 incomplete (scaffold on disk, not in build graph)`

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
| 0 | Scaffold and Build Integration | `in_progress` | `add_subdirectory("Vortex")` not in parent CMake |
| 1 | Substrate Migration | `not_started` | Phase 0 |
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

**Status:** `in_progress`

### What Exists

| Item | Path | Verified |
| ---- | ---- | -------- |
| Directory tree | `src/Oxygen/Vortex/` (subdirs with `.gitkeep`) | Yes — repo inspection |
| CMakeLists.txt | `src/Oxygen/Vortex/CMakeLists.txt` | Yes — declares `Oxygen.Vortex`, links deps, C++23 |
| Export header | `src/Oxygen/Vortex/api_export.h` | Yes — exists |
| Test CMake | `src/Oxygen/Vortex/Test/CMakeLists.txt` | Yes — link test block commented out |

### What Is Missing

| Item | Detail |
| ---- | ------ |
| Parent CMake wiring | `src/Oxygen/CMakeLists.txt` does NOT contain `add_subdirectory("Vortex")` |
| Successful build | `cmake --build --preset windows-debug --target Oxygen.Vortex` fails: "unknown target" |
| Target alias verification | `oxygen::vortex` not verified |

### Validation Log

| Date | Command | Result |
| ---- | ------- | ------ |
| (initial) | `cmake --build --preset windows-debug --target Oxygen.Vortex --parallel 4` | FAIL — "unknown target 'Oxygen.Vortex'" |

### Resume Point

Wire `add_subdirectory("Vortex")` into `src/Oxygen/CMakeLists.txt`, then verify
the target builds with an empty source set.

---

## Phase 1 — Substrate Migration

**Status:** `not_started`

### Steps (from PLAN.md §3)

| Step | Task | Status | Evidence |
| ---- | ---- | ------ | -------- |
| 1.1 | Cross-cutting types (14 headers) | `not_started` | — |
| 1.2 | Upload subsystem (14 files) | `not_started` | — |
| 1.3 | Resources subsystem (7 files) | `not_started` | — |
| 1.4 | ScenePrep subsystem (15 files) | `not_started` | — |
| 1.5 | Internal utilities (7 files) | `not_started` | — |
| 1.6 | Pass base classes (3 files) | `not_started` | — |
| 1.7 | View assembly + composition | `not_started` | — |
| 1.8 | Renderer orchestrator | `not_started` | — |
| 1.9 | Smoke test | `not_started` | — |

### Resume Point

Phase 0 must be completed first.

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
