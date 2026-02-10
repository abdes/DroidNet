# Physical Lighting Roadmap (Oxygen)

## Goal

Deliver end-to-end physically based lighting with **uniform units**,
camera-accurate exposure, robust tonemapping, and consistent PBR rendering
across all demos and content pipelines.

> **Scope (Implementation Note)**
>
> This document specifies a **practical real-time game engine implementation**. It focuses on how Oxygen transports and converts **photometric** quantities (Lux/Lumens/Nits) through CPU data models and HLSL shaders, and how those values integrate with exposure/tonemapping and sky/atmosphere. It is not intended to be a general radiometry/photometry textbook.
>
> **Related Documents:**
>
> - [PBR Architecture](PBR.md) — unit conventions and data model
> - [Forward Pipeline Design](forward_pipeline.md) — rendering architecture
> - [PostProcessPanel Design](PostProcessPanel.md) — UI integration

## Technical Specifications

### Light to Radiance Conversion

To ensure consistency in the shader pipeline, the following conversions are used to transform artist-facing units into the radiance values used for lighting calculations.

| Source Type | Artist Unit | Primary Unit | Conversion to Radiance ($L$) |
| :--- | :--- | :--- | :--- |
| **Sun** | Lux (`_lx`) | Illuminance | $L = \frac{E_v}{\pi}$ (Lambertian assumption for sky) |
| **Directional** | Lux (`_lx`) | Irradiance | $L = E_v$ (Direct Irradiance) |
| **Point / Spot** | Lumens (`_lm`) | Luminous Flux | $L = \frac{\Phi_v}{4\pi \cdot d^2}$ (Point) / Area-weighted (Spot) |
| **Area / Emissive** | Nits (`_nit`) | Luminance | $L = L_v$ |

> [!IMPORTANT]
> **Sun is one object with two responsibilities** in Oxygen:
>
> 1. **Directional-light role (lighting):** the Sun stores **illuminance at the receiver** in Lux (`_lx`) and is used as the source of truth for direct lighting and atmosphere energy.
> 2. **Optional visible-disk role (rendering):** when the sky pass renders a visible sun disk, the disk’s **luminance** in Nits (`_nit`) is **derived from the same Sun illuminance** and the configured sun angular diameter.
>
> Conceptually: the *same physical sun* provides both the scene’s incident illuminance (Lux) and, if drawn, a bright emissive disk in the sky (Nits). There is no separate “sun” and “sun disk” light source in the data model.
>
> [!NOTE]
> All local light sources (Point, Spot) **must** follow the Inverse-Square Law for attenuation ($Intensity \propto 1/d^2$). This is a requirement for physical unit consistency; without it, Lumen and Candela units cannot be calibrated against Lux-based surfaces or Camera Exposure.

### Calibration Standards

To ensure bit-accurate reproducibility in LightBench, use these exact values as the engine's "Golden Standards."

| Scenario | Illuminance (`_lx`) | Target EV100 (`_ev`) | Target Luminance (`_nit`) |
| :--- | :--- | :--- | :--- |
| **High Noon Sun** | **100,000** | **14.64** | **31,831.0** (Perfect Diffuse Reflector) |
| **Overcast Sky** | **10,000** | **11.32** | **3,183.1** |
| **Indoor Office** | **500** | **6.97** | **159.2** |
| **Full Moon** | **0.25** | **-2.00** | **0.08** |

> [!NOTE]
> Calculations based on $C = 250$ (incident-light constant) and $K = 12.5$ (reflected-light constant), following ISO 2720. Formula: $EV_{100} = \log_2(E \cdot 100 / C)$.

---

### Earth Environment Reference (Engineering Specification)

These constants and coefficients represent the standard **Earth 1976** atmospheric model. Use these values to calibrate the `SkyAtmosphereLutComputePass` and verify energy conservation.

#### 1. Geometric & Solar Constants

