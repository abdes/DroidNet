# VortexBasic

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
