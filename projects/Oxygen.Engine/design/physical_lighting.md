<!--
Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
copy at https://opensource.org/licenses/BSD-3-Clause.
SPDX-License-Identifier: BSD-3-Clause
-->

# Physical Lighting Roadmap (Oxygen)

## Goal

Deliver end-to-end physically based lighting with **uniform lux units**,
camera-accurate exposure, robust tonemapping, and consistent PBR rendering
across all demos and content pipelines.

## Current Observations (Codebase Gaps)

### Units & Exposure

- Light intensities are treated as **unitless scalars**.
- Exposure is applied as `exp2(EV)` in
 [src/Oxygen/Renderer/Renderer.cpp](../src/Oxygen/Renderer/Renderer.cpp),
 but EV is not derived from physical light/camera parameters.
- `EnvironmentDynamicData.exposure` is populated without a physical
 relationship to lux.

### Sun & Environment

- Sun state is stored in `SunState` as `intensity` and `illuminance`, but
 no clear lux semantics are enforced.
- Environment post-process is incomplete; only manual EV compensation
 is supported.
- Sky/atmosphere does not yet guarantee energy conservation with sun lux.

### Lights

- Directional/spot/point lights use `intensity` without specifying units.
- `LightCommon::exposure_compensation_ev` exists but is not documented as
 part of a physical pipeline.

### Rendering & Shaders

- Forward lighting uses light intensity directly in
 [src/Oxygen/Graphics/Direct3D12/Shaders/Passes/Forward/ForwardDirectLighting.hlsli](../src/Oxygen/Graphics/Direct3D12/Shaders/Passes/Forward/ForwardDirectLighting.hlsli)
 without lux-to-radiance conversion.
- Tone mapping is applied after exposure, but exposure is not physical.

### Camera

- No physical camera model (aperture/shutter/ISO) is wired to exposure.
- No EV100 pipeline for consistent exposure across scenes.

## Principles (Target State)

- **All light units are physical**:
  - Sun: **lux** (illuminance at surface)
  - Directional: **lux** (irradiance/illuminance)
  - Point/Spot: **lumens** (flux) or **candelas** (intensity)
  - Sky/IBL: **cd/m²** or derived radiance with proper calibration
- **Exposure is physical**: EV100 derived from camera parameters and/or
 scene luminance.
- **Materials are linear**: albedo in linear space, energy-conserving BRDF.
- **Tone mapping is consistent** and happens **after** exposure.

---

# Phased Plan with Trackable Tasks

## Phase 0 — Baseline Audit & Definitions

**Outcome:** agreed unit conventions and internal documentation.

**Deliverables (verifiable):**

- [ ] Unit convention doc committed (lux/lumen/cd/nit) and referenced by code.
- [ ] LightBench demo spec added and runnable via DemoShell.
- [ ] PostProcessPanel (DemoShell, reusable) created with an **Exposure**
  section.
- [ ] Glossary section updated with final terminology.

- [ ] Define authoritative unit conventions (lux, lumens, candelas, nits).
- [ ] Document unit usage in `SunState`, `DirectionalLight`, `PointLight`,
   `SpotLight`, and `PostProcessVolume`.
- [ ] Add a “physical lighting glossary” to this document.
- [ ] Create a simple reference scene spec (mid-gray card, white card,
  18% gray) to validate exposure in LightBench.

## Phase 1 — Data Model & API Alignment

**Outcome:** engine APIs describe physical units explicitly.

**Deliverables (verifiable):**

- [ ] Public headers show explicit unit names (e.g., `intensity_lux`).
- [ ] Doxygen for all light intensity fields references the units.
- [ ] No ambiguous `intensity` remains for light units in public APIs.

- [ ] Update `LightCommon` and light classes to clarify units:
  - Directional: `intensity_lux`
  - Point/Spot: choose **lumens** (recommended) and expose as such
- [ ] Update `SunState` and `EnvironmentDynamicData` fields naming:
  - `sun_illuminance_lux` and `sun_radiance` (if needed)
- [ ] Add unit annotations in headers and documentation comments.

## Phase 2 — Exposure System (Manual & Physical)

**Outcome:** manual exposure is physically meaningful and stable.

**Vocabulary note:** exposure is expressed in stops (EV, log₂ space). The
exposure scalar uses $2^{EV}$, and validation focuses on **adaptation rates**
(e.g., 0.1–0.2 EV per frame), **min/max ranges** (e.g., ±1–2 EV), and
**luminance ratios** (e.g., mid‑gray mapping, white point clipping), not
absolute tolerance.

**Deliverables (verifiable):**

- [ ] `CameraExposure` struct integrated and serialized.
- [ ] Manual exposure path uses EV100 formula end-to-end.
- [ ] Demo UI exposes camera exposure parameters and persists them.

- [ ] Introduce **CameraExposure** struct:
  - aperture (f-number), shutter (seconds), ISO
- [ ] Compute EV100 and exposure scale:
  - $EV100 = \log_2(\frac{N^2}{t}) - \log_2(\frac{ISO}{100})$
  - $exposure = \frac{1}{1.2} \cdot 2^{-EV100}$
- [ ] Update `Renderer::UpdateViewExposure` to use physical camera exposure.
- [ ] Update `PostProcessVolume` to set EV or camera parameters explicitly.

