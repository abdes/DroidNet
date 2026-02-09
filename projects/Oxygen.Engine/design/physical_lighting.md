# Physical Lighting Roadmap (Oxygen)

## Goal

Deliver end-to-end physically based lighting with **uniform lux units**,
camera-accurate exposure, robust tonemapping, and consistent PBR rendering
across all demos and content pipelines.

> **Related Documents:**
>
> - [PBR Architecture](PBR.md) — unit conventions and data model
> - [Forward Pipeline Design](forward_pipeline.md) — rendering architecture
> - [PostProcessPanel Design](PostProcessPanel.md) — UI integration

## Current Observations (Codebase Gaps)

### Units & Exposure

- Light intensities use explicit physical units on light components
  (e.g., lumens for point/spot, lux for directional).
- Exposure is applied as `exp2(EV)` in `Renderer::UpdateViewExposure`
  (`src/Oxygen/Renderer/Renderer.cpp`); the renderer path still does not
  derive EV from camera parameters. (Note: DemoShell applies camera EV100 into
  the `ForwardPipeline`, but `Renderer::UpdateViewExposure` uses compensation
  EV only for manual/manual_camera modes. Also, automatic histogram-based
  exposure is not implemented. `CompositingTaskType::kTonemap` has been removed; tonemapping is managed per-view by `ToneMapPass` in `ForwardPipeline`.)
- `EnvironmentDynamicData.exposure` is populated without a physical
  relationship to lux for the renderer path.

### Sun & Environment

- Sun state is stored in `Sun` class with `intensity_lux_` field
  (`src/Oxygen/Scene/Environment/Sun.h`) — **this is correctly implemented**.
- Environment post-process is incomplete; only manual EV compensation
 is supported.
- Sky/atmosphere does not yet guarantee energy conservation with sun lux.

### Lights

- `DirectionalLight`, `PointLight`, `SpotLight` use `CommonLightProperties.intensity`
  without specifying units (`src/Oxygen/Scene/Light/`).
- `CommonLightProperties::exposure_compensation_ev` exists but is not documented as
 part of a physical pipeline.

### Rendering & Shaders

- Forward lighting uses light intensity directly in
 `src/Oxygen/Graphics/Direct3D12/Shaders/Passes/Forward/ForwardDirectLighting.hlsli`
 without lux-to-radiance conversion.
- Tone mapping is applied after exposure, but exposure is not physical.

### Camera

- Camera exposure (aperture/shutter/ISO) is stored on camera components via
  `CameraExposure` and persisted per camera in DemoShell.
- DemoShell applies manual camera EV100 to the runtime pipeline, but the
  renderer path is not yet driven by camera exposure.

## Principles (Target State)

- **All light units are physical**:
  - Sun: **lux** (illuminance at surface) — ✓ Implemented
  - Directional: **lux** (irradiance/illuminance)
  - Point/Spot: **lumens** (flux) or **candelas** (intensity)
  - Sky/IBL: **cd/m²** or derived radiance with proper calibration
- **Exposure is physical**: EV100 derived from camera parameters and/or
 scene luminance.
- **Materials are linear**: albedo in linear space, energy-conserving BRDF.
- **Tone mapping is consistent** and happens **after** exposure.

---

## Phased Plan with Trackable Tasks

### Phase 0 — Baseline Audit & Definitions

**Outcome:** agreed unit conventions and internal documentation.

**Deliverables (verifiable):**

- [X] Unit convention doc committed (lux/lumen/cd/nit) and referenced by code.
- [X] LightBench demo spec added and runnable via DemoShell.
- [X] PostProcessPanel (DemoShell, reusable) created with an **Exposure** section.
- [X] Glossary section updated with final terminology.
- [X] Define authoritative unit conventions (lux, lumens, candelas, nits).
- [X] Document unit usage in `CommonLightProperties`, `DirectionalLight`,
      `PointLight`, `SpotLight`, and `PostProcessVolume`.
- [X] Add a "physical lighting glossary" to this document.
- [X] Create a simple reference scene spec (mid-gray card, white card,
  18% gray) to validate exposure in LightBench.

### Phase 1 — Data Model & API Alignment

**Outcome:** engine APIs describe physical units explicitly.

**Source Files:**

