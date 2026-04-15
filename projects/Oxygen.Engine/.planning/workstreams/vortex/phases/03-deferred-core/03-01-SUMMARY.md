---
phase: 03-deferred-core
plan: "01"
subsystem: shaders
tags: [vortex, shaders, shader-contracts, deferred-core]
requires:
  - phase: 02-scenetextures-and-scenerenderer-shell/08
    provides: scene-texture publication and view-frame routing vocabulary
provides:
  - shared Vortex HLSL contracts for scene textures, view bindings, and GBuffers
  - shared fullscreen-triangle and pack/unpack helpers for later Phase 3 shaders
affects: [phase-03-shader-foundation, phase-03-depth-base-seeds]
tech-stack:
  added: []
  patterns:
    - CPU/HLSL contract mirroring before stage entrypoints land
    - invalid-slot sentinel preservation across scene-texture and view-frame bindings
key-files:
  created:
    - src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Contracts/Definitions/SceneDefinitions.hlsli
    - src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Contracts/Definitions/LightDefinitions.hlsli
    - src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Contracts/SceneTextures.hlsli
    - src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Contracts/SceneTextureBindings.hlsli
    - src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Contracts/ViewFrameBindings.hlsli
    - src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Contracts/GBufferLayout.hlsli
    - src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Contracts/GBufferHelpers.hlsli
    - src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Shared/FullscreenTriangle.hlsli
    - src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Shared/PackUnpack.hlsli
    - .planning/workstreams/vortex/phases/03-deferred-core/03-01-SUMMARY.md
  modified:
    - .planning/workstreams/vortex/STATE.md
    - .planning/workstreams/vortex/ROADMAP.md
    - .planning/workstreams/vortex/REQUIREMENTS.md
key-decisions:
  - "Phase 03-01 stays contract-only so later depth/base and deferred-light shaders all consume one shared vocabulary."
  - "The shader-side scene-texture and view-frame bindings preserve the CPU field names and invalid-slot sentinel semantics."
patterns-established:
  - "Shared Vortex HLSL contracts land before any stage entrypoints or catalog registrations."
requirements-completed: []
requirements-advanced: [SHDR-01]
duration: "~20min"
completed: 2026-04-15
---

# Phase 03 Plan 01: Shared Shader Contracts Summary

**Shared Vortex HLSL contracts now exist for scene textures, view-frame routing, GBuffers, and the minimal helper layer that later Phase 3 shaders will build on**

## Performance

- **Duration:** ~20 min
- **Completed:** 2026-04-15T11:29:32+04:00
- **Tasks:** 1
- **Files created:** 9

## Accomplishments

- Added the first Vortex shader-contract tree under `Shaders/Vortex/Contracts/` with shared scene-definition, light-definition, scene-texture, view-frame, and GBuffer includes.
- Mirrored the CPU publication vocabulary from `SceneTextures.h` and `ViewFrameBindings.h`, including the invalid bindless sentinel semantics and the full Phase 2 view-slot family.
- Added the shared fullscreen-triangle and octahedral pack/unpack helpers that later depth/base/deferred-light shaders can reuse without inventing parallel local helpers.

## Task Commits

- `b1c47b713` - contract-only HLSL foundation for plan `03-01`

## Files Created/Modified

- `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Contracts/Definitions/*.hlsli` - shared numeric vocabulary for scene and light contracts.
- `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Contracts/SceneTextureBindings.hlsli` - shader-side mirror of the published scene-texture binding payload.
- `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Contracts/ViewFrameBindings.hlsli` - shader-side mirror of the per-view routing payload, including `scene_texture_frame_slot`.
- `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Contracts/GBufferLayout.hlsli` and `GBufferHelpers.hlsli` - semantic GBuffer slot vocabulary plus `ReadGBuffer(...)`.
- `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Shared/*.hlsli` - reusable fullscreen-triangle and pack/unpack helpers.

## Verification Evidence

- The plan `03-01` acceptance grep bundle passed for scene-texture fields, invalid-slot documentation, `scene_texture_frame_slot`, semantic GBuffer slot names, and `ReadGBuffer(...)`.
- CPU/HLSL parity token checks passed across `SceneTextures.h` vs `SceneTextureBindings.hlsli` and `ViewFrameBindings.h` vs `ViewFrameBindings.hlsli`.
- `git diff --check` passed for the intentional plan changes; the only extra warning was the pre-existing dirty `design/vortex/IMPLEMENTATION-STATUS.md`.

## Decisions Made

- Kept this slice strictly include-only so `03-02` can own the first entrypoints and ShaderBake catalog registrations without mixed concerns.
- Preserved the invalid bindless sentinel explicitly in the shader contracts instead of introducing Vortex-local alternative defaults.

## Deviations from Plan

None - plan executed as written.

## Issues Encountered

None.

## User Setup Required

None.

## Next Phase Readiness

- Ready for `03-02-PLAN.md`.
- `SHDR-01` remains pending until the first Vortex shader entrypoints are registered in `EngineShaderCatalog.h` and compile through ShaderBake.

## Self-Check

PASSED - the summary file exists, task commit `b1c47b713` exists in history, and the recorded verification commands passed.

---
*Phase: 03-deferred-core*
*Completed: 2026-04-15*
