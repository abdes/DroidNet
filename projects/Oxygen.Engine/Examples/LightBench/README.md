# LightBench Calibration Scenarios

LightBench is a lighting calibration and visual reference sample for Oxygen
Engine. It provides calibrated scene presets featuring dielectric reference
cards, local lights, and varied exposure modes to verify rendering pipeline
photometric accuracy and post-processing.

## Running LightBench

From `projects/Oxygen.Engine`, launch LightBench using `oxyrun.ps1`:

```powershell
./tools/cli/oxyrun.ps1 oxygen-examples-lightbench -BuildTree build-ninja -Config Release -- --resolution 1920x1080 --fps 60
```

- **Build Configuration**: Use `-Config Debug` for debug builds. Add `-NoBuild`
  before `--` to run an existing executable.
- **Direct Preset Launch**: Pass `--preset <preset-id>` after `--`. Available
  presets: `neutral-reference`, `point-falloff`, `spot-cone`,
  `material-lighting`, `auto-adaptation`, `indoor`, `outdoor-daylight`.

---

## Preset Scenarios

Presets can be selected from the top-center preset bar in the viewport.
Selection updates geometry, camera, lights, exposure, and tone mapping at the
next frame boundary. Click **Reset** in the preset bar to restore default
parameters for the active preset.

| Scenario              | Scene & Lighting                                                               | Exposure & Output                                        | Usage & Verification Notes                                                                                     |
| --------------------- | ------------------------------------------------------------------------------ | -------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------- |
| **Neutral Reference** | Three calibrated cards under white 1000-lux directional light                  | Manual EV 8.3808, Tone Mapper: None, Gamma 2.2           | Baseline photometric reference. Adjust EV or compensation, then reset to return to reference state.            |
| **Point Falloff**     | Single gray card with a 1000-lm point light at 2 m distance                    | Fixed EV 2.7290, Tone Mapper: None                       | Use 1 m / 2 m / 4 m distance buttons to verify inverse-square falloff without automatic exposure compensation. |
| **Spot Cone**         | Broad gray receiver with a 1000-lm spot light at 3 m (15°/30° angles)          | Fixed EV 5.4495, Tone Mapper: None                       | Adjust inner/outer cone angles. Concentrating total flux into narrower cones increases central illuminance.    |
| **Material Lighting** | Matte/glossy spheres and floor ground under angled 1000-lux directional light  | Fixed EV 8, Tone Mapper: ACES                            | Evaluates specular highlights and shadow falloff across spherical and planar geometry.                         |
| **Auto Adaptation**   | Cards, spheres, and ground under balanced directional lighting                 | Auto (Average), Tone Mapper: ACES (Seed EV 8)            | Use Dim / Normal / Bright options to verify smooth temporal exposure adaptation.                               |
| **Indoor**            | Reference objects with warm 1600-lm point fill and 2500-lm shadow-casting spot | Auto (Center-Weighted), Tone Mapper: ACES (Seed EV 5)    | Controlled indoor environment featuring dual shadow-casting local light sources with warm color temperature.   |
| **Outdoor Daylight**  | Reference objects with 100,000-lux directional daylight and floor shadows      | Auto (Center-Weighted), Tone Mapper: ACES (Seed EV 14.6) | High dynamic range daylight scenario testing exposure adaptation to extreme illuminance levels.                |

---

## Photometric Reference Values

The calibration scene features three rough dielectric cards (**Gray**, **White**,
**Black** from left to right) with linear reflectances of 0.18, 0.90, and 0.02.

Under 1000-lux white directional light, theoretical surface luminance values
are:

| Card      | Linear Reflectance | Theoretical Luminance ($\text{cd/m}^2$) |
| --------- | -----------------: | --------------------------------------: |
| **Gray**  |               0.18 |                               59.997675 |
| **White** |               0.90 |                              286.105538 |
| **Black** |               0.02 |                                9.751484 |

### Standard Camera Reference

- **Manual EV100**: `8.380766`
- **Exposure Calibration Key**: `12.5`
- **Compensation**: `0`
- **Tone Mapper**: `None`
- **Gamma**: `2.2`

The ideal gray-card response is `0.18` before gamma. Native rendering tests
account for material packing, sampling and output encoding.

---

## Controls & Serialization

- **Preset Bar**: Switch scenarios, reset settings, and toggle light-step
  distances. Modified inputs give the bar an amber outline; popup borders stay
  neutral.
- **LightBench Panel**: Control directional illuminance, color, and local light
  parameters.
- **Post Process Panel**: Adjust exposure mode, key, compensation, tone mapping
  curve, and gamma.
- **Camera Panel**: Navigation and orthographic framing controls.
- **Console**: [Validated post-process and preset commands](../DemoShell/Console.md)
  are available in ordinary Debug and Release builds.
- **Save / Load Settings**:
  - Scene settings can be serialized to JSON.
  - The default target path is `Examples/LightBench/lightbench.saved.json`
    (ignored by Git).
  - Shipped reference preset file:
    `Examples/LightBench/demo_settings_indoor.json`.

---

## Native Tests & Verification

Photometric accuracy, settings serialization, and render cache invalidation are
tested via automated native unit tests:

```powershell
# Run Debug tests
./tools/cli/oxyrun.ps1 Oxygen.Examples.LightBench.Tests -BuildTree build-ninja -Config Debug

# Run Release tests
./tools/cli/oxyrun.ps1 Oxygen.Examples.LightBench.Tests -BuildTree build-ninja -Config Release
```

### Test Scope

- **HDR & Exposure Verification**: Validates expected GGX shading and exposure
  calculations for forward and deferred pipelines against theoretical
  references.
- **Settings Serialization**: Tests JSON round-tripping, invalid document
  handling, and parameter constraints.
- **Scene Synchronization**: Verifies light toggles and parameter edits
  invalidate renderer caches correctly.

---

## Manual Verification Checklist

When visually inspecting LightBench:

1. **Preset Navigation**: Confirm each scenario loads its expected geometry,
   lighting, and exposure settings. Click **Reset** to confirm state
   restoration.
2. **Point Falloff**: Verify luminance decreases with distance (1 m > 2 m > 4 m)
   under fixed exposure.
3. **Spot Cone**: Verify inner/outer cone angle edits update footprint size and
   center brightness.
4. **Auto Exposure**: Cycle between Dim, Normal, and Bright presets; verify
   luminance adapts smoothly over time.
5. **Settings Serialization**: Save custom settings, switch scenarios, and load
   the saved file to verify settings restoration.

## Widget Regression Tests

The Test Engine suite runs the actual app's controls in Debug and Release:
numeric entry/cancel/focus/drag, modes/curves/masks, complete Save/Reset/Load,
panel return without camera changes, and typed console workflows. Instrumentation
is enabled by default by `build-tree generate`. Use `-UiTests:$false` for
final-release builds. See [test setup and commands](Test/README.md).