| File | Namespace |
| ---- | --------- |
| `src/Oxygen/Scene/Light/LightCommon.h` | `oxygen::scene` |
| `src/Oxygen/Scene/Light/DirectionalLight.h` | `oxygen::scene` |
| `src/Oxygen/Scene/Light/PointLight.h` | `oxygen::scene` |
| `src/Oxygen/Scene/Light/SpotLight.h` | `oxygen::scene` |
| `src/Oxygen/Scene/Environment/Sun.h` | `oxygen::scene` |

**Deliverables (verifiable):**

- [X] Public headers show explicit unit names (e.g., `intensity_lux`).
- [X] Doxygen for all light intensity fields references the units.
- [X] No ambiguous `intensity` remains for light units in public APIs.

**Tasks:**

- [X] Update `CommonLightProperties` in `LightCommon.h` to clarify units:
  - Remove ambiguous `intensity` from common properties.
  - For Point/Spot: use `luminous_flux_lm` (lumens).
  - For Directional: use `intensity_lux` (illuminance).
- [X] Add typed accessors with unit suffixes:
  - `DirectionalLight::SetIntensityLux(float)`
  - `PointLight::SetLuminousFluxLm(float)`
  - `SpotLight::SetLuminousFluxLm(float)`
- [X] Update `SunState` and `EnvironmentDynamicData` fields naming:
  - `sun_illuminance_lux` (already present in Sun class)
  - Add `sun_radiance` if needed for shader transport
- [X] Add unit annotations in headers and documentation comments.

### Phase 2 — Exposure System (Manual & Physical)

**Outcome:** manual exposure is physically meaningful and stable.

