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
- `--point-light <true|false>` and `--spot-light <true|false>`: enable each
  existing scene light independently for forward/deferred contribution checks.
  Both default to enabled.
- `--exposure-proof <none|independent|shared|main-only|pip-only|reordered|source-loss|viewport|lifetime|window-resize|modes>`:
  run a paused, static main/PiP exposure comparison with explicit Auto settings
  and a public remeter at frame 32. PiP has two stops of compensation unless it
  shares main's gain. Shared mode steps main by one stop at frame 44; capture
  frames 42, 43 and 44 to inspect GPU frame sequences 43, 44 and 45.
  These cases use fixed camera inputs and cannot be combined with proof layouts.
- `--proof-wireframe-overlay true`: add overlays to the four-view proof layout
  and cycle their color each frame to check immutable in-flight draw constants.
  Requires `--proof-layout true` or `--aux-proof-layout true`.
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
- Each scene and offscreen view supplies a stable, producer-owned temporal
  handle. Auto exposure therefore adapts across frames instead of restarting
  on every composition update.
- Lit PiP and the two offscreen previews aim at the sample objects' shared
  center with enough field of view to retain all four objects. Offscreen
  viewport origins remain local; screen placement belongs to composition.
- Proof-specific camera/layout/overlay text stays in the demo and tooling code;
  production renderer paths consume the same runtime feature profiles as other
  Vortex callers.

For exposure captures, `tools/vortex/AnalyzeRenderDocMultiViewExposure.py`
exports the composite and each mapped view, reads its GPU P/S/target/meter state,
and compares opaque probes against an independent tone-curve oracle. Run it with
the existing `tools/shadows/Invoke-RenderDocUiAnalysis.ps1` runner. Its arithmetic
verdict is separate from lighting, temporal and visual acceptance; inspect every
pane and the reported nonzero scene probes. The analyzer exports the full-frame
composite before seeking backward through bindless draws, and the capture's
native thumbnail provides a separate presentation reference.

The analyzer also writes a JSON record and each complete pre-composition image.
Capture `independent`, `main-only`, `pip-only` and `reordered` at frame 40, then
run `tools/vortex/Assert-MultiViewExposureEquivalence.py` (Python with Pillow)
with `--family`, `--main`, `--pip`, `--reordered` pointing to those JSON files
and `--output` pointing to the comparison report. It compares scene probes,
gain/meter values and full per-view images; the reordered-composite comparison
excludes the left toolbar/gizmo and top capture FPS label.
The two isolated reports must cover both distinct family views; duplicate inputs
are rejected before comparison.
For shared mode, pass the frame-42/43/44 JSON records to
`tools/vortex/Assert-MultiViewSharedExposure.py` as `--before`, `--step`, and
`--after`, plus `--output`. It verifies owner generations, borrowed-state flags,
the one-stop owner change, and the consumer's exact one-frame delay.

`source-loss` shares main until GPU frame 43, removes main at frame 44, and
resumes game time after frame 46. Capture frames 42, 43, 46 and 51 to observe
GPU frames 43, 44, 47 and 52. Run `Assert-MultiViewSourceLoss.py` (with Pillow)
using `--before`, `--continuity`, `--first`, `--later`, the corresponding
`--first-log`/`--later-log`, and `--output`. It checks image/gain continuity and
the independent high-precision adaptation trajectory using recorded game time.

`AnalyzeRenderDocOverlayConstants.py` checks grid matrices against each draw's
actual ViewConstants and wireframe colors/domain flags against the four-view
overlay fixture. Use `-PassName OverlayFamily` for that fixture, or
`OverlaySourceLoss` for the grid-only lifecycle captures.

`viewport` keeps game time paused while resizing only PiP at GPU frame 44,
insetting its scissor by 96 pixels at frame 48, and restoring its original
rectangle at frame 52. Capture frames 42, 43, 47 and 51. Analyze each with
`AnalyzeRenderDocMultiViewExposure.py` and `AnalyzeRenderDocMeterRectangles.py`.
Name the reports `<prefix>-<capture-frame>.exposure.txt` and `.meter.txt`, then
run `Assert-MultiViewViewport.py --directory <reports> --prefix <prefix>
--output <result.json>` with Pillow available. It checks retained gain/generations,
main-view isolation, matching raster/meter rectangles, untouched pixels outside
the scissor, and exact restoration of PiP. The tonemap oracle checks only pixels
inside the actual draw scissor; this case preserves prior pixels outside it.

`lifetime` hides PiP on GPU frames 44–47 without destroying its registration,
reopens it at frame 48 with one extra stop of requested compensation, then
creates a new logical view/handle at frame 52. Game time stays paused. Capture
frames 42, 43, 47 and 51 and run `Assert-MultiViewLifetime.py --directory <reports>
--prefix <prefix> --output <result.json>` with Pillow. The retained view must
preserve its old gain/image; the new view must initialize from its own target.

`window-resize` changes the native window to 1280x800 after GPU frame 44 and
restores its original size after frame 52. Capture frames 42, 48 and 56, then
run `Assert-MultiViewWindowResize.py --directory <reports> --prefix <prefix>
--output <result.json>` with Pillow. It verifies actual swapchain/view extents,
retained exposure histories and exact restored images. Headless and fullscreen
operation are rejected for this proof.

`modes` changes only PiP while game time stays paused: wireframe at GPU frame
44, restored Auto at 48, Manual EV4 at 52, disabled exposure at 56, physical
camera at 60, zero Auto target at 64, positive target at 68, EV6 seed at 72,
and camera cut at 76. Capture frames 42, 43, 47, 51, 55, 59, 63, 67, 71 and 75.
Run `Assert-MultiViewModes.py --directory <reports> --prefix <prefix>
--output <result.json>` with Pillow to check immediate gains, retained latent
history, event generations, diagnostic restoration and main-view isolation.

For the offscreen forward light-binding regression, capture the existing
`--offscreen-proof-layout true --pip-wireframe false` layout with both lights,
then with each light individually, and finally with both disabled. Use the
point/spot switches above. Analyze each capture with
`tools/vortex/AnalyzeRenderDocForwardLocalLights.py`, using the existing runner's
`-PassName ForwardLocalBoth`, `ForwardLocalPoint`, `ForwardLocalSpot`, or
`ForwardLocalNone` respectively. The audit checks the six-float4 payload,
canonical light count/kinds/flags, consumed grid ranges/indices, and nonzero
forward SceneColor. The disabled case requires zero scene radiance. These are
binding/contribution checks; calibrated forward/deferred brightness and shadow
parity require their own acceptance cases.
