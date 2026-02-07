# Technical Architecture: Modular Physical Atmosphere System

This document provides a comprehensive technical specification, implementation plan, and knowledge capture for the **Oxygen Engine**'s atmospheric system. It is strictly based on the **Hillaire (2020)** "Scalable and Production Ready Sky" technique, as implemented in the **Unreal Engine** reference source code.

---

## 1. Physical Foundation & Medium Modeling

**Reference:** `SkyAtmosphereCommon.hlsl`, `AtmosphereParameters` (Line 81); `RenderSkyCommon.hlsl`, `sampleMediumRGB` (Line 231).

### 1.1 The Multi-Component Atmosphere

The atmosphere is modeled as a heterogeneous medium composed of three distinct physical particles. Modeling them separately is critical for the "Gold-to-Blue" gradient observed during twilight.

* **Rayleigh Scattering:**
  * **Source:** `ScatteringRay` (Line 220, `RenderSkyCommon.hlsl`).
  * **Math:** Exponential falloff $d(h) = e^{-h / H_{ray}}$.
  * **Why:** Simulates scattering off molecules smaller than light's wavelength. It creates the primary blue color of the sky and the red shift of the setting sun.
* **Mie Scattering & Absorption:**
  * **Source:** `ScatteringMie`, `AbsorptionMie`, `ExtinctionMie` (Lines 243-245, `RenderSkyCommon.hlsl`).
  * **Math:** Exponential falloff $d(h) = e^{-h / H_{mie}}$.
  * **Why:** Simulates larger particles (dust, water droplets). Crucial for the "Haze" and the solar corona/glare. Unlike Rayleigh, Mie has significant **absorption** ($\beta_e = \beta_s + \beta_a$), which must be tracked to maintain energy conservation.
* **Ozone (Absorption Only):**
  * **Source:** `AbsorptionOzo`, `densityOzo` (Lines 237, 252, `RenderSkyCommon.hlsl`).
  * **Math:** Two-layer **Tent Function**.
    * $h < W_{ozo}: \text{Linear increase}$
    * $h \ge W_{ozo}: \text{Linear decrease}$
  * **Why:** Ozone is concentrated in the Stratosphere (~25km). Modeling it as an exponential (like Rayleigh) results in poor color accuracy at high altitudes. The "Tent" profile correctly simulates the absorption of yellow/orange light, which is why the Zenith turns deep blue/purple during blue hour.

---

## 2. Advanced Mathematical Mappings (LUTs)

**Reference:** `RenderSkyCommon.hlsl`, `UvTo...` and `...ToUv` functions (Lines 104-190).

### 2.1 Sky-View LUT (Horizon Concentration)

The Sky-View LUT (typically 192x108) is the most sensitive part of the system. Unreal uses a non-linear "Horizon-Aware" mapping.

* **The Problem:** In a linear zenith mapping, the horizon (where all the detail is) occupies only a few rows of pixels.
* **The Hillaire Solution (`NONLINEARSKYVIEWLUT`, Line 121):**
  * The mapping is split exactly at the **Geometric Horizon** ($\text{cos\_horizon} = -\sqrt{1-(R/r)^2}$).
  * **Above Horizon:** $v = 0.5 + 0.5 \times \sqrt{(\text{cos\_zenith} - \text{cos\_horizon}) / (1 - \text{cos\_horizon})}$.
  * **Below Horizon:** $v = 0.5 \times (1 - \sqrt{(\text{cos\_zenith} - (-1)) / (\text{cos\_horizon} - (-1))})$.
  * **Rationale:** The square root (or `coord * coord` in Line 137) "pushes" more texels toward the horizon line ($v=0.5$). This allows for 10x more resolution in the sunset/sunrise band.

### 2.2 Sub-Texel Nudging (`fromUnitToSubUvs`)

* **Code:** `RenderSkyCommon.hlsl`, Line 101.
* **Math:** $uv = (uv_{01} + 0.5 / res) \times (res / (res + 1.0))$.
* **Rationale:** Standard bilinear filtering near the edges of a LUT (like $0^\circ$ or $180^\circ$ zenith) will attempt to blend with "out of bounds" data or wrap incorrectly. This manual scaling ensures the hardware sampler always interpolates between two valid texel centers.

---

## 3. The "Frostbite" Integration Model

**Reference:** `RenderSkyRayMarching.hlsl`, `IntegrateScatteredLuminance` (Line 24).

### 3.1 Segment-Based Analytic Integration

Most engines use "Forward Euler" (Point Sample * StepSize). Unreal uses an **analytic segment integral**.