| Constant | Value | Description |
| :--- | :--- | :--- |
| **Solar Irradiance ($E_{sun}$)** | **133,312 `_lx`** | Illuminance at the top of the atmosphere (TOA). |
| **Sun Angular Diameter** | **0.545°** | Diameter of the sun disk from the observer's perspective. |
| **Earth Radius ($R_E$)** | **6,360 km** | Distance from the center of the earth to sea level ($y=0$). |
| **Atmosphere Thickness** | **100.0 km** | Top-of-Atmosphere (TOA) altitude. Radiance $= 0$ above this. |
| **Standard Albedo ($\rho$)** | **0.1** | Average ground reflectance for multiple scattering updates. |

#### 2. Rayleigh Scattering (Molecular)

*Dominates the blue color of the sky via $1/\lambda^4$ scattering.*

| Parameter | Value | Description |
| :--- | :--- | :--- |
| **Scattering Coeff ($\beta_R$)** | **(5.802, 13.558, 33.1) $10^{-3} km^{-1}$** | At sea level for $\lambda = (680, 550, 440)\text{nm}$. |
| **Scale Height ($H_R$)** | **8.0 km** | Altitude at which density falls by $1/e$ (Exponential). |

#### 3. Mie Scattering (Aerosols)

*Dominates the haze and the white "halo" (glare) near the sun via the phase function.*

| Parameter | Value | Description |
| :--- | :--- | :--- |
| **Scattering Coeff ($\beta_{M,s}$)** | **3.996 $10^{-3} km^{-1}$** | Wavelength-independent scattering at sea level. |
| **Absorption Coeff ($\beta_{M,a}$)** | **4.405 $10^{-4} km^{-1}$** | Wavelength-independent absorption at sea level. |
| **Scale Height ($H_M$)** | **1.2 km** | Altitude at which density falls by $1/e$ (Exponential). |
| **Asymmetry Factor ($g$)** | **0.8** | Cornette-Shanks/Henyey-Greenstein mean cosine. |

#### 4. Ozone Absorption (Chappuis Band)

*Responsible for the deep blue zenith and magenta twilight (filters yellow/orange).*

| Parameter | Value | Description |
| :--- | :--- | :--- |
| **Absorption Coeff ($\beta_{O,a}$)** | **(0.650, 1.881, 0.085) $10^{-3} km^{-1}$** | At **peak density** ($25\text{km}$) for $\lambda = (680, 550, 440)\text{nm}$. |
| **Profile Type** | **Two-Layer Linear** | Piecewise linear density distribution (Stratosphere). |
| **Layer 1 (Bottom)** | **10.0 km to 25.0 km** | Linear increase ($0.0 \rightarrow 1.0$). |
| **Layer 2 (Top)** | **25.0 km to 40.0 km** | Linear decrease ($1.0 \rightarrow 0.0$). |

#### 5. Sources & Research Grounding

The above parameters are derived from these industry-standard research papers and models:

- **Hillaire, S. (2020)**: "A Scalable and Production Ready Sky and Atmosphere Rendering System" (primary source for the **Two-Layer Linear** ozone profile and multi-scattering LUT integration).
- **U.S. Standard Atmosphere (1976)**: Fundamental physical constants for Earth's molecular (Rayleigh) scattering and geometric dimensions.
- **Lagarde, S., & de Rousiers, C. (2014)**: "Moving Frostbite to Physically Based Rendering" (standardized solar constant of 133k Lux and photometric unit integration).
- **ISO 2720:1974**: International standard for camera exposure calibration ($K=12.5$ and $C=250$).

---

## Current Observations (Codebase Gaps)

### Units & Exposure

