# Environment Sun-Below-Horizon Remediation

**Phase:** 4D / 4A integration blocker
**Status:** `in_progress`

## 1. Why This Document Exists

The current Vortex atmosphere lane is **not** parity-closed for the specific
UE5.7 problem space where the atmosphere light's zenith cosine is `<= 0`
(sun/moon at the horizon or below it).

The earlier Vortex closeout work covered translated planet-frame publication,
sky-view generation, camera-aerial generation, and Stage-15 consumption on the
active path. It did **not** fully audit or close the exact UE5.7 contracts that
prevent:

- sudden blackouts when the sun crosses the horizon
- bilinear seam/flicker from ground-occluded transmittance LUTs
- sunset banding from unjittered low-sample ray marching
- disagreement between sky/atmosphere products and direct-light shading

This document corrects that scope and defines the required remediation.

## 2. Authoritative UE5.7 References

This remediation is grounded in the UE5.7 ownership split described by the
following source/shader lanes:

- `Engine/Source/Runtime/Renderer/Private/SkyAtmosphereRendering.cpp`
- `Engine/Source/Runtime/Engine/Private/Components/SkyAtmosphereComponent.cpp`
- `Engine/Source/Runtime/Engine/Private/Components/DirectionalLightComponent.cpp`
- `Engine/Source/Runtime/Engine/Public/Rendering/SkyAtmosphereCommonData.cpp`
- `Engine/Shaders/Private/SkyAtmosphere.usf`
- `Engine/Shaders/Private/SkyAtmosphereCommon.ush`

The focused direct-light / directional-light-resolver remediation for this lane
is now implemented on the current branch.

File-placement rule for that item:

- Vortex-specific shader logic must live under `Shaders/Vortex/...`, not under
  legacy shader trees.

The required UE behaviors for the sunset/below-horizon regime are:

1. raw atmosphere-light direction remains unclamped
2. the only authored horizon floor is the ground-transmittance
   `TransmittanceMinLightElevationAngle`
3. the transmittance LUT spans full `mu in [-1, 1]` and does **not** bake
   planet occlusion
4. direct-light occlusion is reintroduced analytically where needed
5. sky-view LUT parameterization biases resolution around the true horizon seam
6. camera-aerial voxels below/behind the horizon are mirrored across the
   tangent point
7. ray marching uses per-pixel/frame jitter plus variable sample count
8. the sky camera is snapped above the virtual planet surface

Items 5, 6, and 8 already exist in the active Vortex path and must be
preserved. The remaining items are not fully mirrored yet.

## 3. Current Vortex Audit Summary

The active Vortex code already mirrors these UE behaviors:

- translated planet frame plus 5 m sky-camera snap in
  `src/Oxygen/Vortex/Environment/EnvironmentLightingService.cpp`
- sky-view split-horizon parameterization plus sub-texel clamping in
  `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Environment/AtmosphereParityCommon.hlsli`
- camera-aerial below-horizon mirroring plus near-slice fade in
  `.../AtmosphereCameraAerialPerspective.hlsl` and
  `src/Oxygen/Graphics/Direct3D12/Shaders/Atmosphere/AerialPerspective.hlsli`
- sun-disk analytical ground cull, soft edge, and luminance clamp in
  `.../Sky.hlsl`
- multi-scattering full-range `mu` guards in
  `.../AtmosphereMultiScatteringLut.hlsl` and
  `.../AtmosphereUeMirrorCommon.hlsli`

The open gaps are concentrated in the transmittance contract, the direct-light
contract, and missing sunset-noise suppression.

## 4. Gap Inventory

### 4.1 Gap A — `TransmittanceMinLightElevationAngle` Exists But Is Functionally Unused

**UE5.7 contract**

- UE only applies the authored minimum elevation angle when computing the
  directional light's baked `TransmittanceTowardSun` for the non-per-pixel
  ground-lighting path.
- The per-pixel path keeps the raw light and lets the shader apply
  full-range LUT transmittance per shaded point.

**Current Vortex state**

- Authored field exists:
  `src/Oxygen/Scene/Environment/SkyAtmosphere.h`
- Runtime model stores it:
  `src/Oxygen/Vortex/Environment/Internal/AtmosphereState.cpp`
- View data publishes it:
  `src/Oxygen/Vortex/Environment/EnvironmentLightingService.cpp`
- No CPU ground-transmittance function exists.
- No per-light cached `TransmittanceTowardSun` exists.
- No lighting publication equivalent to UE's
  `SetAtmosphereRelatedProperties(...)` exists.

**Resulting behavioral defect**

- Non-per-pixel lighting has no authored horizon floor at all.
- When the sun crosses the horizon, Vortex direct lighting collapses straight to
  shader-side horizon zero instead of following UE's optional clamped
  ground-transmittance path.

**Required remediation**

