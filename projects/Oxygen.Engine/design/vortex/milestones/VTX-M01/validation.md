# VTX-M01 — validation

This record preserves the conditions and results at each implementation checkpoint.
The milestone README owns the final outcome and remaining work.

## Validation and remaining work

**Qualification:** `validated`

Vortex module, Renderer Core, publication, upload/resource substrate, SceneRenderer shell, SceneTextures, non-runtime facades, resolve/cleanup, and related tests are present and freshly validated. Build proof passed `cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.LinkTest Oxygen.Vortex.RendererCapability.Tests Oxygen.Vortex.RenderContext.Tests Oxygen.Vortex.SceneRendererShell.Tests Oxygen.Vortex.SceneTextures.Tests Oxygen.Vortex.SceneRendererPublication.Tests Oxygen.Vortex.RenderContextMaterializer.Tests Oxygen.Vortex.RendererFacadePresets.Tests Oxygen.Vortex.RenderGraphHarnessFacade.Tests Oxygen.Vortex.SinglePassHarnessFacade.Tests Oxygen.Vortex.UploadCoordinator.Tests Oxygen.Vortex.ViewConstantsManager.Tests oxygen-examples-vortexbasic --parallel 4`. Focused CTest passed the corresponding Vortex substrate/facade suites plus `Oxygen.Vortex.LinkTest`. Runtime proof `tools\vortex\Run-VortexBasicRuntimeValidation.ps1 -Output out\build-ninja\analysis\vortex\m01-m03-closeout\vortexbasic-foundation -Frame 3 -RunFrames 6 -Fps 10 -BuildJobs 4` passed overall with runtime exit 0, final present nonzero, CDB/debug-layer `overall_verdict=pass`, no D3D12/DXGI errors, and no blocking warnings.

**Remaining work:** No open VTX-M01 closure gap.
