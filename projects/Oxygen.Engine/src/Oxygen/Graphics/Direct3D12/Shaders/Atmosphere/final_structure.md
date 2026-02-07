# Atmosphere Shader Structure

## 1. Common Reusable Components

### Shaders/Common/

- `Math.hlsli` — constants (PI, TWO_PI, etc.), safe math (SafeSqrt, SafeDivide, SafeExp), clamps, small epsilons
- `Geometry.hlsli` — ray/sphere intersections, horizon helpers, distance utilities, raymarch limits
- `Coordinates.hlsli` — clip/view/world helpers, UV conversions, planet-centered transforms, spherical coordinates
- `Lighting.hlsli` — luminance calculations (Rec. 709, Rec. 601), photometric unit conversions (lux, lumens, candelas)

### Shaders/Atmosphere/

- `AtmosphereMath.hlsli` — densities, optical depth, extinction, transmittance
- `AtmospherePhase.hlsli` — Rayleigh/Mie phase, HG/Cornette-Shanks
- `AtmosphereLutMapping.hlsli` — LUT UV mappings + half-texel offsets
- `AtmosphereDebug.hlsli` — debug gating, ToDebugWorld, line/cross helpers

## 2. Atmosphere Logical Processing Blocks

### Atmosphere/

- `TransmittanceLut_CS.hlsl` — builds transmittance LUT
- `MultiScatLut_CS.hlsl` — builds multiple scattering LUT
- `SkyViewLut_CS.hlsl` — builds sky view LUT
- `DirectIrradianceLut_CS.hlsl` — optional (not present yet)
- `IndirectIrradianceLut_CS.hlsl` — optional (not present yet)
- `ScatteringDensity_CS.hlsl` — optional (not present yet)
- `SingleScatteringLut_CS.hlsl` — optional (not present yet)
- `Terrain_CS.hlsl` — optional (not present yet)

Each pass should call only the atmosphere helpers above (no local math duplication).

## 3. Scattering Passes Mapping

### MultiScatLut_CS.hlsl (second order / infinite bounce approx)

**AtmosphereMath.hlsli:**

- `GetAtmosphereDensity()`, `GetAbsorptionDensity()`
- `TransmittanceFromOpticalDepth()`
- `ComputeExtinction()` helper for beta_rayleigh/mie/abs * density
- `IntegrateOpticalDepthStep()` helper (accumulated_od update)

**AtmospherePhase.hlsli:**

- `RayleighPhase()`, `HenyeyGreensteinPhase()` (keep in phase header even if
  currently not used in this pass; MultiScat uses isotropic 1/4PI now)

**AtmosphereLutMapping.hlsli:**

- `SampleTransmittanceLut()` wrapper (altitude, cos_sun_zenith -> optical depth)
- `ApplyHalfTexelOffset()` for LUT addressing

**Geometry.hlsli:**

- `RaySphereIntersectNearest()` for atmosphere exit distance
- `RaySphereIntersectNearest()` for ground intersection
- `SelectRayLengthGroundOrTop()` helper (choose shorter of ground or top)

**Coordinates.hlsli:**

- `PlanetCenteredPositionFromAltitude()` (origin at +Z radius)
- `AltitudeFromPosition()` helper (length(pos) - planet_radius)

### SkyViewLut_CS.hlsl (single scattering + multi-scat source + ground bounce)

**AtmosphereMath.hlsli:**

- `GetAtmosphereDensity()`, `GetAbsorptionDensity()`
- `TransmittanceFromOpticalDepth()`
- `ComputeExtinction()`, `IntegrateOpticalDepthStep()`
- `ComputeSampleTransmittance()` helper (exp(-extinction * ds))
- `ComputeSigmaSSingle()` helper (Rayleigh+Mie *phase* sun_trans)
- `ComputeSigmaSMulti()` helper (multi-scat radiance *energy comp* factor)

**AtmospherePhase.hlsli:**

- `RayleighPhase()`
- `CornetteShanksMiePhaseFunction()`, `HenyeyGreensteinPhase()` wrapper

**AtmosphereLutMapping.hlsli:**

- `SampleTransmittanceLutOpticalDepth()` for sun trans at sample
- `SampleMultiScatLut()` wrapper (cos_sun_zenith, altitude -> rgb + f_ms)

**Geometry.hlsli:**

- `RaySphereIntersectNearest()` for view ray length
- `RaySphereIntersectNearest()` for ground hit distance
- `ComputeRaymarchLimits()` helper (ray_length, hits_ground)

**Coordinates.hlsli:**

- `AltitudeFromPosition()` (sample altitude)
- `DirectionFromPosition()` for sun zenith (normalize sample_pos)
- `ViewRayFromUv()` (view_dir from LUT uv)

### Notes

- SkyView uses Frostbite/UE style analytic integration via throughput; keep this
  as a shared `IntegrateSegment()` helper to avoid per-pass drift.
- MultiScat uses isotropic 1/4PI for second bounce; keep the constant in
  `AtmospherePhase.hlsli` for reuse in optional passes later.