- **Photometric Unit Audit**:

  | Unit | Current State | Gaps / Inconsistencies | To-Be Implementation |
  | :--- | :--- | :--- | :--- |
  | **Lux (lx)** | Used for `DirectionalLight` and `Sun` illuminance. Shaders treat as incident light $E$. | APIs (`CommonLightProperties`) don't specify unit in field names; UI lacks labels. | Mandatory **`_lx`** suffix in all public/private APIs. 100k lux sun as default. |
  | **Lumen (lm)** | Used conceptually for `Point`/`Spot` lights. Shaders calculate flux-to-intensity. | `CommonLightProperties.intensity` is generic; no explicit "lumens" in data structures. | Mandatory **`_lm`** suffix for local source power fields. UI shows total flux. |
  | **Candela (cd)** | Helper exists in `Lighting.hlsli` for flux conversion. | No primary input support for intensity-based sources. | Add **`_cd`** suffix support for high-intensity spot lights (peak intensity). |
  | **Nit (cd/m²)** | Baseline of 5000 Nits used for cubemaps. Atmosphere integrates to Nits. | Emissive materials use unit-less scales; sky-box intensity is arbitrary multiplier. | Mandatory **`_nit`** suffix for all absolute luminance fields (emissive, sky). |
  | **EV100** | Universal log-unit for exposure. Derived from physical camera parameters OR provided as a manual set point. | Previous inconsistencies between manual offsets and camera-derived values caused calibration drift. | Authoritative scene-referred log-unit for all modes. Mandatory **`_ev`** suffix. Unified $K=12.5$. |

### Exposure Path Analysis

A deep dive into the engine's exposure path (across CPU `Renderer` and HLSL shaders) reveals several discrepancies that break physical consistency:

1. **Photometric Unit Flow (Shaders)**:
    - **Direct Lighting (`ForwardDirectLighting.hlsli`)**: Correctly treats directional light intensity as Lux ($E$). It applies a 1:1 mapping in `LuxToIrradiance` and then divides by $\pi$ to obtain outgoing radiance ($L = E / \pi$) for Lambertian surfaces.
    - **Atmosphere (`AtmosphereSampling.hlsli`)**: Correctly integrates inscattered radiance into Nits (Radiance) by weighting against the physical `Sun::illuminance` (Lux). Sun disk radiance is derived via $L_{sun} = E_{sun} / \Omega_{sun}$ (solid angle).
    - **Tonemapping (`ToneMap_PS.hlsl`)**: Simply multiplies the final radiance by a scalar `exposure` provided by the CPU or an exposure buffer (from the histogram-based adaptation pass).

2. **The "Target Luminance" Mess (CPU vs. GPU)**:
     There is currently no unified "target luminance" (key value) across exposure modes, leading to significant brightness shifts:
    - **Manual (`ExposureMode::kManual`)**: Uses $exposure = (1.0 \div 12.5) \cdot 2^{-EV100}$. This effectively targets a luminance of **0.01**.
    - **Manual-Camera-based (`ExposureMode::kManualCamera`)**: Derives physical $EV100$ from camera parameters ($N, t, ISO$) but uses the same $1/12.5$ scaling, also targeting **0.01**.
    - **Auto (Histogram-based)**: Authoritative source for physical adaptation. However, its shaders (`AutoExposure_Average_CS.hlsl`) target **0.18** by default, creating a stark inconsistency with manual modes.

3. **Scaling Discrepancy**: This results in a baseline brightness difference of $\approx 4.17$ stops ($18 \times$) between Manual and Auto modes, making global physical calibration impossible without manual offsets.

### Sun & Environment

- Sun state is stored in `Sun` class with **`illuminance_lx_`** field
  (`src/Oxygen/Scene/Environment/Sun.h`) — **this is correctly implemented**.
- Environment post-process is incomplete; only manual **`_ev`** compensation
 is supported.
- Sky/atmosphere does not yet guarantee energy conservation with sun **`_lx`**.

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
  - Sun: **`_lx`** (illuminance at surface) — ✓ Implemented
  - Directional: **`_lx`** (irradiance/illuminance)
  - Point/Spot: **`_lm`** (flux) or **`_cd`** (intensity)
  - Sky/IBL: **`_nit`** or derived radiance with proper calibration
- **Exposure is physical**: **`_ev`** derived from camera parameters and/or
  scene luminance.
- **Materials are linear**: albedo in linear space, energy-conserving BRDF.
- **Tone mapping is consistent** and happens **after** exposure.

---

## Calibration

In Oxygen, Image-Based Lighting (IBL) must be calibrated to ensure that ambient lighting is physically proportional to direct light sources, whether it comes from a static HDRI or a dynamic synthetic sky.

