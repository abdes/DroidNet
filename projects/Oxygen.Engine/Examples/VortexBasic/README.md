# VortexBasic

## Fixed exposure fixture

`--validation-scene exposure-fixed` uses an unlit-by-environment deferred
emissive receiver with exact scene input 4096, key 12.5, compensation zero,
None tone curve, gamma one and bloom disabled. It fills the camera for interior
pixel probes. `--validation-exposure-ev` selects Manual EV100 (default 14).
This is a focused native engine fixture; LightBench owns interactive experiments.
The fixture owns a persistent view-state handle, using the public runtime view
publication API without DemoShell settings reapplication.

From the engine root, build `oxygen-examples-vortexbasic` and
`oxygen-graphics-direct3d12`, then run:

```powershell
$out = 'out/build-ninja/analysis/vortex/exposure-lightbench/fixed-gain'
foreach ($ev in @(14,15,16)) {
  ./out/build-ninja/bin/Debug/Oxygen.Examples.VortexBasic.exe --validation-scene exposure-fixed --validation-exposure-ev $ev --frames 20 --fps 10 --vsync false --debug-layer true --capture-provider renderdoc --capture-load search --capture-from-frame 10 --capture-frame-count 1 --capture-output "$out/ev$ev" -v=-1
  ./tools/shadows/Invoke-RenderDocUiAnalysis.ps1 -CapturePath "$out/ev${ev}_capture.rdc" -UiScriptPath tools/vortex/AnalyzeRenderDocExposureFixed.py -PassName "EV$ev" -ReportPath "$out/ev$ev-analysis.txt" -AnalysisTimeoutSeconds 60
}
```

The analyzer checks the bound production tonemap constants and scene input,
then compares a 4x4 pixel region covering every Bayer phase against independent
0.25/0.125/0.0625 pre-dither expectations and one-code UNorm8 tolerance. It saves
the actual Stage-22 output PNG for inspection. A replay image is identified as
that product, not as a desktop screenshot. The fixture currently qualifies the
fixed deferred-emissive path; it does not establish complete Auto/HDR integration.

The current foundation matrix uses these exact cases. Use the case name as the
analyzer's `-PassName`, the listed scene as `--validation-scene`, and append the
listed arguments to the common launch above. Negative numbers use `=`, as shown.

| Case | Scene | Fixture arguments | Expected gain |
| --- | --- | --- | --- |
| EV14 | exposure-fixed | `--validation-exposure-ev=14` | 2^-14 |
| EV15 | exposure-fixed | `--validation-exposure-ev=15` | 2^-15 |
| EV16 | exposure-fixed | `--validation-exposure-ev=16` | 2^-16 |
| EV32 | exposure-fixed | `--validation-exposure-ev=32` | 2^-32 |
| EV-32 | exposure-fixed | `--validation-exposure-ev=-32` | 2^32 |
| Bias | exposure-fixed | `--validation-exposure-ev=16 --validation-exposure-key=6.25 --validation-exposure-compensation=2` | 2^-15 |
| Disabled | exposure-fixed | `--validation-exposure-enabled=false` | 1 |
| Camera | exposure-fixed | `--validation-camera-exposure=true` | 1/15125, from f/11, 125/s, ISO100 |
| InvalidRetained | exposure-fixed | `--validation-invalid-exposure=true` | 2^-14, retained after an invalid key at frame 8 |
| Auto160 | exposure-locked | `--validation-exposure-ev=160` | 1 |
| Auto-160 | exposure-locked | `--validation-exposure-ev=-160` | 1 |
| AutoCurveLocked | exposure-curve-cancellation | Defaults | 2 |

The locked fixtures use exact input 0.25. `exposure-locked` sets compensation
equal to its locked EV bound. The curve case locks EV0, sets compensation 1e20,
constant curve compensation -1e20, and key 25; the one-stop key bias must survive
cancellation. These qualify arithmetic without asset-readiness/temporal settling
confounding the result. Ordinary Auto trajectories and robust histogram behavior
are separate qualification cases.

PixelHistory additionally checks actual float shader output before UNorm8
storage at a zero-dither Bayer phase. EV32 intentionally stores black, while the
float result remains 2^-20 (approximately 9.53674e-7). EV-32 and Disabled
intentionally clip white. The Auto cases verify the bound 80-byte state,
settings/frame identity and the consumed 560-byte normalized target record.

For a separate CDB debug-layer audit use the same scene arguments with
`--capture-provider off` and normal log verbosity, then
`tools/vortex/Assert-VortexBasicDebugLayerAudit.ps1`. Do not pass `-v=-1` for
that audit: its existing exit-code check requires the runtime exit log.

For example, in a Visual Studio Developer PowerShell from the engine root:

