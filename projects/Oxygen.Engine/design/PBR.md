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

## Lighting

Lighting defines how authored values are interpreted and converted into
shader inputs. Oxygen follows a physically based model with explicit unit
conventions and clear separation between authored values and shader-space
quantities.

### Architecture overview

- **Authoring layer (CPU):** light components store values in physical units
 and remain unit-consistent across the engine.
- **Transport layer:** light data is packed into GPU structures without
 losing unit meaning (e.g., “lux” stays “lux”).
- **Shading layer (GPU):** unit conversions happen in shader helpers using
 explicit formulas (lux → radiance, lumens → candela → radiance).
- **Exposure layer:** camera exposure converts scene radiance into display
 values (EV100-based manual exposure and auto-exposure if enabled).

### Units

Oxygen uses the following units by default:

- **Directional (sun/sky) illuminance:** lux (lm/m²)
- **Point and spot luminous flux:** lumens (lm)
- **Surface and IBL luminance:** nits (cd/m²)
- **Exposure:** EV100 (from aperture, shutter, ISO)

If a light type supports an alternate unit (e.g., candela for point/spot),
the unit must be explicit in the field name and UI label.

### Conventions and formulas

- **Directional light (lux):** convert illuminance to radiance in shader using
 a Lambertian model.
- **Point light (lumens):** convert lumens → candela using total flux and
 distribution, then candela → radiance.
- **Spot light (lumens):** distribute flux within the cone; convert to candela
 and then to radiance.

Recommended helper set (shader library):

- `LuxToIrradiance`
- `LumensToCandela`
- `CandelaToRadiance`

### Data model

All public fields must include the unit in the name or Doxygen comment. Examples:

- `DirectionalLight::intensity_lux`
- `PointLight::luminous_flux_lm`
- `SpotLight::luminous_flux_lm`
- `Environment::sky_luminance_nits`

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

### Validation scenes

All lighting changes should be verified against a reference scene containing:

- 18% gray card
- White and black cards
- One calibrated directional light and one point light
- Known camera exposure

Validation criteria use luminance ratios:

- Middle‑gray mapping (typically 0.18 or 0.22).
- White point clipping in outdoor scenes (around 12–16 EV).
- Scene‑referred luminance continuity across frames.

Acceptable deviation is **±10–20% luminance error**, and auto‑exposure
adaptation is typically **~0.1–0.2 EV per frame**.

### Glossary terms

- **Illuminance (lux):** luminous flux per unit area; used for sun/directional.
- **Luminous flux (lumens):** total emitted light; used for point/spot inputs.
- **Luminous intensity (candela):** lumens per steradian.
- **Luminance (cd/m², nits):** brightness of a surface or environment.
- **Radiance/irradiance:** shader-space quantities derived from physical units.
- **EV100:** exposure value normalized to ISO 100.