## Phase 3 — Lux-Consistent Lighting

**Outcome:** lights produce correct radiance for lux/lumen inputs.

**Deliverables (verifiable):**

- [ ] Shader conversions tested against reference values (unit tests or
      captured numeric validation).
- [ ] Directional, point, and spot lights visually match reference charts.
- [ ] Lux-based sun produces expected mid‑gray at target EV100.

- [ ] Directional lights: treat input as lux; convert to radiance using
   Lambertian model in shader.
- [ ] Point lights: if input is lumens, convert to intensity (cd) and then
   radiance. If input is cd, skip conversion.
- [ ] Spot lights: apply lumens/candela conversion with cone distribution.
- [ ] Add conversion helpers in shader library:
  - `LuxToIrradiance`, `LumensToCandela`, `CandelaToRadiance`.
- [ ] Update `ForwardDirectLighting.hlsli` to use conversions.

## Phase 4 — Sun & Environment Integration

**Outcome:** sun/sky/atmosphere respond correctly to physical units.

**Deliverables (verifiable):**

- [ ] Sun lux is the single source of truth for environment systems.
- [ ] Atmosphere LUTs match sun intensity changes without manual scaling.
- [ ] IBL brightness is calibrated to a known sky luminance (cd/m²).

- [ ] Normalize sun direction and ensure lux stored as illuminance.
- [ ] Use lux in `EnvironmentDynamicData` and consistently in shaders.
- [ ] Ensure aerial perspective uses physically correct sun intensity.
- [ ] Calibrate sky dome / IBL to realistic luminance (cd/m²).

## Phase 5 — Tone Mapping & Color Management

**Outcome:** stable, predictable output across displays.

**Deliverables (verifiable):**

- [ ] Tonemapper outputs match reference images for standard test scenes.
- [ ] Linear->sRGB conversion is applied exactly once per pixel.
- [ ] White balance and exposure compensation are verified by capture.

- [ ] Select a primary tone mapper (ACES fitted recommended).
- [ ] Add optional white-balance and exposure compensation stage.
- [ ] Ensure linear->sRGB conversion happens only once.
- [ ] Validate color pipeline against reference charts.

## Phase 6 — Auto Exposure (Histogram)

**Outcome:** robust auto exposure that works with physical units.

**Deliverables (verifiable):**

- [ ] Histogram pass produces stable exposure in bright/dark scenes.
- [ ] Auto exposure speed parameters affect convergence as expected.
- [ ] Exposure output is logged/visualized for validation builds.

- [ ] Implement luminance histogram pass (compute).
- [ ] Add metering modes (center-weighted, average, spot).
- [ ] Add temporal smoothing (speed up/down).
- [ ] Bind computed exposure to `EnvironmentDynamicData.exposure`.

## Phase 7 — Content & Tooling

**Outcome:** content authoring uses correct units and defaults.

**Deliverables (verifiable):**

- [ ] Demo presets updated with physical values (lux/lumen/EV).
- [ ] UI labels show units for all light and exposure controls.
- [ ] Import pipeline recognizes light units from source formats.

- [ ] Update demo presets to use physical values.
- [ ] Add UI labels with units in DemoShell panels.
- [ ] Add import pipeline metadata for light units where applicable.

## Phase 8 — Visualization & Gizmos

**Outcome:** light and exposure debugging visuals are available in LightBench.

**Deliverables (verifiable):**

- [ ] Light gizmos (position/axis markers) toggleable in LightBench.
- [ ] Scene widgets for light selection and inspection.

- [ ] Design and implement light visualization overlays (point/spot).
- [ ] Add optional gizmo scaling and visibility toggles.
- [ ] Capture reference screenshots for validation docs.

---

# Concrete Code Actions (By Area)

## Renderer

- [ ] Update `Renderer::UpdateViewExposure` to use camera model.
- [ ] Update `EnvironmentDynamicDataManager::SetSunState` to store lux
   explicitly and/or add conversion to radiance for shaders.

## Lights

- [ ] Rename and document light intensity fields with explicit units.
- [ ] Add conversion utilities in light data emission.

## Shaders

- [ ] Add conversion helpers in shared shader headers.
- [ ] Update forward lighting to apply physical conversions.

## Environment

- [ ] Implement robust post-process pipeline with exposure/tone map stages.
- [ ] Ensure environment sun/sky values match lux calibration.

## Camera

- [ ] Add camera exposure parameters to scene/camera classes.
- [ ] Expose camera settings in DemoShell panel.

---

# Validation & QA Checklist

- [ ] Middle‑gray mapping matches 0.18 or 0.22 within ±10–20% luminance error.
- [ ] White point clipping aligns with ~12–16 EV for outdoor scenes.
- [ ] Scene‑referred luminance is continuous across frames.
- [ ] Auto exposure adapts smoothly at ~0.1–0.2 EV per frame.
- [ ] Sun/sky/IBL are energy consistent.

---

# Glossary

- **Lux (lx)**: illuminance on a surface.
- **Lumen (lm)**: total light flux emitted.
- **Candela (cd)**: luminous intensity (lm/sr).
- **Nit (cd/m²)**: luminance of emissive surfaces or sky.
- **EV100**: exposure value referenced to ISO 100.
