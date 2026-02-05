# Physically Based Rendering in Oxygen

## Introduction

This document captures the PBR architecture used by Oxygen, including the
principles, data flow, and conventions required to achieve predictable,
physically plausible results. It is intended to be the single source of
truth for how materials, lights, exposure, and post-processing interact.

The priorities are:

- **Consistency:** authored values map to stable visual outcomes.
- **Physical plausibility:** units are explicit, and conversions are
 documented and enforced.
- **Debuggability:** validation scenes and metrics make it easy to verify
 correctness.

> **Related Documents:**
>
> - [Physical Lighting Roadmap](physical_lighting.md) — phased implementation plan
> - [Forward Pipeline Design](forward_pipeline.md) — rendering architecture
> - [PostProcessPanel Design](PostProcessPanel.md) — UI integration

## Lighting

Lighting defines how authored values are interpreted and converted into
shader inputs. Oxygen follows a physically based model with explicit unit
conventions and clear separation between authored values and shader-space
quantities.

### Architecture overview

- **Authoring layer (CPU):** light components store values in physical units
 and remain unit-consistent across the engine.
- **Transport layer:** light data is packed into GPU structures without
 losing unit meaning (e.g., "lux" stays "lux").
- **Shading layer (GPU):** unit conversions happen in shader helpers using
 explicit formulas (lux → radiance, lumens → candela → radiance).
- **Exposure layer:** camera exposure converts scene radiance into display
  values (EV100-based manual exposure and auto-exposure if enabled).
- **Composition layer:** assembled via `ToneMapPass` and `CompositingPass`
  during the `OnCompositing` phase to resolve HDR radiance into final SDR
  pixel values.

### Units

Oxygen uses the following units by default:

| Light Type | Authored Unit | Symbol | Notes |
| ---------- | ------------- | ------ | ----- |
| Directional (sun/sky) | Illuminance | lux (lm/m²) | `Sun::SetIntensityLux()` |
| Point | Luminous flux | lumens (lm) | Via `CommonLightProperties.intensity` (see Phase 1) |
| Spot | Luminous flux | lumens (lm) | Via `CommonLightProperties.intensity` (see Phase 1) |
| Surface / IBL | Luminance | nits (cd/m²) | `SkySphere` + `SkyLight` luminance calibration |
| Exposure | Exposure Value | EV100 | From aperture, shutter, ISO |

If a light type supports an alternate unit (e.g., candela for point/spot),
the unit must be explicit in the field name and UI label.

> **Implementation Note:** The current `CommonLightProperties.intensity` field
> in `src/Oxygen/Scene/Light/LightCommon.h` is unitless. Phase 1 of the
> [Physical Lighting Roadmap](physical_lighting.md) addresses renaming and
> documenting these fields with explicit units.

### Conventions and formulas

- **Directional light (lux):** convert illuminance to radiance in shader using
 a Lambertian model.
- **Point light (lumens):** convert lumens → candela using total flux and
 distribution, then candela → radiance.
- **Spot light (lumens):** distribute flux within the cone; convert to candela
 and then to radiance.

Recommended helper set (shader library):

- `LuxToIrradiance(float illuminance_lux)` — directional lights
- `LumensToCandela(float flux_lm)` — point/spot lights (isotropic)
- `CandelaToRadiance(float intensity_cd, float distance)` — attenuation

> **Status:** These helpers are specified but not yet implemented in shaders.
> See [Physical Lighting Roadmap, Phase 3](physical_lighting.md#phase-3--lux-consistent-lighting).

### Data model

All public fields must include the unit in the name or Doxygen comment.

**Current Implementation:**

| Class | Header | Current Field | Target Field |
| ----- | ------ | ------------- | ------------ |
| `Sun` | `src/Oxygen/Scene/Environment/Sun.h` | `intensity_lux_` | ✓ Complete |
| `DirectionalLight` | `src/Oxygen/Scene/Light/DirectionalLight.h` | `common_.intensity` | `intensity_lux` |
| `PointLight` | `src/Oxygen/Scene/Light/PointLight.h` | `common_.intensity` | `luminous_flux_lm` |
| `SpotLight` | `src/Oxygen/Scene/Light/SpotLight.h` | `common_.intensity` | `luminous_flux_lm` |

### Exposure and calibration

Manual exposure uses physical camera parameters:

- Aperture ($N$), shutter time ($t$), ISO
- $EV100 = \log_2(\frac{N^2}{t}) - \log_2(\frac{ISO}{100})$
- $exposure = \frac{1}{1.2} \cdot 2^{-EV100}$

Exposure is expressed in **stops** (log₂ space). A change of 1 EV doubles or
halves brightness. Auto-exposure is validated by **adaptation rate** and
**luminance ratios**, not fixed absolute values.

Auto-exposure (if enabled) must operate on scene luminance derived from the
same unit-consistent pipeline.

### Sky environment calibration

Sky environment luminance is authored explicitly in **nits (cd/m²)** and is
applied consistently to:

- **Sky background** (`SkySphere::SetLuminanceNits`).
- **IBL capture/filtering** (sky cubemap scaling during IBL generation).
- **IBL shading** (diffuse/specular energy is derived from the calibrated
  maps and only tinted at shading time).

This avoids per-sky exposure hacks and ensures a single camera EV produces
correct relative brightness between the sun, sky, and IBL.

**Default Exposure (EV100 = 9.7):**

This corresponds to a typical sunny outdoor scene with:

- Aperture: f/11
- Shutter: 1/125s
- ISO: 100

### Pipeline Integration

The PBR pipeline is implemented as a **Coroutine-based Render Graph**
(see [Forward Pipeline Design](forward_pipeline.md) for complete definitions).

1. **HDR Capture**: The main shading coroutines render raw physical radiance
   into intermediate HDR textures (`Format::kRGBA16Float`).
2. **Post-Process Chain**: Effects like Bloom or TAA are injected as
   **Coroutine Contributors** during the high-dynamic-range phase.
3. **SDR Resolution**: The `OnCompositing` phase uses physical exposure laws
   via `ToneMapPass` to resolve the HDR intermediate into the swapchain,
   ensuring the final image maintains physical integrity.

## Validation Scenes

All lighting changes should be verified against a reference scene containing:

- 18% gray card
- White and black cards
- One calibrated directional light and one point light
- Known camera exposure (EV100 = 9.7 recommended for outdoor)

Validation criteria use luminance ratios:

- Middle‑gray mapping (typically 0.18 or 0.22).
- White point clipping in outdoor scenes (around 12–16 EV).
- Scene‑referred luminance continuity across frames.

Acceptable deviation is **±10–20% luminance error**, and auto‑exposure
adaptation is typically **~0.1–0.2 EV per frame**.

> **Reference Demo:** `Examples/LightBench` provides a validation scene
> with calibrated lighting and exposure controls.

## Glossary

- **Illuminance (lux):** luminous flux per unit area; used for sun/directional.
- **Luminous flux (lumens):** total emitted light; used for point/spot inputs.
- **Luminous intensity (candela):** lumens per steradian.
- **Luminance (cd/m², nits):** brightness of a surface or environment.
- **Radiance/irradiance:** shader-space quantities derived from physical units.
- **EV100:** exposure value normalized to ISO 100.
- **Coroutine Contributors:** awaitable task generators that inject work into
  the main render coroutine (see [forward_pipeline.md](forward_pipeline.md)).
- **Interceptor Pattern:** architectural redirection of view outputs to HDR
  intermediates before shading (see [forward_pipeline.md](forward_pipeline.md)).
