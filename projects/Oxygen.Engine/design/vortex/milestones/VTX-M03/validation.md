# VTX-M03 — validation

This record preserves the conditions and results at each implementation checkpoint.
The milestone README owns the final outcome and remaining work.

## Validation and remaining work

**Qualification:** `validated`

LightingService, ShadowService baseline, PostProcessService, Stage 8/12/22 routing, and focused tests are present and freshly validated. Build proof passed `Oxygen.Vortex.LightingService.Tests`, `Oxygen.Vortex.ShadowService.Tests`, `Oxygen.Vortex.PostProcessService.Tests`, `Oxygen.Vortex.SceneRendererDeferredCore.Tests`, shader bake/catalog targets, and `oxygen-examples-vortexbasic`. CTest passed `Oxygen.Vortex.LightingService.Tests`, `Oxygen.Vortex.ShadowService.Tests`, `Oxygen.Vortex.PostProcessService.Tests`, `Oxygen.Vortex.SceneRendererDeferredCore.Tests`, and `Oxygen.Graphics.Direct3D12.ShaderBakeCatalog.Tests`. Runtime proof `vortexbasic-foundation.validation.txt` passed with Stage 12 deferred-lighting scope, directional/point/spot draw-count proof, nonzero Stage 12 SceneColor for directional/point/spot lighting, compositing present operation count, final present nonzero, CDB/debug-layer `overall_verdict=pass`, no D3D12/DXGI errors, and no blocking warnings. Later milestones validate expanded conventional shadows, diagnostics, translucency, environment, and composition behavior.

**Remaining work:** No open VTX-M03 closure gap.