1. Add a Vortex CPU function that mirrors UE's
   `GetTransmittanceAtGroundLevel(...)` contract:
   - observer at `planet_radius + 500 m`
   - azimuth discarded
   - elevation clamped by authored
     `transmittance_min_light_elevation_deg`
   - fixed 15-sample optical-depth integration to top-of-atmosphere
2. Publish the result as a per-atmosphere-light runtime field:
   - `transmittance_toward_sun_rgb`
   - computed once per frame after atmosphere-light resolution
3. Feed that published value into the non-per-pixel direct-light path.
4. Keep the per-pixel path raw and let the shader apply point-specific
   transmittance instead.

### 4.2 Gap B — Vortex Still Uses A Legacy Direct-Lighting Contract Instead Of The Atmosphere-Light Model

**UE5.7 contract**

- Atmosphere-light resolution is authoritative.
- Non-per-pixel direct light uses baked ground transmittance.
- Per-pixel direct light uses raw outer-space illuminance and applies the LUT in
  shader.
- The light-selection contract and atmosphere contract are the same system.

**Current Vortex state**

- `SceneRenderer.cpp` builds the active directional light from
  `DirectionalLightResolver::ResolveDirectionalLights().front()`.
- `DirectionalLightResolver.cpp` still ignores the authored
  `AtmosphereLightSlot` field entirely.
- Deferred lighting keys off legacy light flags in
  `DeferredLightDirectional.hlsl`.
- Forward lighting independently applies `ComputeSunTransmittance(...)` in
  `Forward/ForwardDirectLighting.hlsli`.
- `EnvironmentLightingService` and `AtmosphereLightState` resolve atmosphere
  lights, but Stage 12 direct-light shading does not consume that resolved
  state as its authoritative source.

**Resulting behavioral defect**

- The "good" atmosphere-light model is not the authority for direct lighting.
- Forward and deferred paths disagree:
  - deferred non-per-pixel path multiplies by shader-side point transmittance
  - deferred per-pixel path skips atmosphere attenuation entirely
  - forward path always multiplies point transmittance regardless of the
    per-pixel flag
- Two-slot atmosphere-light resolution is not the direct-lighting authority.

**Required remediation**

1. Retire the sunset-relevant parts of the legacy direct-light contract.
2. Make `EnvironmentLightingService` / `AtmosphereLightState` the authoritative
   source for atmosphere-light selection and atmosphere-related light payloads.
3. Extend the Stage-12/forward directional-light publication to carry:
   - slot identity
   - resolved `direction_to_light_ws`
   - raw illuminance
   - `use_per_pixel_atmosphere_transmittance`
   - baked `transmittance_toward_sun_rgb`
4. Remove the `ResolveDirectionalLights().front()` shortcut as the sunset
   authority path.
5. Ensure forward and deferred use the exact same branch:
   - non-per-pixel: `raw_light * transmittance_toward_sun_rgb`
   - per-pixel: `raw_light`, then parity transmittance lookup in shader

### 4.3 Gap C — The Transmittance LUT Contract Still Bakes Or Reapplies Ground Occlusion

**UE5.7 contract**

- The transmittance LUT intentionally remains smooth across the horizon.
- Earth/planet occlusion is **not** baked into the LUT.
- Analytical planet shadow is reintroduced separately where needed.
- `GetTransmittance(...)` uses the raw LUT UV parameterization without the
  sky-view sub-texel remap.

**Current Vortex state**

- `AtmosphereTransmittanceLut.hlsl` truncates the integration to ground when
  `ground_dist < ray_length`.
- `AtmosphereSampling.hlsli::SampleTransmittanceLut(...)` hard-zeros below the
  geometric horizon and applies a half-texel remap.
- `AtmosphereUeMirrorCommon.hlsli::VortexSampleTransmittanceLut(...)` also
  hard-zeros below the horizon.
- `VortexIntegrateSingleScatteredLuminance(...)` then multiplies the direct
  term by analytical `planet_shadow`, duplicating occlusion on top of the
  horizon-zero sampler.

**Resulting behavioral defect**

- Horizon continuity is broken at the LUT/sampler level instead of only at the
  analytical occlusion stage.
- Sunset interpolation near the horizon can darken too early and can produce
  seam/flicker behavior UE explicitly avoids.
- The active Vortex direct-light helpers do not share the same transmittance
  contract as the parity atmosphere integrator.

**Required remediation**

1. Change `AtmosphereTransmittanceLut.hlsl` so the LUT always integrates to the
   top-atmosphere exit only:
   - no ground truncation
   - no baked planet shadow
2. Split transmittance sampling into two explicit helpers:
   - `ParityTransmittanceLutSample(...)`
     - full `mu in [-1, 1]`
     - raw UE-style UV mapping
     - no horizon branch
     - no half-texel remap
   - `AnalyticalPlanetOccludedTransmittance(...)`
     - ray/planet test with a 1 m offset
     - used only where UE does analytical occlusion
