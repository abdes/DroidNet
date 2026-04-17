---
phase: 04-migration-critical-services-and-first-migration
plan: "04-03"
subsystem: shadow-service
tags:
  - vortex
  - shadows
  - directional
  - deferred
  - migration
requires:
  - phase: 04-01
    provides: truthful Stage 6 / Stage 12 lighting ownership and the shared FrameLightSelection directional-light seam
provides:
  - directional-only ShadowService family with per-view ShadowFrameBindings publication
  - Stage 8 ShadowService dispatch and current-view shadow slot publication through ViewFrameBindings
  - Stage 12 directional shadow-term consumption in deferred lighting with VSM and local-light conventional shadows still disabled
  - directional shadow depth shader family registered in EngineShaderCatalog and exercised by ShaderBake/runtime validation
affects:
  - 04-05 examples-async migration
  - 04-06 composition/presentation validation
  - Stage 8 shadow depth path
  - Stage 12 deferred direct lighting
tech-stack:
  added: []
  patterns:
    - per-view directional shadow publication through ViewFrameBindings
    - directional shadow depth rendering reuses existing draw bindings from the current view constants path
    - deferred directional lighting consumes the published shadow product without widening to VSM or local-light conventional shadows
key-files:
  created:
    - src/Oxygen/Vortex/Shadows/ShadowService.h
    - src/Oxygen/Vortex/Shadows/ShadowService.cpp
    - src/Oxygen/Vortex/Shadows/Passes/ShadowDepthPass.h
    - src/Oxygen/Vortex/Shadows/Passes/ShadowDepthPass.cpp
    - src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Shadows/DirectionalShadowDepth.hlsl
    - src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Shadows/DirectionalShadowCommon.hlsli
  modified:
    - src/Oxygen/Vortex/SceneRenderer/SceneRenderer.h
    - src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp
    - src/Oxygen/Vortex/Lighting/LightingService.h
    - src/Oxygen/Vortex/Lighting/LightingService.cpp
    - src/Oxygen/Vortex/Lighting/Passes/DeferredLightPass.h
    - src/Oxygen/Vortex/Lighting/Passes/DeferredLightPass.cpp
    - src/Oxygen/Graphics/Direct3D12/Shaders/EngineShaderCatalog.h
    - src/Oxygen/Vortex/Test/ShadowService_test.cpp
    - src/Oxygen/Vortex/Test/SceneRendererDeferredCore_test.cpp
    - src/Oxygen/Vortex/Test/Resources/DrawMetadataEmitter_test.cpp
key-decisions:
  - "ShadowService publishes the Phase 4C contract as directional-only ShadowFrameBindings and keeps VSM/local-light conventional shadows out of the ABI."
  - "SceneRenderer republished the current view bindings immediately after Stage 8 so Stage 12 shaders consume the shadow slot through the normal ViewConstants path."
  - "The directional shadow depth pass reuses existing draw bindings and current-view constants instead of introducing a second draw/publication system."
patterns-established:
  - "Pattern 1: frame-scope ShadowService build with per-view shadow publication, matching LightingService ownership style."
  - "Pattern 2: Stage 12 shadow consumption is gated by the published directional shadow product and does not infer or synthesize a separate shadow authority seam."
requirements-completed:
  - SHDW-01
duration: 34m
completed: 2026-04-17
---

# Phase 04 Plan 03: ShadowService Summary

**Directional-first ShadowService with Stage 8 publication and Stage 12 deferred-light shadow consumption, while VSM and local-light conventional shadows remain explicitly inactive**

## Performance

- **Duration:** 34 min
- **Started:** 2026-04-17T17:42:23+04:00
- **Completed:** 2026-04-17T18:16:12+04:00
- **Tasks:** 2
- **Files modified:** 24

## Accomplishments

