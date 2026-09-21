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
- `--exposure-proof <none|independent|shared|main-only|pip-only|reordered|source-loss|viewport|lifetime|window-resize|modes|atmosphere|atmosphere-lit|consumer-visual>`:
  run a paused, static main/PiP exposure comparison with explicit Auto settings
  and a public remeter at frame 32. PiP has two stops of compensation unless it
  shares main's gain. Shared mode steps main by one stop at frame 44; capture
  frames 42, 43 and 44 to inspect GPU frame sequences 43, 44 and 45.
  These cases use fixed camera inputs and cannot be combined with proof layouts.
  The two atmosphere cases use their own recipes described below.
- `--exposure-proof layouts`: use the existing sunlit scene with volumetric fog
  and Auto exposure in ordinary, standard, auxiliary, offscreen or feature
  layouts. It preserves wireframe and feature diagnostics and uses a lit
  auxiliary color producer. Most panes use Spot; NoEnvironment and the
  offscreen preview/capture use Average. Their central shadows would make
  Spot metering overexpose the rest of the image. Simulation is paused and
  registered/offscreen owners remeter at frame 32, so capture frame 40 for the
  static comparison.
- `--exposure-view-only "<view name>"`: with `layouts`, keep the named view and
  its auxiliary producers for an isolated comparison at the same viewport and
  camera settings. For example, standard layout uses `M06A.LitPerspective`;
  offscreen products use `M06B.OffscreenPreview.Deferred` and
  `M06B.OffscreenCapture.Forward`. An empty selection renders the full family.

Layout comparison reports include stable render-target names. Run
`python tools/vortex/Assert-MultiViewLayoutExposure.py --family <report.json>
--cases <cases.json> --output <result.json>`. Each entry in the JSON `cases`
array specifies `report`, `selected_target`, and `required_producers`; the
captured target set must match exactly. Every mapped family view needs its own
selection, including producers also required by another case. Optional
`auxiliary_copies` entries specify `producer`, `consumer`, and pixel `extent`
to verify the mapped copy into the consumer's top-left inset. The current
four-pane layout at 2560x1400 uses a 256x233 inset.

- `--exposure-proof mixed`: render the existing meshes as an opaque sphere,
  emissive cube, masked cone and translucent cylinder under the daylight/fog
  recipe. It uses Average metering and supports the layout and named-view
  controls above. Each view, including offscreen products, owns its exposure
  recipe (key 12.5, adaptation speeds 3 EV/s up and 1 EV/s down), so saved
  DemoShell settings cannot replace it. Use `--pip-wireframe false` for lit PiP
  validation.
- `--exposure-proof interactions`: run the same mixed scene through the bounded
  sequence below in ordinary or offscreen layout. Use a windowed run of at
  least 144 frames. The overlay identifies the phase and material domains;
  `Vortex.MultiView.Interactions` logs the actual game delta used by the renderer.

| Engine frame | Interaction                                      |
| ------------ | ------------------------------------------------ |
| 32           | Remeter after resources are resident             |
| 40           | Unpause and begin camera motion                  |
| 44–51        | PiP compensation +0.5 EV                         |
| 52–59        | PiP Manual EV14.5                                |
| 56–95        | PiP uses the forward path                        |
| 60           | PiP returns to Auto                              |
| 64 / 68      | PiP seed EV15 / camera cut                       |
| 72–79        | Reverse submission order                         |
| 76–83        | Resize PiP; inset scissor at 80–83               |
| 88 / 96      | Resize window to 1280x800 / restore              |
| 100–103      | Hide PiP while retaining its state               |
| 104 / 108    | Reopen PiP / create a fresh PiP identity         |
| 112–119      | PiP borrows main exposure; seed the owner at 116 |
| 120–127      | Remove the owner; PiP continues independently    |
| 128          | Recreate the main view                           |
| 136          | Pause the final state                            |

Capture indices are zero-based: capture index 43 records engine frame 44.
Capture overhead changes actual dt; response validation must use the captured
solver dt, not assume the requested `--fps` was achieved.