```powershell
cmake --build out/build-ninja --config Debug --target Oxygen.Scene.ExposureSettings.Tests Oxygen.Vortex.RendererPublicationSplit.Tests Oxygen.Vortex.PostProcessService Oxygen.Vortex.SceneRendererDeferredCore oxygen-examples-vortexbasic oxygen-graphics-direct3d12 Oxygen.Graphics.Direct3D12.ShaderBakeCatalog.Tests --parallel 4
ctest --preset test-debug -R 'ExposureSettings|PostProcessService|SceneRendererDeferredCore|RuntimeViewPublication|ShaderBakeCatalog' --output-on-failure

$out = (Resolve-Path 'out/build-ninja/analysis/vortex/exposure-lightbench/fixed-gain').Path
[IO.File]::WriteAllText("$out/debug.commands.txt", "g`nq`n", [Text.Encoding]::ASCII)
& 'C:/Program Files (x86)/Windows Kits/10/Debuggers/x64/cdb.exe' -G -g -logo "$out/ev14-debug.cdb.log" -cf "$out/debug.commands.txt" ./out/build-ninja/bin/Debug/Oxygen.Examples.VortexBasic.exe --validation-scene exposure-fixed --validation-exposure-ev=14 --frames 20 --fps 10 --vsync false --debug-layer true --capture-provider off *> "$out/ev14-debug.stdout.log"
./tools/vortex/Assert-VortexBasicDebugLayerAudit.ps1 -DebuggerLogPath "$out/ev14-debug.cdb.log" -RuntimeLogPath "$out/ev14-debug.stdout.log" -ReportPath "$out/ev14-debug-report.txt"
```

The evidence manifest identifies the 12-case captures, numeric reports,
float probes, inspected image groups, debug audits, binaries/shaders and source
snapshot. `autocurve-unsettled*` retains an earlier ordinary-Auto settling failure
for diagnosis and is excluded from the arithmetic verdicts.

VortexBasic renders procedural assets through the native D3D12 Vortex renderer.
The default scene retains the animated cube, environment, and optional feature
probes. The `sidedness` scene provides a deterministic material-culling chart
without cooked content, an editor process, or persisted scene selection.

## Sidedness validation

From `projects/Oxygen.Engine`, after building `oxygen-examples-vortexbasic`:

```powershell
.\out\build-ninja\bin\Debug\Oxygen.Examples.VortexBasic.exe `
  --validation-scene sidedness --shading-path deferred --fps 30
```

Run again with `--shading-path forward` for the other Vortex base pass. A fixed
camera, manual exposure, white directional light, gray receivers, and disabled
atmosphere/fog keep the results comparable. The window title identifies the path.
The key light uses the primary sun slot because the current Vortex directional
selection publishes that slot; atmosphere rendering remains disabled.
Use `--frames 120` for a bounded run; omit it for native window inspection.

The chart has three rows: **opaque red**, **masked green**, and **translucent blue**.
The masked material has constant alpha 1 and cutoff 0.5, exercising the masked
pass variants without a texture. The blue material has alpha 0.55. Columns run
left to right:

| Column | Triangle | Expected |
| --- | --- | --- |
| 1 | Single-sided front | Lit |
| 2 | Single-sided back | Absent; gray receiver visible |
| 3 | Double-sided front | Lit |
| 4 | Double-sided back | Lit with the backface normal reversed |
| 5 | Mirrored single-sided front | Lit; reversed silhouette |
| 6 | Mirrored single-sided back | Absent; gray receiver visible |
| 7 | Mirrored double-sided front | Lit; reversed silhouette |
| 8 | Mirrored double-sided back | Lit with the backface normal reversed |
| 9 | Single-sided child of a mirrored parent | Lit; reversed silhouette |
| 10 | Mirrored child of a mirrored parent | Lit; original silhouette |

All single-sided columns in a row share one geometry/material pair; double-sided
columns share another. The asymmetrical triangle makes mirroring observable.
Columns 2 and 6 must remain empty in **all three rows**. Gray receiver panels make
unexpected black backfaces or depth-only silhouettes visible. The directional
light also exercises shadow-map culling for the opaque and masked rows against
these receivers: rejected single-sided backs must not cast their old shadow
silhouette. The transparent row does not participate in shadow casting.

The bottom row contains a single-sided cube, mirrored cube, sphere, and mirrored
sphere, all using the same gold material. Both members of each pair must retain
their visible exteriors and coherent lighting.

### Captures and diagnostics

The standard capture CLI is supported:

```powershell
.\out\build-ninja\bin\Debug\Oxygen.Examples.VortexBasic.exe `
  --validation-scene sidedness --shading-path deferred --frames 120 --fps 30 `
  --capture-provider renderdoc --capture-load search `
  --capture-from-frame 60 --capture-frame-count 1 `
  --capture-output .\out\build-ninja\analysis\sidedness\deferred
```

`--validation-motion true` translates the chart using the engine frame sequence
number, permitting reproducible depth/velocity capture inspection. Static mode
is the default for screenshots. `--shader-debug-mode world-normals` and
`--shader-debug-mode scene-depth-linear` provide additional deferred diagnostics.
The chart uses the renderer's normal depth-prepass policy; disabled-prepass
coverage belongs to the focused engine pass tests.

### Normal-map handedness

Add `--validation-normal-map true` to apply a constant tangent-space normal of
approximately `(0, 0.6, 0.8)` to the triangle materials. The example cooks this
one-pixel linear texture through `CookScratchImage` with the D3D12 packing policy,
then loads and pins it through `IAssetLoader`. Texture sampling and material
binding use the ordinary renderer path.

Compare columns 1/5/9/10 (single-sided fronts), 3/7 (double-sided fronts), and
4/8 (double-sided backs). Mirroring X must preserve the normal's world Z tilt
within each group. Fronts tilt upward; double-sided backs tilt downward after
the normal is reversed. Each pair must agree in both deferred and forward runs.
The deferred `world-normals` debug view provides a direct comparison without
lighting/shadow differences. Opaque and masked rows participate in that GBuffer
debug view; the translucent row uses forward shading.

This scene is a validation instrument. Its presence does not itself certify a
renderer fix; record the built revision, path, capture, and observed result.
