---
phase: 03-deferred-core
plan: "12"
subsystem: lighting
tags: [vortex, deferred-core, lighting, shaders, shaderbake]
requires:
  - phase: 03-deferred-core/11
    provides: Stage 10 publication remains the truthful deferred-input boundary for later lighting consumers
provides:
  - Lighting-domain deferred-light shared HLSL helpers for published bindings, BRDF evaluation, and local-light attenuation
  - final directional, point, and spot deferred-light shader entrypoints under `Vortex/Services/Lighting/`
  - `EngineShaderCatalog.h` registrations that make ShaderBake compile the Phase 3 deferred-light family
affects: [phase-03-stage12-runtime, phase-04-lighting-service]
tech-stack:
  added: []
  patterns:
    - Deferred-light shaders resolve current-view scene textures through `LoadBindingsFromCurrentView()` and published `ViewFrameBindings`
    - Lighting-domain shader file homes can land before Stage 12 CPU orchestration so later plans avoid shader-path churn
key-files:
  created:
    - src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Shared/DeferredShadingCommon.hlsli
    - src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Lighting/DeferredLightingCommon.hlsli
    - src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Lighting/DeferredLightDirectional.hlsl
    - src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Lighting/DeferredLightPoint.hlsl
    - src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Lighting/DeferredLightSpot.hlsl
  modified:
    - src/Oxygen/Graphics/Direct3D12/Shaders/EngineShaderCatalog.h
key-decisions:
  - "Used the existing renderer `ViewConstants` bindless view-frame slot to load published scene-texture bindings instead of introducing a new lighting-only binding contract in this plan."
  - "Replaced the unsupported matrix-inverse approach with view/projection-based world-position reconstruction so the new deferred-light family compiles without widening Stage 12 CPU scope."
patterns-established:
  - "Deferred-light shader registration in `EngineShaderCatalog.h` documents the request identities inline beside each `ShaderFileSpec` block."
  - "Phase 3 lighting helpers stay shader-only here; runtime PSO and SceneRenderer Stage 12 ownership remain for later plans."
requirements-completed: [SHDR-01]
duration: "8 min"
completed: 2026-04-15
---

# Phase 03 Plan 12: Deferred-Light Shader Family Summary

**Lighting-domain deferred-light shaders now compile through ShaderBake with shared Vortex GBuffer/BRDF helpers and final directional, point, and spot registrations**

## Performance

- **Duration:** 8 min
- **Started:** 2026-04-15T14:46:36+04:00
- **Completed:** 2026-04-15T14:54:11+04:00
- **Tasks:** 1
- **Files modified:** 6

## Accomplishments

- Added `DeferredShadingCommon.hlsli` and `DeferredLightingCommon.hlsli` so the deferred-light family shares published-binding loading, BRDF evaluation, and local-light attenuation helpers.
- Added the final directional, point, and spot deferred-light entrypoints under `Vortex/Services/Lighting/`.
- Registered the new deferred-light family in `EngineShaderCatalog.h` and proved ShaderBake compiles the six new requests as part of `oxygen-graphics-direct3d12`.

## Task Commits

1. **Task 1: Add deferred-light family HLSL and register the final lighting requests**
   `5bb7bee3e` - `feat(03-12): restore the final Phase 3 deferred-light shader surface`

## Files Created/Modified

- `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Shared/DeferredShadingCommon.hlsli` - shared deferred-light shading helpers for GBuffer decode, approximate world-position reconstruction, and Cook-Torrance evaluation.
- `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Lighting/DeferredLightingCommon.hlsli` - family-local helpers for published current-view bindings, volume-vertex generation, and local-light attenuation.
- `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Lighting/DeferredLightDirectional.hlsl` - fullscreen directional deferred-light entrypoints.
- `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Lighting/DeferredLightPoint.hlsl` - sphere-volume point-light deferred entrypoints.
- `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Lighting/DeferredLightSpot.hlsl` - cone-volume spot-light deferred entrypoints.
- `src/Oxygen/Graphics/Direct3D12/Shaders/EngineShaderCatalog.h` - ShaderBake registration for the final directional, point, and spot deferred-light requests.