## 4. Sky View / Terrain Passes Mapping

### SkyView LUT Resolve (SkySphere_PS.hlsl, Atmosphere)

**AtmosphereLutMapping.hlsli:**

- `GetSkyViewLutUv()` helper (view_dir, origin_altitude)
- `ApplyHalfTexelOffset()` for LUT addressing

**AtmosphereMath.hlsli:**

- `ApplySkyExposure()` helper (luminance/exposure control)
- `ApplySkyLuminanceClamp()` helper (FP16 safety clamp)

**Geometry.hlsli:**

- `HorizonCosineFromAltitude()` helper (ground/atmo horizon fade)

**Coordinates.hlsli:**

- `PlanetCenteredPositionFromWorld()` for view ray origin
- `ViewDirectionFromWorld()` helper (normalized)

**AtmosphereDebug.hlsli:**

- Debug gating and line/cross rendering for LUT sampling verification

### Terrain_CS.hlsl (optional, not present yet)

**AtmosphereMath.hlsli:**

- `TransmittanceFromOpticalDepth()`
- `ComputeExtinction()`, `ComputeSampleTransmittance()`
- `EvaluateAerialPerspective()` helper (tau integration along view)

**AtmospherePhase.hlsli:**

- `RayleighPhase()`, `HenyeyGreensteinPhase()`

**AtmosphereLutMapping.hlsli:**

- `SampleTransmittanceLutOpticalDepth()` for sun/view transmittance
- `SampleSkyViewLut()` wrapper (view_dir, altitude -> sky radiance)

**Geometry.hlsli:**

- `RaySphereIntersectNearest()` for atmosphere segment length
- `ComputeRaymarchLimits()` helper (ray_length, hits_ground)

**Coordinates.hlsli:**

- `AltitudeFromPosition()`, `DirectionFromPosition()` for sun zenith
- `ViewRayFromUv()` for terrain pixel -> view ray

### DirectIrradianceLut_CS.hlsl (optional, not present yet)

**AtmosphereMath.hlsli:**

- `ComputeExtinction()`, `TransmittanceFromOpticalDepth()`
- `IntegrateOpticalDepthStep()`

**AtmosphereLutMapping.hlsli:**

- `SampleTransmittanceLutOpticalDepth()`

**Coordinates.hlsli:**

- `PlanetCenteredPositionFromAltitude()`, `DirectionFromPosition()`

### IndirectIrradianceLut_CS.hlsl (optional, not present yet)

**AtmosphereMath.hlsli:**

- `EvaluateMultiScatIrradiance()` helper (from multi-scat LUT)

**AtmosphereLutMapping.hlsli:**

- `SampleMultiScatLut()` wrapper (cos_sun_zenith, altitude)

### ScatteringDensity_CS.hlsl (optional, not present yet)

**AtmosphereMath.hlsli:**

- `ComputeScatteringDensity()` helper (single + multi source terms)
- `ComputeSigmaSSingle()`, `ComputeSigmaSMulti()`

**AtmospherePhase.hlsli:**

- `RayleighPhase()`, `CornetteShanksMiePhaseFunction()`

**AtmosphereLutMapping.hlsli:**

- `SampleTransmittanceLutOpticalDepth()`
- `SampleMultiScatLut()`

### SingleScatteringLut_CS.hlsl (optional, not present yet)

**AtmosphereMath.hlsli:**

- `IntegrateSegment()` helper (throughput integration)
- `ComputeSigmaSSingle()`, `TransmittanceFromOpticalDepth()`

**AtmospherePhase.hlsli:**

- `RayleighPhase()`, `HenyeyGreensteinPhase()`

**AtmosphereLutMapping.hlsli:**

- `SampleTransmittanceLutOpticalDepth()`

## 5. Functional Enhancement Plan

Ordered by dependencies + complexity.

### 5.0 Ground Truth (Current)

**LUT dimensionality is 2D/2D-array only:**

- Transmittance LUT outputs `RWTexture2D`
  - Reference: `TransmittanceLut_CS.hlsl#L34-L38`
- MultiScat LUT uses 2D UV parameterization and outputs `RWTexture2D`
  - Reference: `MultiScatLut_CS.hlsl#L15-L17`, `MultiScatLut_CS.hlsl#L218-L220`
- SkyView LUT outputs `RWTexture2DArray` (slice = altitude)
  - Reference: `SkyViewLut_CS.hlsl#L41-L48`

**Aerial perspective:**

- Current implementation is a 2-sample LUT approximation
  - Reference: `AerialPerspective.hlsli#L121-L125`

**UE reference for 3D scattering and camera volumes:**

- `Texture3D` usage in ScatteringDensity and MultipleScattering
  - Reference: `SkyAtmosphereScatteringDensity.hlsl#L8-L12`
  - Reference: `SkyAtmosphereMultipleScattering.hlsl#L8-L10`
- Camera volumes in RenderWithLuts
  - Reference: `RenderWithLuts.hlsl#L6-L10`