- Added the real `src/Oxygen/Vortex/Shadows/` family: `ShadowService`, directional cascade setup, directional-only target allocation, shadow-caster culling, cascade orchestration, and a real shadow depth pass surface.
- Replaced the placeholder `ShadowFrameBindings` ABI with the Phase 4C directional conventional-shadow contract and published it through `ViewFrameBindings.shadow_frame_slot`.
- Wired Stage 8 through `ShadowService`, refreshed current-view bindings after shadow publication, and made Stage 12 directional deferred lighting consume the published shadow product without enabling VSM or local-light conventional shadows.
- Registered and validated the new directional shadow depth shader family via `EngineShaderCatalog` / ShaderBake and reran the live `VortexBasic` runtime validator successfully.

## Task Commits

Each task was committed atomically:

1. **Task 1 RED: create the directional-only ShadowService contract surface** - `08ed8b145` (`test`)
2. **Task 1 GREEN: add the ShadowService family and directional publication contract** - `4c2b3cca5` (`feat`)
3. **Task 2 RED: pin Stage 8 / Stage 12 shadow integration proof** - `0b86020b1` (`test`)
4. **Task 2 GREEN: wire ShadowService into SceneRenderer and Stage 12 lighting** - `318be0647` (`feat`)

## Files Created/Modified

- `src/Oxygen/Vortex/Shadows/ShadowService.h/.cpp` - service-owned per-view shadow publication and CPU inspection/seam resolution
- `src/Oxygen/Vortex/Shadows/Internal/CascadeShadowSetup.*` - directional cascade matrices and sampling metadata generation
- `src/Oxygen/Vortex/Shadows/Internal/ConventionalShadowTargetAllocator.*` - directional-only backing surface allocation and SRV registration
- `src/Oxygen/Vortex/Shadows/Internal/ShadowCasterCulling.*` - Stage 8 shadow-caster filtering from prepared draw metadata
- `src/Oxygen/Vortex/Shadows/Passes/ShadowDepthPass.*` - Stage 8 directional shadow depth recording against the published draw bindings path
- `src/Oxygen/Vortex/Shadows/Passes/CascadeShadowPass.*` - per-view cascade orchestration and ShadowDepthPass dispatch
- `src/Oxygen/Vortex/Types/ShadowFrameBindings.h` - new directional conventional-shadow contract
- `src/Oxygen/Vortex/Shadows/Types/*` - frame input, cascade binding, and directional frame-data types
- `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.h/.cpp` - ShadowService ownership, Stage 8 dispatch, current-view binding refresh, and new deferred-light shadow state
- `src/Oxygen/Vortex/Lighting/LightingService.*` - directional shadow product handoff into deferred direct lighting
- `src/Oxygen/Vortex/Lighting/Passes/DeferredLightPass.*` - directional shadow term bookkeeping and texture-state handling
- `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Shadows/*` - directional shadow depth shader and Stage 12 sampling helpers
- `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Lighting/DeferredLightDirectional.hlsl` - directional shadow attenuation in the deferred directional light path
- `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Lighting/DeferredLightingCommon.hlsli` - shadow-info expansion in the lighting constant contract
- `src/Oxygen/Graphics/Direct3D12/Shaders/EngineShaderCatalog.h` - directional shadow depth shader registration
- `src/Oxygen/Vortex/Test/ShadowService_test.cpp` - contract, catalog, and service-surface proof
- `src/Oxygen/Vortex/Test/SceneRendererDeferredCore_test.cpp` - Stage 8 publication and Stage 12 directional shadow consumption proof
- `src/Oxygen/Vortex/Test/Resources/DrawMetadataEmitter_test.cpp` - Stage 8 shadow-caster routing proof

## Decisions Made

- Kept the Phase 4C ABI directional-only: `ShadowFrameBindings` now exposes one conventional directional surface plus cascade metadata and deliberately omits local-light shadow payloads and VSM state.
- Used the existing renderer publication path instead of inventing a new one: Stage 8 updates `ViewFrameBindings.shadow_frame_slot`, then `Renderer::RefreshCurrentViewFrameBindings(...)` republishes the current view before Stage 12.
- Implemented the directional shadow depth shader against the existing draw-binding system (`DrawMetadata`, current worlds, masked alpha test) so ShadowService stays inside Vortex’s established stage architecture.

## Verification

