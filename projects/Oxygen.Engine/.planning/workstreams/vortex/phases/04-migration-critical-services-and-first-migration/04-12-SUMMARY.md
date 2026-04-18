---
phase: "04"
plan: "12"
subsystem: "async-migration"
tags:
  - vortex
  - async
  - demoshell
  - imgui
  - runtime
  - ui
key_files_modified:
  - Examples/Async/MainModule.cpp
  - Examples/DemoShell/Services/RenderingSettingsService.h
  - Examples/DemoShell/Services/RenderingSettingsService.cpp
  - Examples/DemoShell/UI/RenderingVm.h
  - Examples/DemoShell/UI/RenderingVm.cpp
  - Examples/DemoShell/UI/RenderingPanel.cpp
  - Examples/DemoShell/UI/DemoShellUi.cpp
  - Examples/DemoShell/Test/AsyncVortexMigrationSurface_test.cpp
  - src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Stages/BasePass/BasePassDebugView.hlsl
  - src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp
decisions:
  - "The Async Vortex seam keeps legacy renderer-bound lighting and ground-grid panels disabled."
  - "The Rendering panel is now Vortex-backed, but only for debug visualizations that are actually wired on the live Vortex runtime."
  - "The bounded runtime panel follows the UE-style view/debug-state pattern instead of reviving legacy pipeline-owned controls."
  - "Debug visualization now follows the neutral-tonemap policy already used by UE 5.7 and the legacy ForwardPipeline."
  - "Async scene authored values now exercise roughness/metalness debug views and preserve two-sided behavior when a submesh material is overridden."
---

# 04-12 Summary

## Outcome

`04-12` is locally complete.

The Async/DemoShell seam now exposes a truthful Vortex-backed Rendering panel,
and the follow-up visual fixes keep that panel meaningful: debug modes run
through a neutral post-process path, Async sphere materials carry visible
roughness/metalness variation, and the rotating two-submesh quad keeps correct
normal-mode lighting even when a submesh override material is active.

## What Changed

- kept `enable_renderer_bound_panels = false` on the Async path, but stopped
  using that flag to suppress the Rendering panel
- bound `RenderingSettingsService` to `vortex::Renderer` so the panel can drive
  only the currently wired Vortex debug visualizations
- limited the Vortex-side Rendering panel surface to truthful controls instead
  of exposing legacy pipeline-owned view-mode, wireframe, GPU-debug, or
  atmosphere-blue-noise toggles that are not live on the current runtime
- switched Vortex debug visualization to a neutral Stage 22 policy
  (`ToneMapper::kNone`, fixed exposure, bloom disabled) and skipped non-IBL
  Stage 15 sky/fog work for non-IBL debug views
- seeded Async sphere materials with deterministic roughness/metalness
  variation so those debug modes show real scene content instead of a flat
  scalar
- corrected the two-submesh quad triangle winding to match its authored +Z
  normals
- preserved double-sided behavior when toggling the submesh override material
- updated the Async migration seam regression test to prove the new bounded
  Vortex-backed panel contract plus the new Async debug-scene invariants

## Verification

- `cmake --build --preset windows-debug --target oxygen-vortex oxygen-examples-async Oxygen.Examples.DemoShell.AsyncVortexMigrationSurface.Tests --parallel 4`
- `ctest --preset test-debug --output-on-failure -R "AsyncVortexMigrationSurface|Oxygen.Examples.DemoShell.AsyncVortexMigrationSurface.Tests"`
- `rg -n "CompositionView|get_active_pipeline|oxygen::renderer|Oxygen/Renderer" Examples/Async/MainModule.cpp Examples/Async/main_impl.cpp Examples/DemoShell/DemoShell.h Examples/DemoShell/DemoShell.cpp Examples/DemoShell/Runtime/AppWindow.h Examples/DemoShell/Runtime/AppWindow.cpp`
  - result: no matches
- manual visual confirmation from the user:
  - Rendering panel `Base Color`, `World Normals`, `Roughness`, `Metalness`,
    `Scene Depth Raw`, and `Scene Depth Linear` are acceptable on Async
  - the two-submesh quad now lights correctly in `Normal` mode, including while
    its override material toggles

## Notes

- Before enabling the Vortex-side Rendering panel, the runtime/UI seam was
  checked against UE 5.7's view/debug-state handling in
  `Engine/Public/SceneView.h`, `Engine/Private/PrimitiveDrawingUtils.cpp`,
  `Engine/Private/SceneView.cpp`, and
  `Shaders/Private/DebugViewModePixelShader.usf`.
- The comparison confirmed the truthful fix is a view-state-backed debug
  surface, not a revival of legacy pipeline-owned controls.
- `.planning/workstreams/vortex/phases/04-migration-critical-services-and-first-migration/04-VERIFICATION.md`
  was intentionally not rewritten here; later Phase 4 proof plans still own the
  visible overlay, sky, exposure, and parity verdicts.