**Architectural Note:** Exposure is integrated into the `ForwardPipeline` via
the **Interceptor Pattern** (see [forward_pipeline.md, REQ02](forward_pipeline.md#requirements-specification)).
Raw scene radiance is captured in an HDR intermediate and resolved to SDR
during the `OnCompositing` phase using `ToneMapPass` and `CompositingPass`.

**Vocabulary note:** exposure is expressed in stops (EV, log₂ space). The
exposure scalar uses $2^{EV}$, and validation focuses on **adaptation rates**
(e.g., 0.1–0.2 EV per frame), **min/max ranges** (e.g., ±1–2 EV), and
**luminance ratios** (e.g., mid‑gray mapping, white point clipping), not
absolute tolerance.

**Deliverables (verifiable):**

- [X] `CameraExposure` struct integrated and serialized.
- [X] Demo UI exposes camera exposure parameters and persists them.
- [X] Manual exposure path uses EV100 formula end-to-end in renderer.
- [X] **Renderer camera EV wiring & calibration:** Update `Renderer::UpdateViewExposure` to consume camera EV100 for `kManualCamera` and apply the ISO 2720 calibration formula `exposure = (1/12.5) * 2^{-EV100}`. Apply a display key scale after calibration to align mid-gray for display. (Files: `src/Oxygen/Renderer/Renderer.cpp`, `src/Oxygen/Scene/Camera/CameraExposure.h`). Verification: unit tests for EV->exposure conversion and an integration test using LightBench mid-gray scene.
- [X] **Histogram-based auto exposure:** Implement an auto-exposure compute pass (luminance histogram), metering modes (average/center-weighted/spot), temporal smoothing and bind computed exposure to `EnvironmentDynamicData.exposure`. (Files: new `src/Oxygen/Renderer/Passes/AutoExposurePass.*`, updates to `EnvironmentDynamicDataManager`). Verification: deterministic tests for histogram outputs and adaptation behavior.
- [X] **Compositing tonemap behavior documented:** Tonemapping is performed per-view by `ToneMapPass` in `ForwardPipeline`. The `CompositingTaskType::kTonemap` enum value and placeholders were removed to avoid confusion; `Renderer::OnCompositing` focuses on copy/blend/texture-blend/taa operations. (File: `src/Oxygen/Renderer/Renderer.cpp`). Verification: end-to-end compositing test that exercises ForwardPipeline tonemap behavior.
- [X] **Shader conversion helpers & refactor:** Add named helpers (`LuxToIrradiance`, `LumensToCandela`, `CandelaToRadiance`) to `src/Oxygen/Graphics/Direct3D12/Shaders/Common/PhysicalLighting.hlsli` and refactor `ForwardDirectLighting.hlsli` to call them.
- [X] **Shader conversion validation:** Add shader unit tests or numeric validation
  against reference conversions.
- [X] **Validation scenes & tests:** Add LightBench presets and automated tests to verify mid-gray mapping, EV correctness, and daylight references (e.g., EV100 9.7/14-16 scenarios). (Files: `Examples/LightBench`, test assets, `Examples/DemoShell/demo_settings.json`).

**Tasks:**

- [X] Introduce **CameraExposure** struct:
  - aperture (f-number), shutter (seconds), ISO
  - Location: `src/Oxygen/Scene/Camera/CameraExposure.h` (new file)
- [X] Compute EV100:
  - $EV100 = \log_2(\frac{N^2}{t}) - \log_2(\frac{ISO}{100})$
- [X] Align exposure scale with ISO 2720 calibration and a display key:
  - $exposure = \frac{1}{12.5} \cdot 2^{-EV100} \cdot key$
- [X] Update `Renderer::UpdateViewExposure` to use physical camera exposure (kManualCamera), add unit tests and a LightBench integration scene to verify mid-gray mapping.
- [ ] Implement `AutoExposurePass`: compute luminance histogram, expose metering modes, provide temporal smoothing, and bind results to `EnvironmentDynamicData.exposure`. Add compute shader tests and integration scenarios.
- [X] Add `PhysicalLighting.hlsli` shared helpers and refactor
  `ForwardDirectLighting.hlsli` to use them.
- [ ] Add shader validation tests for conversion helpers.
- [X] Documented that tonemapping is managed by `ForwardPipeline` and removed `CompositingTaskType::kTonemap`; add an end-to-end compositing test to verify ForwardPipeline behavior.
- [ ] Add docs and Doxygen comments for exposure fields, update `PostProcessVolume` docs to reflect EV/camera parameter support.
- [X] Integrate with PostProcessPanel (see [PostProcessPanel.md, Phase C](PostProcessPanel.md#phase-c-postprocess-integration-exposure--tonemapping)).

### Phase 3 — Lux-Consistent Lighting

**Outcome:** lights produce correct radiance for lux/lumen inputs.

**Deliverables (verifiable):**

- [ ] Shader conversions tested against reference values (unit tests or
      captured numeric validation).
- [ ] Directional, point, and spot lights visually match reference charts.
- [ ] Lux-based sun produces expected mid‑gray at target EV100.

**Tasks:**

- [X] Directional lights: treat input as lux; convert to radiance using
   Lambertian model in shader.
- [X] Point lights: if input is lumens, convert to intensity (cd) and then
   radiance. If input is cd, skip conversion.
- [X] Spot lights: apply lumens/candela conversion with cone distribution.
- [X] Add conversion helpers in shader library:
  - `LuxToIrradiance`, `LumensToCandela`, `CandelaToRadiance`.
  - Location: `src/Oxygen/Graphics/Direct3D12/Shaders/Common/PhysicalLighting.hlsli`
- [X] Update `ForwardDirectLighting.hlsli` to use conversions.

### Phase 4 — Sun & Environment Integration

**Outcome:** sun/sky/atmosphere respond correctly to physical units.

**Deliverables (verifiable):**

- [ ] Sun lux is the single source of truth for environment systems.
- [ ] Atmosphere LUTs match sun intensity changes without manual scaling.
- [ ] IBL brightness is calibrated to a known sky luminance (cd/m²).

**Tasks:**

- [ ] Normalize sun direction and ensure lux stored as illuminance.
- [ ] Use lux in `EnvironmentDynamicData` and consistently in shaders.
- [ ] Ensure aerial perspective uses physically correct sun intensity.
- [ ] Calibrate sky dome / IBL to realistic luminance (cd/m²).

### Phase 5 — Tone Mapping & Color Management

**Outcome:** stable, predictable output across displays.

**Deliverables (verifiable):**

- [ ] Tonemapper outputs match reference images for standard test scenes.
- [ ] Linear->sRGB conversion is applied exactly once per pixel.
- [ ] White balance and exposure compensation are verified by capture.

**Tasks:**

- [ ] Select a primary tone mapper (ACES fitted recommended).
- [ ] Add optional white-balance and exposure compensation stage.
- [ ] Ensure linear->sRGB conversion happens only once.
- [ ] Validate color pipeline against reference charts.

**Tonemapper Options (Engine Support):**

| Enum Value | UI Label | Description |
| ---------- | -------- | ----------- |
| `ToneMapper::kAcesFitted` | ACES | Industry-standard filmic curve |
| `ToneMapper::kFilmic` | Filmic | Classic filmic S-curve |
| `ToneMapper::kReinhard` | Reinhard | Simple luminance mapping |
| `ToneMapper::kNone` | None | Passthrough (debug only) |

### Phase 6 — Auto Exposure (Histogram)

**Outcome:** robust auto exposure that works with physical units.

**Deliverables (verifiable):**

- [ ] Histogram pass produces stable exposure in bright/dark scenes.
- [ ] Auto exposure speed parameters affect convergence as expected.
- [ ] Exposure output is logged/visualized for validation builds.

**Tasks:**

- [ ] Implement luminance histogram pass (compute).
- [ ] Add metering modes (center-weighted, average, spot).
- [ ] Add temporal smoothing (speed up/down).
- [ ] Bind computed exposure to `EnvironmentDynamicData.exposure`.

### Phase 7 — Content & Tooling

**Outcome:** content authoring uses correct units and defaults.

**Deliverables (verifiable):**

- [ ] Demo presets updated with physical values (lux/lumen/EV).
- [ ] UI labels show units for all light and exposure controls.
- [ ] Import pipeline recognizes light units from source formats.

**Tasks:**

- [ ] Update demo presets to use physical values.
- [ ] Add UI labels with units in DemoShell panels.
- [ ] Add import pipeline metadata for light units where applicable.

### Phase 8 — Visualization & Gizmos

**Outcome:** light and exposure debugging visuals are available in LightBench.

**Deliverables (verifiable):**

- [ ] Light gizmos (position/axis markers) toggleable in LightBench.
- [ ] Scene widgets for light selection and inspection.

**Tasks:**

- [ ] Design and implement light visualization overlays (point/spot).
- [ ] Add optional gizmo scaling and visibility toggles.
- [ ] Capture reference screenshots for validation docs.

---

## Concrete Code Actions (By Area)

### Renderer

- [ ] Update `Renderer::UpdateViewExposure` to use camera model.
- [ ] Update `EnvironmentDynamicDataManager::SetSunState` to store lux
   explicitly and/or add conversion to radiance for shaders.
- [X] Document that tonemapping is performed per-view by `ToneMapPass` in `ForwardPipeline` and remove `CompositingTaskType::kTonemap` and related placeholders from the compositing API. (Files: `src/Oxygen/Renderer/Types/CompositingTask.h`, `src/Oxygen/Renderer/Renderer.cpp`)

### Lights

- [ ] Rename and document light intensity fields with explicit units.
- [ ] Add conversion utilities in light data emission.

### Shaders

- [ ] Add conversion helpers in shared shader headers.
- [ ] Update forward lighting to apply physical conversions.

### Environment

- [ ] Implement robust post-process pipeline with exposure/tone map stages.
- [ ] Ensure environment sun/sky values match lux calibration.

### Camera

- [ ] Add camera exposure parameters to scene/camera classes.
- [ ] Expose camera settings in DemoShell panel.

---

## Validation & QA Checklist

- [ ] Middle‑gray mapping matches 0.18 or 0.22 within ±10–20% luminance error.
- [ ] White point clipping aligns with ~12–16 EV for outdoor scenes.
- [ ] Scene‑referred luminance is continuous across frames.
- [ ] Auto exposure adapts smoothly at ~0.1–0.2 EV per frame.
- [ ] Sun/sky/IBL are energy consistent.
- [X] **Coroutine Integrity**: Post-process effects are implemented as awaitable **Coroutine Contributors** ([forward_pipeline.md, REQ07](forward_pipeline.md#requirements-specification)). (Implemented — passes are co_awaited in `ForwardPipeline` and `Renderer::OnCompositing`; tonemapping is handled per-view by `ToneMapPass` in `ForwardPipeline`; `CompositingTaskType::kTonemap` has been removed.)
- [ ] **Interceptor Validation**: HDR intermediates are correctly managed and
      resolved during `OnCompositing` ([forward_pipeline.md, REQ04](forward_pipeline.md#requirements-specification)).

---

## Glossary

- **Lux (lx)**: illuminance on a surface.
- **Lumen (lm)**: total light flux emitted.
- **Candela (cd)**: luminous intensity (lm/sr).
- **Nit (cd/m²)**: luminance of emissive surfaces or sky.
- **EV100**: exposure value referenced to ISO 100.
- **Coroutine Contributors**: see [forward_pipeline.md](forward_pipeline.md#definitions).
- **Interceptor Pattern**: see [forward_pipeline.md](forward_pipeline.md#definitions).
