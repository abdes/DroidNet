# VX-IBL-01 validation

S1 is validated: common GPU products, BRDF publication, shared surface evaluation,
Nexus-backed resource reuse and independent numerical references pass their
native checks. S2–S6 remain open.

Read: [current results](#current-results), [reproduce](#reproduce),
[remaining gates](README.md#acceptance).

## Current results

[Latest S1 run](evidence/s1-reuse/run.json): 23 IBL tests pass. The preceding
publication checkpoint passed 75 resource-registry and 63 environment tests. The shared evaluator covers
independent diffuse/specular gains, occlusion, HDR scale, grazing views,
near-black metals and stale generations. Recorded readers retain BRDF and cube
products after their CPU owners are released.

![Native GPU material fixture](evidence/s1-surface/materials.png)

The fixture evaluates analytic sphere normals through the production IBL helper
on D3D12. [Linear pixels](evidence/s1-surface/materials.pfm) precede the preview's
Reinhard/sRGB display transform. Production scene-pass integration is S2.

| Check                                             | Result                                                                                                      | Evidence                                                                                           |
| ------------------------------------------------- | ----------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------- |
| Release build and shader archive                  | Passed                                                                                                      | [Build log](evidence/s1-reuse/build.log)                                                           |
| Native IBL products, reuse and surface evaluation | 23/23 passed                                                                                                | [Tests](evidence/s1-reuse/tests.xml)                                                               |
| Managed raw/bindless views and cache identity     | 75/75 passed                                                                                                | [Registry tests](evidence/s1-surface/registry.xml)                                                 |
| Environment regression                            | 63/63 passed                                                                                                | [Tests](evidence/s1-surface/environment.xml)                                                       |
| Surface RenderDoc replay                          | 16 producer dispatches, 16,368 product values and 216 surface values passed                                 | [Capture](evidence/s1-reuse/surface.rdc) · [Report](evidence/s1-reuse/renderdoc.json)              |
| 128-face cube checkpoint                          | 22 dispatches, 1,048,560 product values passed; metadata transitions invalid → finite/incomplete → complete | [Capture](evidence/s1-convolution/constant.rdc) · [Report](evidence/s1-convolution/renderdoc.json) |

The earlier [cube checkpoint](evidence/s1-convolution/run.json) covers full mip
chains, four-child reduction, hemisphere orientation, invalid-source rejection,
shader catalog and standalone-header checks.

Nexus retirement tests reuse one physical resource bundle across twelve
successive generations. Live owners and unsubmitted readers prevent reuse;
discard releases the pending reader, and retained products survive pool closure.
Dimension changes replace a free slot's storage. No retirement tickets remain
pending or abandoned after the repeated-use test.

The independent GGX check covers 306 scalar values across faces, mip levels,
centers, edges and corners. CPU code computes double-precision sample directions,
PDFs, footprint LODs and weights; the GPU supplies seamless cube sampling only.
Matching 32/64-sample results pass `0.001 * max(1, reference)` including FP16
storage. Against 1,024 samples, the smooth-plus-directional 5.2-peak fixture has
**0.6274% peak error and 0.0878% RMS**, within its 5%/1% quality bounds.
[Reference results](evidence/s1-reuse/reference.xml) retain the measured values.

The lookup check exercises every texel and all four domain corners through the
production HLSL sampler. Quantized texel reads must agree within one UNorm16 unit.
The separate double-precision 256×256 halfway-vector quadrature checks twenty
view/roughness points against the prescribed 128-sample generator, with absolute
A/B error at most 0.035. This estimator check does not replace material-image
qualification.

Remaining qualification: atmosphere/height-fog capture and production integration
(S2), capture quotas and full fault/lifetime stress (S3), incremental scheduling
(S4), DemoShell/editor workflows (S5), and integrated Tracy/native performance
acceptance (S6).

## Reproduce

From the repository root in a Visual Studio developer shell:

```powershell
cmake --build projects/Oxygen.Engine/out/build-ninja --config Release --target Oxygen.Vortex.LightingGpuAbi.Tests Oxygen.Vortex.EnvironmentLightingService.Tests --parallel 8
& projects/Oxygen.Engine/out/build-ninja/bin/Release/Oxygen.Vortex.LightingGpuAbi.Tests.exe '--gtest_filter=*Ibl*'
& projects/Oxygen.Engine/out/build-ninja/bin/Release/Oxygen.Vortex.EnvironmentLightingService.Tests.exe
```

The [run record](evidence/s1-foundation/run.json) identifies the source hashes,
build outputs and standalone-header command. The [milestone plan](README.md)
owns slice state and the complete acceptance list.

To capture and replay the constant-cube producer test:

```powershell
$env:OXYGEN_IBL_CAPTURE = 'H:/projects/DroidNet/projects/Oxygen.Engine/out/build-ninja/ibl-capture/constant'
& projects/Oxygen.Engine/out/build-ninja/bin/Release/Oxygen.Vortex.LightingGpuAbi.Tests.exe '--gtest_filter=IblConvolutionGpuTest.ConstantCubePreservesEveryFaceAndMip'
Remove-Item Env:OXYGEN_IBL_CAPTURE
& projects/Oxygen.Engine/tools/shadows/Invoke-RenderDocUiAnalysis.ps1 -CapturePath out/build-ninja/ibl-capture/constant_capture.rdc -UiScriptPath tools/vortex/AnalyzeRenderDocIblProducts.py -PassName IblProducts -ReportPath out/build-ninja/ibl-capture/products.txt
```

For the surface capture, use the same commands with test filter
`IblConvolutionGpuTest.SurfaceEvaluationMatchesDiffuseAndSplitSumReference` and
analyzer `-PassName IblSurface`. Set `OXYGEN_IBL_IMAGE` to an absolute `.pfm` path
when running `IblConvolutionGpuTest.OffscreenDielectricAndMetalRoughnessImage` to
export the linear samples. The retained
[plot script](evidence/s1-surface/render-materials.py) takes that PFM path and
writes the PNG preview beside it.