* **Code:** Line 232: `float3 Sint = (S - S * SampleTransmittance) / medium.extinction;`
* **Rationale:** Instead of assuming light is constant at a point, it assumes the **Source Term** $S$ is constant over a segment $dt$ and integrates the **Extinction** exponentially across it.
* **Math:** $\int_{0}^{dt} S \cdot e^{-\beta_e \cdot t} dt = \frac{S}{\beta_e}(1 - e^{-\beta_e \cdot dt})$.
* **Benefit:** This is mathematically robust for large step sizes. It allows the **Camera Volume LUT** to look smooth with only 32 slices, whereas a point-integrator would require hundreds.

---

## 4. Multiple Scattering: The Infinite Sum

**Reference:** `RenderSkyRayMarching.hlsl`, `NewMultiScattCS` (Line 418).

### 4.1 The $1/(1-r)$ Rationale

* **Problem:** Raymarching only accounts for "Single Scattering" (Sun $\rightarrow$ Particle $\rightarrow$ Camera). In reality, light bounces many times.
* **Algorithm (Reference Line 532):**
  * Precompute a "Multiple Scattering LUT" (32x32).
  * It stores $f_{ms}$ (the average fraction of light that stays in the atmosphere after one bounce).
  * **The Knowledge:** Using the sum of a geometric series ($1+r+r^2...$), the total scattering is $L_{\text{total}} = L_{\text{single}} \times \frac{1}{1-f_{ms}}$.
* **Impact:** This maintains the "Atmospheric Glow" and prevents the sky from becoming unnaturally dark when the sun is behind mountains or below the horizon.

---

## 5. Aerial Perspective (3D Camera Volume)

**Reference:** `RenderSkyRayMarching.hlsl`, `RenderCameraVolumePS` (Line 645).

### 5.1 Froxel Distribution

* **Code:** Line 661: `Slice *= Slice; // squared distribution`.
* **Rationale:** For a camera frustum extending 100km, uniform slices (e.g., every 3km) are too coarse for objects near the player.
* **Implementation:** By squaring the slice index, we get high-density frozels near the camera (0-5km) and low-density frozels in the distant background.

### 5.2 Planet-Relative Offsetting

* **Code:** Line 676.
* **Implementation:** `if (viewHeight <= Atmosphere.BottomRadius) { ... offset to surface ... }`.
* **Rationale:** If a 3D LUT voxel is technically "underground" due to the earth's curvature, naive sampling will integrate atmosphere starting from inside the planet. This creates a blue light leak visible through the terrain. The reference offsets the start position to exactly $R_g + \epsilon$.

---

## 6. Optimization & Performance Engineering

### 6.1 Blue Noise Jittering

* **Code:** `RenderSkyRayMarching.hlsl`, Line 134.
* **Implementation:** `t = t0 + (t1 - t0) * whangHashNoise(...)`.
* **Impact:** Converts deterministic banding into high-frequency noise. In a modern engine like Oxygen, the **TAA (Temporal Anti-Aliasing)** will resolve this noise into a perfectly smooth gradient for "free."

---

## 7. Oxygen Engine Implementation Roadmap

### Phase 1: Modular Physics Headers

- Create `AtmospherePhysics.hlsli` containing the Rayleigh/Mie/Ozone constants and the `sampleMediumRGB` logic.
* Create `AtmosphereLookups.hlsli` containing the quadratically warped Sky-View and Froxel mapping.

### Phase 2: The Scattering Density Pass (3D Compute)

This is the "Source Term" optimization.

1. **Grid:** 32x32x32 volume.
2. **Kernel:** For each cell, compute `ScatteredLight = (Sun_L * Sun_Transmittance * Phase) + MultiScat_L`.
3. **Barrier:** UAV Write $\rightarrow$ SRV Read.

### Phase 3: Optimized Integrators

- **SkyViewLut_CS:** Read the 3D Density volume and perform 32 integration steps.
* **CameraVolumeLut_CS:** Read the 3D Density volume and accumulate scattering into the Froxel grid.

### Phase 4: Forward Sampler

Update `AerialPerspective.hlsli`:

```hlsl
// New Sampler Logic
float3 view_dir = normalize(world_pos - camera_pos);
float depth = length(world_pos - camera_pos);
float slice_w = sqrt(depth / MaxDistance); // Inverse of squared distribution
float4 atmo = CameraVolumeLut.SampleLevel(LinearSampler, float3(screen_uv, slice_w), 0);
return fragment_color * (1.0 - atmo.a) + atmo.rgb;
```