### 5.1 Sky-view LUT Parameterization Refinements (Lowest Risk)

**UE baseline:**

- Sub-texel UV remap and non-linear sky-view mapping in `UvToSkyViewLutParams`
  and `fromUnitToSubUvs`/`fromSubUvsToUnit`
  - Reference: `RenderSkyCommon.hlsl#L92-L147`

**Implementation tasks:**

- Add sub-texel remap helpers to `AtmosphereLutMapping.hlsli`
- Update SkyView LUT UV mapping in `SkyViewLut_CS.hlsl` (see `UvToViewDirection` mapping)
  - Reference: `SkyViewLut_CS.hlsl#L86-L135`
- Apply the same remap in sky resolve sampling path

### 5.2 Depth Clamp + Occlusion in View Integration (Low to Medium Risk)

**UE baseline:**

- Depth clamp inside `IntegrateScatteredLuminance`
  - Reference: `RenderSkyRayMarching.hlsl#L58-L70`
- Earth-shadow + shadowmap terms
  - Reference: `RenderSkyRayMarching.hlsl#L180-L193`

**Implementation tasks:**

- Add optional depth clamp for sky resolve or for any view-ray integration path to limit tMax by depth
- Add earth-shadow term (sun ray intersects planet) and optional shadowmap multiplication in SkyView integration

### 5.3 Aerial Perspective Integration Quality (Depends on 5.2)

**Current:**

- 2-sample LUT approximation
  - Reference: `AerialPerspective.hlsli#L121-L125`

**Implementation tasks:**

- Add a short view-segment integration mode (4-8 steps) driven by transmittance + sky-view LUT sampling
- Gate with `AtmosphereFlags` to preserve the fast path

### 5.4 Direct Irradiance LUT (Independent)

**UE baseline:**

- `ComputeDirectIrradianceTexture` in `DirectIrradianceLutPS`
  - Reference: `SkyAtmosphereDirectIrradianceLut.hlsl#L16-L25`

**Implementation tasks:**

- Add `DirectIrradianceLut_CS.hlsl` and bind it into the atmosphere pipeline
- Feed it into sky/ground lighting where sun/sky irradiance is used

### 5.5 Single Scattering 3D LUT (Foundation for 3D Pipeline)

**UE baseline:**

- `ComputeSingleScatteringTexture` in `SingleScatteringLutPS`
  - Reference: `SkyAtmosphereSingleScatteringLut.hlsl#L22-L31`

**Implementation tasks:**

- Add `SingleScatteringLut_CS.hlsl` that writes a 3D LUT (Rayleigh + Mie)
- Define the 3D parameterization and sampling helpers in `AtmosphereLutMapping`

### 5.6 Scattering Density 3D LUT (Depends on 5.5 + 5.4)

**UE baseline:**

- `ComputeScatteringDensityTexture`
  - Reference: `SkyAtmosphereScatteringDensity.hlsl#L8-L30`

**Implementation tasks:**

- Add `ScatteringDensity_CS.hlsl` that consumes single scattering + irradiance
  and outputs scattering density for higher orders

### 5.7 Multiple Scattering 3D LUT (Depends on 5.6)

**UE baseline:**

- `ComputeMultipleScatteringTexture`
  - Reference: `SkyAtmosphereMultipleScattering.hlsl#L19-L30`

**Implementation tasks:**

- Add `MultipleScatteringLut_CS.hlsl` using scattering density to accumulate higher-order scattering
- Decide on iteration count or convergence target

### 5.8 Indirect Irradiance LUT (Depends on 5.5-5.7)

**UE baseline:**

- `ComputeIndirectIrradianceTexture`
  - Reference: `SkyAtmosphereIndirectIrradiance.hlsl#L18-L28`

**Implementation tasks:**

- Add `IndirectIrradianceLut_CS.hlsl` and integrate into lighting

### 5.9 Camera Volumes (Depends on 5.5-5.7)

**UE baseline:**

- Camera volume textures in `RenderWithLuts`
  - Reference: `RenderWithLuts.hlsl#L6-L10`
- Depth-aware sky/ground resolve
  - Reference: `RenderWithLuts.hlsl#L70-L79`

**Implementation tasks:**

- Add camera scattering/transmittance volume generation pass
- Sample volumes in sky resolve and aerial perspective for geometry

### 5.10 Terrain / Surface Lighting Path (Depends on 5.2 + 5.4)

**UE baseline:**

- `TerrainPixelShader` applies transmittance LUT for sun lighting
  - Reference: `Terrain.hlsl#L111-L119`

**Implementation tasks:**

- Add `Terrain_CS` or forward surface hook that samples transmittance LUT,
  applies sun lighting and optional shadowing

### 5.11 Sampling Stability Controls (Optional, After 5.2 or 5.5)

**UE baseline:**

- Variable sample distribution in `IntegrateScatteredLuminance`
  - Reference: `RenderSkyRayMarching.hlsl#L76-L137`

**Implementation tasks:**

- Add variable step distribution + optional blue-noise dither for LUT integration passes to reduce banding
