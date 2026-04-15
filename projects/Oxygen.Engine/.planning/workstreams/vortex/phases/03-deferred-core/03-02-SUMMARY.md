---
phase: 03-deferred-core
plan: "02"
subsystem: shaders
tags: [vortex, shaders, shaderbake, deferred-core, depth-prepass, base-pass]
requires:
  - phase: 03-deferred-core/01
    provides: shared Vortex shader contracts and helper includes
provides:
  - initial Vortex depth-prepass and base-pass GBuffer entrypoints
  - first EngineShaderCatalog registrations for Vortex deferred-core shaders
  - minimal Vortex BRDF/material adapter helpers for the base-pass seed path
affects: [phase-03-shader-foundation, phase-03-depth-base-seeds]
tech-stack:
  added: []
  patterns:
    - catalog-first Vortex shader registration before any CPU-side stage wiring
    - Vortex base-pass seeding by adapting the existing forward material evaluation path
key-files:
  created:
    - src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Shared/BRDFCommon.hlsli
    - src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Materials/GBufferMaterialOutput.hlsli
    - src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Stages/DepthPrepass/DepthPrepass.hlsl
    - src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Stages/BasePass/BasePassGBuffer.hlsl
    - .planning/workstreams/vortex/phases/03-deferred-core/03-02-SUMMARY.md
  modified:
    - src/Oxygen/Graphics/Direct3D12/Shaders/EngineShaderCatalog.h
    - .planning/workstreams/vortex/STATE.md
    - .planning/workstreams/vortex/ROADMAP.md
    - .planning/workstreams/vortex/REQUIREMENTS.md
key-decisions:
  - "Register only Vortex depth/base requests in this slice; deferred-light shader families stay with later Phase 03 plans."
  - "Seed the Vortex base-pass shader by adapting the existing forward material evaluation path instead of introducing a duplicate material stack."
patterns-established:
  - "The first Vortex stage entrypoints compile through ShaderBake before SceneRenderer stage wiring lands."
requirements-completed: []
requirements-advanced: [SHDR-01]
duration: "~15min"
completed: 2026-04-15
---

# Phase 03 Plan 02: Depth/Base Shader Seeds Summary

**The first Vortex deferred-core entrypoints now exist, are registered in the engine shader catalog, and compile through ShaderBake without pulling later deferred-light work forward**

## Performance

- **Duration:** ~15 min
- **Completed:** 2026-04-15T11:41:58+04:00
- **Tasks:** 1
- **Files created:** 4

## Accomplishments

- Added the first Vortex stage entrypoints for `DepthPrepass` and `BasePassGBuffer`, including the `DepthPrepassVS`/`PS` and `BasePassGBufferVS`/`PS` seed functions required by the plan.
- Added the minimal Vortex helper layer those entrypoints need: `BRDFCommon.hlsli` for shared deferred-core numeric helpers and `GBufferMaterialOutput.hlsli` for adapting the existing forward material evaluation path into semantic GBuffer outputs.
- Registered only the initial Phase 3 Vortex depth/base requests in `EngineShaderCatalog.h`, with `HAS_VELOCITY` permutations on the depth-prepass family and no deferred-light registrations yet.

## Task Commits

- `54b3f6e87` - seed Vortex deferred-core entrypoints and ShaderBake registrations for plan `03-02`

## Files Created/Modified

- `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Stages/DepthPrepass/DepthPrepass.hlsl` - first Vortex depth-prepass seed shader with the `HAS_VELOCITY` permutation family.
- `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Stages/BasePass/BasePassGBuffer.hlsl` - first Vortex base-pass GBuffer seed shader wired to the existing draw/material helper path.
- `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Materials/GBufferMaterialOutput.hlsli` - adapter from current material evaluation to Vortex semantic GBuffer packing.
- `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Shared/BRDFCommon.hlsli` - shared deferred-core BRDF seed helpers for later lighting work.
- `src/Oxygen/Graphics/Direct3D12/Shaders/EngineShaderCatalog.h` - initial Phase 3 Vortex depth/base registrations only.

## Verification Evidence

- The plan acceptance grep bundle passed for `DepthPrepassVS`/`DepthPrepassPS`, `BasePassGBufferVS`/`BasePassGBufferPS`, the two Vortex stage paths in `EngineShaderCatalog.h`, and the `HAS_VELOCITY` permutation.
- A negative catalog check confirmed there are still no `Vortex/Services/Lighting/*` registrations in `EngineShaderCatalog.h`, so this plan did not execute later deferred-light work.
- `cmake --build --preset windows-debug --target oxygen-graphics-direct3d12 --parallel 4` passed and ShaderBake expanded the catalog to 177 requests, compiling the 6 new Vortex depth/base variants successfully.
- `git diff --check` passed for the plan changes; Git emitted only the non-failing Windows LF→CRLF working-tree warning.

## Decisions Made

- Kept the catalog scope intentionally narrow so `03-12` still owns the deferred-light shader family registrations.
- Reused the existing forward material evaluation helpers for the base-pass seed path instead of inventing a Vortex-only material stack before the stage wiring exists.

## Deviations from Plan

None - plan executed as written.

## Issues Encountered

None.

## User Setup Required

None.

## Next Phase Readiness

- Ready for `03-03-PLAN.md`.
- `SHDR-01` remains pending because Phase 03 still needs later Vortex shader families beyond the initial depth/base registrations landed here.

## Self-Check

PASSED - the summary file exists, task commit `54b3f6e87` exists in history, and the recorded verification commands passed.

---
*Phase: 03-deferred-core*
*Completed: 2026-04-15*