- `cmake --build --preset windows-debug --target Oxygen.Vortex.ShadowService.Tests --parallel 4`
  Outcome: passed during Task 1 RED/GREEN verification using the repo’s actual target naming
- `cmake --build --preset windows-debug --target oxygen-vortex Oxygen.Vortex.ShadowService.Tests --parallel 4`
  Outcome: passed
- `ctest --preset test-debug --output-on-failure -R "ShadowService"`
  Outcome: passed
- `rg -n "class ShadowService|cascade_count|technique_flags" src/Oxygen/Vortex/Shadows src/Oxygen/Vortex/Types/ShadowFrameBindings.h`
  Outcome: passed
- `cmake --build --preset windows-debug --target Oxygen.Vortex.ShadowService.Tests Oxygen.Vortex.SceneRendererDeferredCore.Tests Oxygen.Vortex.DrawMetadataEmitter.Tests --parallel 4`
  Outcome: failed in RED as expected before Stage 8 / Stage 12 integration, then passed after Task 2 implementation
- `ctest --preset test-debug --output-on-failure -R "SceneRendererDeferredCoreTest\.DeferredLighting|SceneRendererDeferredCoreTest\.Stage8PublishesDirectionalShadowFrameSlotForDeferredLighting|Oxygen\.Vortex\.SceneRendererDeferredCore\.Tests"`
  Outcome: passed
- `cmake --build --preset windows-debug --target oxygen-vortex oxygen-examples-vortexbasic --parallel 4`
  Outcome: passed, including ShaderBake compiling the new directional shadow depth family into `shaders.bin`
- `powershell -NoProfile -File tools/vortex/Run-VortexBasicRuntimeValidation.ps1 -Output build/artifacts/vortex/phase-4/vortexbasic/04-03`
  Outcome: passed
  Evidence:
  `build/artifacts/vortex/phase-4/vortexbasic/04-03.validation.txt` reports `overall_verdict=pass`, `runtime_exit_code=0`, `d3d12_error_count=0`, and all Stage 3 / Stage 9 / Stage 12 draw and scope checks passing.
- `ctest --preset test-debug --output-on-failure -R "ShadowService|SceneRendererDeferredCore|DrawMetadataEmitter"`
  Outcome: returned non-zero because two pre-existing `Oxygen.Renderer.DrawMetadataEmitter` test registrations point at missing executables; all in-scope Vortex `ShadowService`, `SceneRendererDeferredCore`, and `DrawMetadataEmitter` tests passed.

## Deviations from Plan

### Auto-fixed Issues

None.

### Plan/Branch Adjustment

- The plan’s build commands use `Oxygen.Vortex` / `Oxygen.Examples.VortexBasic`, but this branch exposes the concrete CMake targets as `oxygen-vortex` / `oxygen-examples-vortexbasic`. Verification used the real target names.
- The exact plan CTest regex also matches two out-of-scope `Oxygen.Renderer.DrawMetadataEmitter` registrations whose executables are missing from this build tree. The broad command was still run and documented, then the in-scope Vortex proof surface was rerun with a narrower regex to verify the actual `04-03` behavior. The out-of-scope registration issue was logged in `deferred-items.md`.

## Issues Encountered

- The fake-graphics test backend did not always provide authoritative incoming state for the new directional shadow surface. `DeferredLightPass` was updated to adopt or begin tracking resource state, matching the tolerant state-management pattern already used by the new shadow depth pass.

## Known Stubs

None.

## Next Phase Readiness

- `ShadowService` now provides the truthful Phase 4C baseline that later services and migration work can consume: Stage 8 publication is live, Stage 12 directional shadow use is wired, and VSM/local-light conventional shadows are still deferred.
- `04-05` / Async migration can rely on the current renderer path having directional shadows in the live `VortexBasic` validation surface.
- Residual risk: the broad renderer-wide `DrawMetadataEmitter` CTest registration issue remains out of scope for `04-03` and should be cleaned separately if future plans continue to use that shared regex.

## Self-Check

PASSED

---
*Phase: 04-migration-critical-services-and-first-migration*
*Completed: 2026-04-17*