### 1. Calibration for Static HDRI Assets

Static HDRIs typically have arbitrary intensity scales and require normalization to a target Lux value.

**Step 1: Calculate Raw HDRI Illuminance ($E_{raw}$):**

Integrate the HDRI luminance over the hemisphere to find its total energy in arbitrary engine units.

- **Formula**: $E_{raw} = \int_{0}^{2\pi} \int_{0}^{\pi/2} L(\theta, \phi) \cos \theta \sin \theta \, d\theta \, d\phi$
- **Practical Engine Implementation**: Sample the lowest MIP level of the diffuse irradiance cubemap (or SH coefficients) and multiply by $\pi$ to get the raw Lux value.

**Step 2: Sun Neutralization (Avoid Double-Lighting):**

- **Action**: In the pre-processing tool, mask out the captured sun disk and fill it with surrounding sky colors.
- **Why**: The analytical `Sun` component will provide the direct sunlight. Keeping the sun in the HDRI results in incorrect "double-lighting."

> [!NOTE]
> In the HDRI context, **“sun disk”** refers to the **visual sun blob** (the very bright pixels corresponding to the solar disk) present in the captured panorama. It is *not* a separate light type—it's an emissive feature baked into the image that must be removed if the engine also renders/uses the analytical Sun for direct lighting.

**Step 3: Intensity Mapping:**

- **Formula**: $multiplier = \frac{E_{target}}{E_{raw}}$
- **Result**: `SkyLight::SetIntensity(multiplier)` ensures the HDRI provides the exact desired Lux (e.g., 500 Lux for an overcast sky).

### 2. Calibration for Dynamic/Synthetic Environments

In modern engine workflows (like Oxygen), the environment is often generated procedurally using a sky/atmosphere model and a synthetic sun.

**The Continuity Principle:**

Since the `SkyAtmosphere` is driven by the physical `Sun::illuminance_lx_`, the radiance it generates is already physically correct (**`_nit`**). No external "mapping" or arbitrary scaling is required.

**Procedural Calibration Flow:**

1. **Source**: The `SkyAtmosphere` generates radiance based on Sun **`_lx`** and atmospheric coefficients (Rayleigh, Mie).
2. **Capture**: The `SkyCapturePass` renders this synthetic sky into a cubemap (at runtime or cubemap generation time).
3. **Consistency**: Because the source is already calibrated in **`_lx`** / **`_nit`**, the captured IBL is **intrinsically calibrated**.
4. **Intensity**: For captured scenes, `SkyLight::intensity_` should default to **1.0 (unity)** to maintain the physical relationship established by the atmosphere model.

**Handling Ground & Emissive Contributions:**

To achieve energy-consistent Image-Based Lighting (IBL) in both hybrid (HDRI + Sun) and procedural (Atmosphere + Sun) environments, the `SkyCapturePass` must evaluate ground radiance using the same physical laws as the primary scene.

- **Ground Radiance Evaluation**: Following established prior work (e.g., *Lagarde & de Rousiers 2014, "Moving Frostbite to PBR"*), the radiance from the terrain $L_{ground}$ in the lower hemisphere of the captured cubemap must be calculated as:
  $$L_{ground} = \frac{\rho}{\pi} \cdot E_{sun}$$
  Where $\rho$ is the **base color (reflectance)** and $E_{sun}$ is the **sun's irradiance** derived from `Sun::illuminance_lx_`. This ensures that a "100,000 **`_lx`**" sun correctly produces a proportional "31,830 **`_nit`**" ($100,000 / \pi$) radiance for a perfectly white ground, providing consistent indirect lighting (bounce) to scene objects via the pre-filtered IBL.
- **Emissive Calibration**: Emissive surfaces or volumetric clouds appearing in the `SkyCapturePass` must be specified in **Luminance (**`_nit`**)**. The `SkyCapture_PS.hlsl` must treat these as absolute **Radiance** values. This allows the IBL to capture high-energy events (e.g., a glowing sky at sunset) that contribute correctly to the scene's ambient energy when integrated across the probe's hemisphere.
- **Energy Balance**: By evaluating the ground at the correct radiance scale, the resulting **Irradiance (Diffuse)** and **Prefiltered (Specular)** maps derived from the capture will naturally include "ground bounce," removing the need for unphysical "ambient" or "hemisphere" light components.