For numerical interaction checks, analyze captures with
`tools/vortex/AnalyzeRenderDocExposureTransitions.py` through
`tools/shadows/Invoke-RenderDocUiAnalysis.ps1`. Set
`OXYGEN_RENDERDOC_EXPOSURE_FRAMES` to the comma-separated engine frames to
inspect. The analyzer checks histogram reduction, target interpolation, the
actual adaptation timestep and descriptor-identified shared history. The
`Vortex.PostProcess.Exposure.Solve` marker permits direct selection of each
solve. Feed the phase reports to `Assert-MultiViewExposureInteractions.py
--reports <reports...> --output <result.json>` for the complete event timeline,
including prior-owner latency and source-loss continuity.

For mixed materials, use `AnalyzeRenderDocMixedMaterials.py` on one static
four-view capture, followed by `Assert-MultiViewMixedMaterials.py --report
<report.json> --output <result.json>`. It checks opaque/masked base colors,
masked coverage, scene-referred emission and the translucent pass contribution.
Read raw intermediate textures at the end marker following the last draw;
the draw event itself may expose pre-draw contents. Compare each named view
against its isolated capture with `Assert-MultiViewLayoutExposure.py` as above.

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

For the EX05-15 human visual checkpoint, launch from the engine directory:

```powershell
./out/build-ninja/bin/Debug/Oxygen.Examples.MultiView.exe --exposure-proof consumer-visual --visual-fog volume --pip-wireframe false --fps 60 -v=-1
```

`tools/vortex/Show-ExposureVisualCheck.ps1` launches the same check from any
working directory; `-Fog clear` or `-Fog local` selects its initial choice.
Use `-Configuration Release` after building that configuration to select it
instead of Debug.
`--visual-fog-jitter false` is a fixture-only diagnostic control; the normal
visual checkpoint keeps jitter enabled.

The small overlay switches the same original meshes/materials between **Clear
(reference)**, **Fog**, and **Local fog**. Cameras, lighting and exposure remain
fixed. EV15 gives an exact gain of `1/32768`; there is no AE settling ambiguity in
this A/B comparison. Automatic precision selection is per view and retains FP32
when the current products or temporal history cannot meet the FP16 error budget.
Use `--visual-fog clear` or `--visual-fog local` for matching scripted captures.
Allow volumetric history to settle after switching; qualification captures use
frame 96 or later.

Look for recognizable green/blue/red materials in both views, a smooth depth
veil in **Fog**, and a localized patch around the left sphere in **Local fog**.
The local volume extends onto the ground in front of the sphere so both camera
angles show its contribution. Object boundaries should remain intact, and
returning to **Clear** should restore the reference without a brightness jump
caused by exposure adaptation. Keep any feedback tied to the selected fog choice
and view. The original scene, ground surface and point/spot lights are retained;
the editor grid is hidden in this checkpoint.

The visual recipe deliberately uses noticeable fog: base extinction is
`0.06 m^-1`; the local patch uses radial/height extinction `0.9/0.45` with a
fixture-only `0.1 m` start distance and a 2.25 m horizontal radius centered at
`(-1.75, -0.25, 0.3)`. These are demonstration settings; the
engine defaults and saved console preferences are unchanged.

The daylight framing includes a visible procedural sky, an extended ground plane
and the authored 110,000-lux sun with shadows. Volumetric fog uses the existing
distant-sky ambient LUT. Captured-scene surface IBL is not supplied by this
checkpoint; this does not add a sky-capture implementation.

The bounded scripted round-trip uses `--visual-fog-cycle true`: it starts clear,
requests Fog at GPU frame 32, Local fog at 96, and Clear at 128 through the same
queued state-change path as the radio buttons. Capture CLI frames 30/94/126/158
to inspect clear/settled fog/local/returned clear. This checks state transitions;
the human review also checks the mouse-operated controls.
For longer transition diagnostics, `--visual-fog-hold-local true` runs the same
cycle through Fog and Local, then leaves Local active instead of returning to
Clear at frame 128. It does not change the rendering or exposure settings.

The image checker measures red/green/blue inside fixed interior regions of the
cone, sphere and cylinder. Sky/background pixels do not establish material
visibility. Its negative controls remove each material region while preserving
the blue sky.

