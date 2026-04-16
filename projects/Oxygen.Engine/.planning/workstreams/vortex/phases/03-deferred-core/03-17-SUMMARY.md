---
phase: 03-deferred-core
plan: "17"
subsystem: deferred-lighting-and-closeout
tags: [vortex, deferred-core, deferred-lighting, verification]
requires:
  - phase: 03-deferred-core/16
    provides: Real draw-frame publication and Stage 9 base-pass execution
provides:
  - Real Stage 3 depth/partial-velocity rendering
  - Real Stage 12 deferred-light rendering with fullscreen and stencil-bounded local lights
  - Truthful Phase 03 closeout proof rerun
requirements-completed: [DEFR-01, DEFR-02]
completed: 2026-04-15
---

# Phase 03 Plan 17: Deferred Lighting and Closeout Summary

## Outcome

Phase 03 is now truthfully complete. The remediation replaced the remaining
proof-only seams with real render-path execution and reran the repo-owned
closeout proof successfully.

## Key Changes

- `DepthPrepassModule` now records a real Stage 3 pass, binds a depth/velocity
  framebuffer, emits draw calls from prepared-scene metadata, and copies
  `SceneDepth` into `PartialDepth`.
- `BasePassModule` keeps the real Stage 9 base pass and now chooses depth
  testing with the active reverse-Z convention.
- `SceneRenderer::RenderDeferredLighting(...)` now records actual Stage 12
  work:
  - directional fullscreen draws
  - point-light stencil-mark + lighting draws
  - spot-light stencil-mark + lighting draws
  - per-light constant-buffer publication
- The deferred-light HLSL now uses procedural sphere/cone volume generation and
  dedicated stencil-mark pixel entrypoints.
- `FramebufferImpl` now reuses existing RTV/DSV views instead of trying to
  re-register duplicate view descriptions.
- The deferred-core analyzer/runner now checks the real render-path evidence
  from Stage 3/9/12 rather than the older telemetry-oriented strings.

## Verification

- `cmake --build --preset windows-debug --target oxygen-vortex oxygen-graphics-direct3d12 Oxygen.Vortex.SceneRendererDeferredCore.Tests Oxygen.Vortex.SceneRendererPublication.Tests --parallel 4`
- `ctest --test-dir out/build-ninja -C Debug -R "^Oxygen\.Vortex\.(SceneRendererDeferredCore|SceneRendererPublication)\.Tests$" --output-on-failure`
- `powershell -NoProfile -File tools/vortex/Verify-DeferredCoreCloseout.ps1 -Output out/build-ninja/analysis/vortex/deferred-core/frame10`

All three passed.

## Remaining Deferral

Runtime RenderDoc validation stays deferred to Phase 04, where Async and
DemoShell will provide the first truthful Vortex runtime frame surface.