### 3. Oxygen Pipeline Integration

- **Input**: HDRI file + `target_ambient_lx` (Static) OR `SkyCapturePass` output (Dynamic).
- **Output**: `SkyLight::intensity_` (multiplier).
- **Component Flow**: The `EnvironmentSystem` syncs these parameters to `EnvironmentDynamicData`. Shaders use the resulting radiance values directly, ensuring that a "100,000 **`_lx`**" sun and its "10,000 **`_lx`**" atmosphere contribution work together without manual scaling.

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
- [ ] **Baseline Fixes & Audit:**
  - [ ] **Unify Target Luminance & Calibration**: Adopt a unified target luminance (e.g., 0.18 for middle gray) across ALL exposure models (`Manual`, `ManualCamera`, and `Auto`) and standardize the ISO 2720 factor ($1 \div 12.5$) in `Renderer::UpdateViewExposure`.
  - [ ] **Audit AutoExposurePass Integration**: Verify consistent utilization of the histogram-based pass in the `ForwardPipeline` and ensure it uses the same physical constants as manual modes.
  - [ ] **Validate Physical-to-Pixel Mapping**: Create a test scene with a 100k **`_lx`** sun and an 18% gray card; verify the resulting pixel value matches the expected target (0.18).

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

- [X] Public headers show explicit unit names (e.g., `illuminance_lx_`).
- [X] Doxygen for all light intensity fields references the units.
- [X] No ambiguous `intensity` remains for light units in public APIs.

**Tasks:**

- [X] Update `CommonLightProperties` in `LightCommon.h` to clarify units:
  - Remove ambiguous `intensity` from common properties.
  - For Point/Spot: use `luminous_flux_lm` (lumens).
  - For Directional: use `intensity_lx` (illuminance).
- [X] Add typed accessors with unit suffixes:
  - `DirectionalLight::SetIntensityLux(float)`
  - `PointLight::SetLuminousFluxLm(float)`
  - `SpotLight::SetLuminousFluxLm(float)`
- [X] Update `SunState` and `EnvironmentDynamicData` fields naming:
  - `sun_illuminance_lx` (already present in Sun class)
  - Add `sun_radiance_nit` if needed for shader transport
- [X] Add unit annotations in headers and documentation comments.

### Phase 2 — Exposure System (Manual & Physical)

**Outcome:** manual exposure is physically meaningful and stable.