## Decisions Made

- Reused the existing renderer view constants and published bindless view-frame slot because this plan only owns shader-family publication, not a new Stage 12 CPU constant-binding contract.
- Kept the shader family in the Lighting domain now, even though CPU orchestration is still inline in `SceneRenderer`, to preserve the authored file-home contract and avoid future shader-path churn.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Replaced unsupported matrix inversion in the shared deferred-light helper**
- **Found during:** Task 1 (Add deferred-light family HLSL and register the final lighting requests)
- **Issue:** DXC rejected the initial `inverse(...)` helper in `DeferredLightingCommon.hlsli`, which blocked ShaderBake from compiling the new lighting family.
- **Fix:** Moved world-position reconstruction to a view/projection-based helper in `DeferredShadingCommon.hlsli` and updated the deferred-light shaders to use that compile-safe path.
- **Files modified:** `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Shared/DeferredShadingCommon.hlsli`, `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Lighting/DeferredLightingCommon.hlsli`, `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Lighting/DeferredLightDirectional.hlsl`, `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Lighting/DeferredLightPoint.hlsl`, `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Lighting/DeferredLightSpot.hlsl`
- **Verification:** `cmake --build --preset windows-debug --target oxygen-graphics-direct3d12 --parallel 4` passed and ShaderBake compiled all six deferred-light requests.
- **Committed in:** `5bb7bee3e`

---

**Total deviations:** 1 auto-fixed (1 blocking)
**Impact on plan:** The fix stayed inside the authored shader-family scope and was required for ShaderBake to accept the new deferred-light registrations.

## Issues Encountered

- `oxytidy` cannot analyze a standalone header entry like `EngineShaderCatalog.h` without a matching translation unit in `compile_commands.json`, so verification was rerun against `EngineShaders.cpp`, which owns the shader registry TU and passed cleanly.

## User Setup Required

None - no external service configuration required.

## Verification Evidence

- `rg -n 'LoadBindingsFromCurrentView\(' src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Lighting/DeferredLightingCommon.hlsli` passed with the helper at line `34`.
- `rg -n 'DeferredLightDirectionalPS|DeferredLightPointPS|DeferredLightSpotPS' src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Lighting/DeferredLightDirectional.hlsl src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Lighting/DeferredLightPoint.hlsl src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Lighting/DeferredLightSpot.hlsl` passed at lines `23`, `30`, and `30`.
- `rg -n 'VortexDeferredLightDirectionalVS|VortexDeferredLightDirectionalPS|VortexDeferredLightPointVS|VortexDeferredLightPointPS|VortexDeferredLightSpotVS|VortexDeferredLightSpotPS' src/Oxygen/Graphics/Direct3D12/Shaders/EngineShaderCatalog.h` passed at lines `320`, `325`, and `330`.
- `cmake --build --preset windows-debug --target oxygen-graphics-direct3d12 --parallel 4` passed; ShaderBake expanded `194` requests, compiled the six new deferred-light requests, and repacked `bin/Oxygen/Debug/dev/shaders.bin`.
- `powershell -ExecutionPolicy Bypass -File tools\cli\oxytidy.ps1 src\Oxygen\Graphics\Direct3D12\Shaders\EngineShaders.cpp -Configuration Debug -SummaryOnly` passed with `0 warnings, 0 errors`.

## Next Phase Readiness

- Ready for `03-13-PLAN.md`.
- Phase 3 now has the authored Lighting-domain deferred-light shader family on disk and in ShaderBake, so later work can focus on Stage 12 runtime orchestration instead of shader-path scaffolding.

## Self-Check

PASSED - `03-12-SUMMARY.md` exists on disk, the created/modified shader files are
present, commit `5bb7bee3e` is in history, and the touched shader files contain
no placeholder/TODO stub markers.

---
*Phase: 03-deferred-core*
*Completed: 2026-04-15*