3. Rewire these consumers:
   - single-scattering integrator: parity LUT sample + analytical planet shadow
   - sun disk: analytical ground cull first, then parity LUT sample
   - per-pixel direct lighting: parity LUT sample, no baked-light floor
   - non-per-pixel direct lighting: CPU ground-transmittance result, not the
     parity LUT helper
4. Remove the old `AtmosphereSampling.hlsli` transmittance path from the active
   Vortex lighting code once parity helpers are live.

### 4.4 Gap D — Missing Sunset Jitter / TAA-Friendly Sample Offsets

**UE5.7 contract**

- Near-horizon marching uses `InterleavedGradientNoise(...)` keyed by view pixel
  and frame index.
- Variable sample count plus quadratic step distribution reduces banding.

**Current Vortex state**

- Variable sample count exists.
- Quadratic distribution exists.
- The sample start offset is still the fixed scalar
  `kVortexSegmentSampleOffset = 0.3f`.
- `sv_pos` is passed through the integrator signature but not used.
- No `StateFrameIndexMod8`-style jitter path exists in the active Vortex
  atmosphere shaders.

**Resulting behavioral defect**

- Sky-view and camera-aerial ray marching can band or shimmer at low sun angles,
  especially when sample counts are reduced by authored `trace_sample_count_scale`.

**Required remediation**

1. Publish a small frame-modulo counter into the view data consumed by the
   active atmosphere shaders.
2. Add a parity noise helper equivalent to UE's sunset jitter behavior:
   - regular views: interleaved-gradient noise by pixel + frame
   - reflection capture / deterministic special views: fixed offset
3. Replace the constant `0.3f` segment offset with the parity noise result in
   the sky-view and camera-aerial generation paths.
4. Keep the current quadratic step distribution and variable sample count.

### 4.5 Gap E — Vortex Default Authored Clamp Does Not Match UE5.7

**UE5.7 contract**

- `TransmittanceMinLightElevationAngle` defaults to `-90.0`.

**Current Vortex state**

- `SkyAtmosphere` defaults the field to `-6.0`.
- `EnvironmentViewData` invalid/default payload also uses `-6.0`.

**Resulting behavioral defect**

- Once Gap A is wired correctly, the default authored behavior will still not
  match UE unless this value is corrected.

**Required remediation**

1. Change the authored default to `-90.0`.
2. Change the invalid/default view payload to `-90.0`.
3. Update any preset/default hydration paths that currently assume `-6.0`.

## 5. Work Sequence

The remediation must execute in this order:

1. Correct the design and status docs first.
2. Add the CPU ground-transmittance function and per-light published field.
3. Replace the direct-lighting authority path so forward/deferred consume the
   atmosphere-light model instead of the legacy front-light shortcut.
4. Fix the transmittance LUT generation and sampling contract.
5. Rewire single-scatter, sun-disk, and direct-light consumers to the split
   parity/analytical helpers.
6. Add sunset-noise jitter and TAA-friendly sample offsets.
7. Re-run forward + deferred sunset validation and capture review.

Do **not** reorder 3 and 4 by first tuning the legacy direct-light helper.
That would preserve the wrong ownership boundary and leave the old path alive.

## 6. Validation Gate

This scope is not closed until the following are evidenced:

1. **Source-to-source parity check**
   - Vortex transmittance LUT no longer bakes ground occlusion
   - direct-lighting branch split matches UE's non-per-pixel vs per-pixel
     contract
   - analytical planet shadow remains in the sky/single-scatter paths
2. **Forward + deferred functional checks**
   - same sun settings in both pipelines yield matching horizon behavior
   - per-pixel flag changes behavior exactly once, in the correct place
3. **Sunset sweep**
   - elevations at `+1 deg`, `0 deg`, `-1 deg`, `-6 deg`, and `-12 deg`
   - non-per-pixel and per-pixel modes
   - no sudden direct-light blackout at the authored non-per-pixel clamp
   - no raw-light leak when per-pixel mode is enabled
4. **Capture-backed proof**
   - sky-view and camera-aerial products remain stable at/below horizon
   - no horizon seam from transmittance LUT interpolation
   - no below-ground light leak
5. **Residual gap statement**
   - if runtime capture or UAT is not rerun, status remains `in_progress`
   - the missing capture/UAT delta must be listed explicitly

## 7. Non-Negotiable Preservation Rules

The following existing Vortex mechanisms already match the target shape closely
enough and must not be regressed while fixing the sunset lane:

- translated planet frame publication
- 5 m sky-camera snap above the planet
- split-horizon sky-view parameterization with sub-texel clamp
- camera-aerial below-horizon mirroring
- sun-disk soft edge and uniform chromaticity-preserving clamp
- multi-scattering full-range `mu` guards
