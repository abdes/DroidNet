# MultiView Rendering Example

Production-style Vortex demo for multi-view rendering, runtime surface
composition, offscreen products, auxiliary view dependencies, and
feature-gated runtime variants.

## Features

- Main and picture-in-picture scene views sharing one scene with independent
  cameras, view state, exposure, and composition routes.
- Proof layouts for VTX-M06A multi-view composition, VTX-M06B offscreen
  products, and VTX-M06C feature-gated runtime variants.
- Runtime texture composition layers for offscreen preview/capture products.
- ImGui overlay composition after scene surfaces.
- Vortex-native runtime contracts only; no legacy renderer fallback path.

## Building

```powershell
cmake --build out\build-ninja --config Debug --target oxygen-examples-multiview --parallel 4
```

## Running

```powershell
out\build-ninja\bin\Debug\Oxygen.Examples.MultiView.exe
```

Common validation-oriented options:

- `--pip-wireframe <true|false>`: force the PiP view to wireframe or run the
  full scene-linear path.
- `--pip-scissor-inset <pixels>`: inset the PiP scene scissor before rendering.
- `--proof-layout <true|false>`: run the VTX-M06A multi-view proof layout.
- `--aux-proof-layout <true|false>`: run the VTX-M06A auxiliary
  producer/consumer proof layout.
- `--offscreen-proof-layout <true|false>`: run the VTX-M06B offscreen proof
  layout with visible offscreen preview/capture products.
- `--feature-variant-proof-layout <true|false>`: run the VTX-M06C 3x2 proof
  layout for depth-only, shadow-only, no-environment, no-shadowing,
  no-volumetrics, and diagnostics-only views.

The VTX-M06C layout intentionally marks three cells as `BLACK expected`:
depth-only, shadow-only, and diagnostics-only. Those views publish reduced
products rather than a scene-color image, so black output is the expected
visual result.

## Proof Scripts

The feature-variant closure path is:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\vortex\Run-VortexFeatureVariantValidation.ps1 -Output out\build-ninja\analysis\vortex\m06c-feature-variants -Frame 5 -RunFrames 65 -Fps 30 -BuildJobs 4
```

The wrapper builds the demo, runs a CDB/D3D12 debug-layer audit, captures and
analyzes a RenderDoc frame, verifies runtime records for all six variants, and
checks 60 steady-state frames for scene-texture allocation churn.

## Architecture Notes

- Scene setup lives in the demo; feature selection is passed through
  `CompositionView::ViewFeatureProfile`.
- Per-view execution is serialized through Vortex view-family rendering.
- Scene views share content but not view constants, histories, exposure, or
  product publication state.
- Proof-specific camera/layout/overlay text stays in the demo and tooling code;
  production renderer paths consume the same runtime feature profiles as other
  Vortex callers.