**Architectural Note:** Exposure is integrated into the `ForwardPipeline` via
the **Interceptor Pattern** (see [forward_pipeline.md, REQ02](forward_pipeline.md#requirements-specification)).
Raw scene radiance is captured in an HDR intermediate and resolved to SDR
during the `OnCompositing` phase using `ToneMapPass` and `CompositingPass`.

**Vocabulary note:** exposure is expressed in stops (**`_ev`**, log₂ space). The
exposure scalar uses $2^{EV}$, and validation focuses on **adaptation rates**
(e.g., 0.1–0.2 **`_ev`** per frame), **min/max ranges** (e.g., ±1–2 **`_ev`**), and
**luminance ratios** (e.g., mid‑gray mapping, white point clipping), not
absolute tolerance.

**Deliverables (verifiable):**

- [X] `CameraExposure` struct integrated and serialized.
- [X] Demo UI exposes camera exposure parameters and persists them.
- [X] Manual exposure path uses EV100 formula end-to-end in renderer.
- [X] **Renderer camera EV wiring & calibration:** Update `Renderer::UpdateViewExposure` to consume camera **`_ev`** for `kManualCamera` and apply the ISO 2720 calibration formula `exposure = (1/12.5) * 2^{-EV100}`. Apply a display key scale after calibration to align mid-gray for display. (Files: `src/Oxygen/Renderer/Renderer.cpp`, `src/Oxygen/Scene/Camera/CameraExposure.h`). Verification: unit tests for **`_ev`** -> exposure conversion and an integration test using LightBench mid-gray scene.
- [X] **Histogram-based auto exposure:** Implement an auto-exposure compute pass (luminance histogram), metering modes (average/center-weighted/spot), temporal smoothing and bind computed exposure to `EnvironmentDynamicData.exposure`. (Files: new `src/Oxygen/Renderer/Passes/AutoExposurePass.*`, updates to `EnvironmentDynamicDataManager`). Verification: deterministic tests for histogram outputs and adaptation behavior.
- [X] **Compositing tonemap behavior documented:** Tonemapping is performed per-view by `ToneMapPass` in `ForwardPipeline`. The `CompositingTaskType::kTonemap` enum value and placeholders were removed to avoid confusion; `Renderer::OnCompositing` focuses on copy/blend/texture-blend/taa operations. (File: `src/Oxygen/Renderer/Renderer.cpp`). Verification: end-to-end compositing test that exercises ForwardPipeline tonemap behavior.
- [X] **Shader conversion helpers & refactor:** Add named helpers (`LuxToIrradiance`, `LumensToCandela`, `CandelaToRadiance`) to `src/Oxygen/Graphics/Direct3D12/Shaders/Common/PhysicalLighting.hlsli` and refactor `ForwardDirectLighting.hlsli` to call them.
- [X] **Shader conversion validation:** Add shader unit tests or numeric validation
  against reference conversions.
- [X] **Validation scenes & tests:** Add LightBench presets and automated tests to verify mid-gray mapping, **`_ev`** correctness, and daylight references (e.g., **`_ev`** 9.7/14-16 scenarios). (Files: `Examples/LightBench`, test assets, `Examples/DemoShell/demo_settings.json`).

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
- [ ] Add docs and Doxygen comments for exposure fields, update `PostProcessVolume` docs to reflect **`_ev`** / camera parameter support.
- [X] Integrate with PostProcessPanel (see [PostProcessPanel.md, Phase C](PostProcessPanel.md#phase-c-postprocess-integration-exposure--tonemapping)).

### Phase 3 — Lux-Consistent Lighting

**Outcome:** lights produce correct radiance for lux/lumen inputs.

**Deliverables (verifiable):**

- [ ] Shader conversions tested against reference values (unit tests or
      captured numeric validation).
- [ ] Directional, point, and spot lights visually match reference charts.
- [ ] Lux-based sun produces expected mid‑gray at target **`_ev`**.

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
- [ ] Auto exposure adapts smoothly at ~0.1–0.2 **`_ev`** per frame.
- [ ] Sun/sky/IBL are energy consistent.
- [X] **Coroutine Integrity**: Post-process effects are implemented as awaitable **Coroutine Contributors** ([forward_pipeline.md, REQ07](forward_pipeline.md#requirements-specification)). (Implemented — passes are co_awaited in `ForwardPipeline` and `Renderer::OnCompositing`; tonemapping is handled per-view by `ToneMapPass` in `ForwardPipeline`; `CompositingTaskType::kTonemap` has been removed.)
- [ ] **Interceptor Validation**: HDR intermediates are correctly managed and
      resolved during `OnCompositing` ([forward_pipeline.md, REQ04](forward_pipeline.md#requirements-specification)).

---

## Glossary

- **Lux (lx)**: **Illuminance** (incident light). Mandatory suffix: **`_lx`**.
- **Lumen (lm)**: **Luminous Flux**. Mandatory suffix: **`_lm`**.
- **Candela (cd)**: **Luminous Intensity**. Mandatory suffix: **`_cd`**.
- **Nit (cd/m²)**: **Luminance**. Mandatory suffix: **`_nit`**.
- **EV100**: **Exposure Value**. Mandatory suffix: **`_ev`**.
- **ISO 2720**: The international standard for camera exposure calibration.
- **Coroutine Contributors**: see [forward_pipeline.md](forward_pipeline.md#definitions).
- **Interceptor Pattern**: see [forward_pipeline.md](forward_pipeline.md#definitions).