`atmosphere-lit` retains the original sphere, cube, cylinder, cone, ground and
their non-emissive materials. It adds an authored 110,000-lux sun and atmosphere
to the ordinary point/spot setup, with independent Auto exposure (key 12.5,
zero compensation) in main and lit PiP. Poses are paused and the existing
frame-32 remeter event initializes the bright scene without elapsed simulation
time. Capture frame 42 for Average metering. At GPU frame 48, both views switch
to the existing Spot profile (radius 0.2) and explicitly remeter; capture frame
54 for that result. Geometry, materials, lights, cameras and display mapping
remain identical. Average includes the dark background and is retained as the
washout diagnostic, not a readability pass. This is the lit-material case;
the separate emissive-card fixture below does not qualify it.
Use `AnalyzeRenderDocLitReadability.py` with `-PassName LitReadability`, then,
with NumPy installed, run
`Assert-MultiViewLitReadability.py --inputs <spot-report.json> --average
<average-report.json> --output <result.json>`.
For each view, the checker requires the original green, blue and red G-buffer
materials on at least 100 pixels each, no base-pass emission, the authored sun
and agreement with the measured Auto target within 0.1%. Each material must have
less than 1% near-white output (all RGB codes >=250) and more than 5% colored
output (channel spread >8). Synthetic white outputs must fail. Inspect the
presented composite as well; these checks do not qualify physical-light units
or the complete MultiView layout matrix.
The paired check also requires identical HDR inputs, exact current/target gain
agreement after each remeter, and an actual readability failure in the Average
main view. This separates the chosen meter target from adaptation time.

`atmosphere` fixes both views at Manual EV2, key 12.5, compensation zero,
with paused poses and emissive cards on the lit shader path. AP starts at
zero distance, uses scattering strength 0.01 and an authored 1000-lux sun.
The proof disables the standard point/spot lights and replaces the sample meshes
with cards facing -Y. Both cameras are in front (-Y); the sun is behind (+Y),
so visible card normals have nonpositive N·L. No authored specular-factor switch
is used to claim isolation. The background card stays opaque throughout.
DemoShell uses the existing scene-authored environment mode for this scenario,
so its override profile cannot replace the proof sun or atmosphere settings.
The complete initial recipe is staged before scene publication, including the
sun, so paused execution starts with coherent environment inputs.
GPU frames before 44 use opaque deferred cards; frames 44–47 use the same
four foreground cards as alpha-one forward materials; from frame 48 the first
card is half-alpha forward and the others are opaque.
Frames 52–55 restore opaque deferred cards at scattering strength 8; from frame
56 the same cards use the opaque forward base pass. Capture frames 54 and 58
(GPU frames 55 and 59) for this stronger AP comparison. Opaque/masked forward
base passes must leave AP to Stage 15; only later translucency applies it inline.
Capture frames 42, 46 and 50 (GPU frames 43, 47 and 51). Analyze each with
`AnalyzeRenderDocMultiViewExposure.py`, then
`AnalyzeRenderDocMultiViewAtmosphere.py` using `-PassName ApDeferred`,
`ApForward` and `ApMixed`, respectively. Use matching report prefixes ending in
`.exposure.txt` and `.ap.txt`. Run `Assert-MultiViewAtmosphere.py --deferred
<frame42.ap.json> --forward <frame46.ap.json> --mixed <frame50.ap.json> --output
<result.json>` with NumPy and Pillow. It compares every HDR pixel at the frozen
0.5% relative plus `1e-5` absolute budget. Passing the ordinary exposure analyzer
alone does not establish this comparison. Inspect all three presented phases.
For the opaque pair, use `-PassName ApOpaqueReference` and `ApOpaqueForward`,
then add `--opaque-reference <frame54.ap.json> --opaque-forward <frame58.ap.json>`
to the comparison command. It also rejects inline AP reads in the opaque forward
base pass. Inspect both additional presented phases.
The comparison also requires a measured AP contribution on at least 100 pixels
per view/phase. On AP-affected geometry, fewer than 1% of mapped pixels may be
near-white in all RGB channels (codes >=250), and at least 5% must retain a
channel spread above eight codes. These readability checks supplement the raw
float comparison and native image inspection.
The grid is a diagnostic overlay using opaque depth; it may cross forward cards
that do not write depth. The raw scene comparison precedes that overlay and does
not claim identical final grid occlusion between material domains.
