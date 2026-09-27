# Async demo

A compact AsyncEngine scene showing frame phases, animated geometry and Vortex
lighting. Start with [running](#run), [showcases](#showcases) or
[validation](#validation).

## Run

From `projects/Oxygen.Engine`, build the configured tree and launch:

```powershell
cmake --build out/build-ninja --config Release --target oxygen-examples-async
./out/build-ninja/bin/Release/Oxygen.Examples.Async.exe --resolution 1280x720
```

Use `--frames 120 --fps 30 --vsync false` for a bounded run, `--headless` for
execution without a visible window, and `--help` for capture/debug-layer options.
Camera and environment preferences persist in this demo's `demo_settings.json`.

## Showcases

- **Async phases:** the Async Demo panel shows actions and CPU callback durations
  from the previous completed frame. Renderer CPU/GPU timings live in Diagnostics.
- **Animated scene:** a separated 4×4 opaque material grid varies roughness along
  +X and metalness along +Y. Four translucent dielectric spheres orbit outside
  it. All spheres use two distance-selected LODs. A two-submesh quad
  demonstrates independent visibility and material overrides. Pause with
  **Animate scene** to inspect lighting without moving geometry.
- **Camera:** fly/orbit controls and a figure-eight drone path are available in
  Camera Controls. Startup preserves the saved mode and speed.
- **Lighting:** a physical sun drives atmosphere and captured-sky IBL. Height fog
  takes its ambient contribution from the atmosphere; the authored defaults add
  no constant fog luminance or lower-hemisphere fill.
- **Spotlight:** a camera-mounted 3,000 lm light with 60 m range starts with shadows enabled. Its lateral
  offset makes cast shadows visible beside their occluders. Controls use lumens,
  metres and cone angles; saved settings take precedence over defaults.

To isolate spotlight shadows, pause animation, disable the sun in Environment,
then compare **Cast Shadows** on/off in Async Demo. Use manual exposure for a
fixed comparison; automatic exposure intentionally adapts to reduced lighting.
Opaque spheres cast shadows; alpha-blended spheres demonstrate transparency and
are not opaque shadow casters. Existing saved fog/sky colors can still contribute
light after the sun is disabled.

## Integration

`SceneObserverSyncModule` publishes light/transform changes to scene observers
after gameplay and before rendering. Without it, disabling the sun leaves stale
atmosphere and IBL lighting.

`AsyncScene` owns procedural content and animation. `MainModule` orchestrates
frame phases and supplies a `CompositionView` to DemoModuleBase.
The shared runtime owns HDR scene targets, tonemapped composition, GPU-safe target
retirement and camera resolution. A persistent view-state identity retains
exposure history. DemoShell applies rendering, fog, postprocess and grid controls
through its normal main-view contract.

`AsyncDemoVm` exposes scene controls; `AsyncDemoSettingsService` persists user
choices. Frame-phase tracking is demo instrumentation, not a replacement for Tracy.

## Validation

With `OXYGEN_BUILD_UI_TESTS=ON`, the `async/lighting` UI test checks animation
pause/resume, retained camera lens edits, applied exposure, the real sun checkbox
and spotlight toggles.
It requires isolated settings and optionally records RenderDoc captures of
`daylight`, `spot-shadowed`, `spot-unshadowed` and `lights-off`:

```powershell
$env:OXYGEN_UI_TEST_OUTPUT = "$PWD/out/async-lighting"
$env:OXYGEN_UI_TEST_SETTINGS = "$PWD/out/async-lighting/settings.json"
$env:OXYGEN_UI_TEST_FILTER = "lighting"
./out/build-ninja/bin/Release/Oxygen.Examples.Async.exe --frames 600 --fps 30 `
  --resolution 1280x720 --vsync false --debug-layer true `
  --capture-provider renderdoc --capture-load path `
  --capture-library "C:/Program Files/RenderDoc/renderdoc.dll"
```

Create the output directory and seed `settings.json` with `{}` for authored
lighting defaults. Never point test settings at the interactive settings file.
Run `tools/vortex/AnalyzeRenderDocAsyncLighting.py` through
`tools/shadows/Invoke-RenderDocUiAnalysis.ps1` for each capture to export scene
color before the UI overlay and inspect the retained spot depth map. Compare
the shadowed/unshadowed images in RenderDoc; check that lights-off contributes no surface illumination. Use a fixed
camera, animation time and exposure for these comparisons. For performance work,
measure native Release runs with Tracy; RenderDoc replay timing is not a benchmark.

Drone regression tests cover true pause, nearest-route re-entry, vertical
directions and invalid paths. Native-lifetime ownership keeps debug diagnostics
alive until retained D3D12 allocations retire, so shutdown reports run afterward.
