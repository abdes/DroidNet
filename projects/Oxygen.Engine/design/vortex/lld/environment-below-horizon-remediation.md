# Environment Sun-Below-Horizon Remediation

**Phase:** 4D / 4A integration blocker
**Status:** `in_progress`

## 1. Why This Document Exists

The current Vortex atmosphere lane is **not** parity-closed for the specific
UE5.7 problem space where the atmosphere light's zenith cosine is `<= 0`
(sun/moon at the horizon or below it).

The earlier Vortex closeout work covered translated planet-frame publication,
sky-view generation, camera-aerial generation, and Stage-15 consumption on the
active path. The later direct-light authority remediation and the UE5.7
km-internal atmosphere-space refactor closed a large part of that scope.

The remaining work is now narrower. It is focused on the exact UE5.7 sunset
contracts that still prevent:

- transmittance sampling policy drift between the parity integrator and the
  active lighting helpers
- bilinear seam/flicker from horizon-branching transmittance helpers
- sunset banding from unjittered low-sample ray marching
- default authored horizon-floor behavior that still differs from UE5.7

This document removes the already-landed tasks and defines only the remaining
remediation.

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

Items 1, 2, 5, 6, and 8 already exist in the active Vortex path and must be
preserved. Item 3 is implemented at LUT-generation time, but not yet mirrored
consistently by all active sampling helpers. Item 4 is implemented in parts of
the parity path, but not yet isolated cleanly from the remaining legacy
samplers. Item 7 is still open.

## 3. Current Vortex Audit Summary

The active Vortex code already mirrors these UE behaviors:

- translated planet frame plus a 1 m parity snap / radius offset in
  `src/Oxygen/Vortex/Environment/EnvironmentLightingService.cpp` and
  `src/Oxygen/Core/Types/Atmosphere.h`
- UE5.7-style km-internal atmosphere-space contracts and CPU-side boundary
  conversion in
  `src/Oxygen/Core/Types/Atmosphere.h`,
  `src/Oxygen/Vortex/Types/EnvironmentStaticData.h`,
  `src/Oxygen/Vortex/Types/EnvironmentViewData.h`,
  and the active Vortex atmosphere shaders
- sky-view split-horizon parameterization plus sub-texel clamping in
  `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Environment/AtmosphereParityCommon.hlsli`
- camera-aerial below-horizon mirroring plus near-slice fade in
  `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Environment/AtmosphereCameraAerialPerspective.hlsl`
  and
  `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Environment/AerialPerspective.hlsli`
- sun-disk analytical ground cull, soft edge, and luminance clamp in
  `.../Sky.hlsl`
- multi-scattering full-range `mu` guards in
  `.../AtmosphereMultiScatteringLut.hlsl` and
  `.../AtmosphereUeMirrorCommon.hlsli`
- CPU ground-transmittance baking plus per-atmosphere-light
  `transmittance_toward_sun_rgb` publication in
  `src/Oxygen/Vortex/Environment/Internal/AtmosphereLightTranslation.h`,
  `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp`,
  and the Stage-12 lighting bindings/tests
- atmosphere-light authoritative direct-light publication, including
  non-per-pixel baked transmittance and per-pixel raw-light branching, in
  `src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp`,
  `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Lighting/AtmosphereDirectionalLightShared.hlsli`,
  `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Lighting/ForwardDirectLighting.hlsli`,
  and `src/Oxygen/Vortex/Test/LightingService_test.cpp`
- transmittance LUT generation to top-atmosphere exit only, without ground
  truncation, in
  `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Environment/AtmosphereTransmittanceLut.hlsl`

The remaining gaps are concentrated in the active transmittance-sampling
contract, local-fog compose leakage onto far-depth sky pixels, and the
default authored light-elevation clamp.

### 4.2A Urgent Visual Regression — Local Fog Compose Leaks Into Far-Depth Sky Pixels

**UE5.7 contract**

- Local fog / fog-volume composition must not inject emissive or inscattering
  contributions onto sky pixels that carry the far-depth clear value.
- Sky / atmosphere own the far-background path; local fog needs a valid scene
  surface termination depth before evaluating its ray integral.

**Current Vortex state**

- `LocalFogVolumeCompose.hlsl` samples scene depth and reconstructs a world
  position unconditionally.
- At sky pixels, that depth is the far reference, so the reconstructed point is
  an arbitrary far-plane position instead of a real scene surface.
- `GetLocalFogVolumeContribution(...)` then integrates local fog volumes
  against that fake far point and adds emissive / inscattering over the sky.
- `LocalFogVolumeCommon.hlsli` also lit local fog from raw directional-light
  luminance, bypassing the atmosphere-aware directional-light publication that
  UE feeds into local fog.

**Resulting behavioral defect**

- Bright local-fog blobs and halos appear on the sky dome at negative sun
  elevations, even when the sun position itself is correct.
- This contaminates sunset diagnosis because the dominant visible artifact is
  not from the sky-atmosphere LUT path at all.

**Required remediation**

1. Add a far-depth early-out in
   `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Environment/LocalFogVolumeCompose.hlsl`
   immediately after `SampleSceneDepth(...)`.
2. If the sampled depth equals the projection-aware far reference, return
   `float4(0,0,0,1)` and skip world-position reconstruction and local-fog
   integration.
3. Mirror UE5.7 local-fog directional-light setup:
   use the published directional-light color and multiply by the baked
   `transmittance_toward_sun_rgb` when the light is atmosphere-authoritative,
   including the per-pixel-transmittance approximation path UE uses for local
   fog to avoid a transmittance texture sample in the fog shader.
4. Preserve Oxygen coordinate conventions; this is a depth/light-contract fix,
   not a handedness or basis change.

**Grep / source invariants**

- `LocalFogVolumeCompose.hlsl` must contain a projection-aware far-depth helper
  and an early-out on sky pixels before `ReconstructWorldPosition(...)`.

## 4. Gap Inventory

### 4.1 Gap A — Active Transmittance Sampling Helpers Still Diverge From The UE5.7 Sunset Contract

**UE5.7 contract**

- The transmittance LUT intentionally remains smooth across the horizon.
- Earth/planet occlusion is **not** baked into the LUT.
- Analytical planet shadow is reintroduced separately where needed.
- `GetTransmittance(...)` uses the raw LUT UV parameterization without the
  sky-view sub-texel remap.

**Current Vortex state**

- `AtmosphereTransmittanceLut.hlsl` now integrates to the top-atmosphere exit
  only and no longer bakes ground truncation.
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

1. Split active transmittance sampling into two explicit helpers in a new
   `Shaders/Vortex/Services/Environment/ParityTransmittance.hlsli`:

   ```hlsl
   // Mirror UE5.7 `GetTransmittance` in SkyAtmosphereCommon.ush:
   // - mu in [-1, 1], full LUT domain
   // - raw `getTransmittanceLutUvs` mapping (no `FromUnitToSubUvs` remap)
   // - no horizon branch, no hard zero below mu = cos_horizon
   float3 ParityTransmittanceLutSample(
       uint lut_slot,
       float lut_width, float lut_height,
       float cos_zenith,
       float altitude_km,
       float planet_radius_km,
       float atmosphere_height_km);

   // Mirror UE5.7 `GetAtmosphereTransmittance` in SkyAtmosphereCommon.ush:
   //  1. RaySphereIntersectNearest against the virtual planet using
   //     `PLANET_RADIUS_OFFSET` (= 1 m in km) as the safety epsilon
   //  2. return 0 if the ray hits the planet
   //  3. otherwise call ParityTransmittanceLutSample with the geometry at the
   //     sample point
   float3 AnalyticalPlanetOccludedTransmittance(
       float3 planet_center_to_world_pos_km,
       float3 world_dir,
       uint lut_slot,
       float lut_width, float lut_height,
       float planet_radius_km,
       float atmosphere_height_km);
   ```

   Invariants enforced by code review:
   - `ParityTransmittanceLutSample` must not contain any `cos_zenith <
     cos_horizon` branch, must not call `ApplyHalfTexelOffset` /
     `FromUnitToSubUvs`, and must not multiply by any analytical planet-shadow
     factor.
   - `AnalyticalPlanetOccludedTransmittance` is the only helper allowed to
     return zero for geometric occlusion.

2. Rewire these consumers (exact files and symbols):

   - Single-scattering integrator
     `Shaders/Vortex/Services/Environment/AtmosphereUeMirrorCommon.hlsli::
     VortexIntegrateSingleScatteredLuminance` — sun term uses
     `ParityTransmittanceLutSample` at the sample point and multiplies by the
     analytical `planet_shadow` factor already computed locally. Remove any
     `VortexSampleTransmittanceLut` horizon-zero path that duplicates that
     occlusion.

   - Sun disk
     `Shaders/Vortex/Services/Environment/Sky.hlsl::GetLightDiskLuminance` —
     first the analytical ground cull (`RaySphereIntersectNearest` > 0 ⇒
     return 0), then `ParityTransmittanceLutSample` at the camera altitude
     with `cos_zenith = dot(sun_dir, up)`. Do not call
     `AnalyticalPlanetOccludedTransmittance` here because the ground cull is
     already done; UE does the two in this order.

   - Per-pixel direct lighting
     `Shaders/Vortex/Services/Lighting/AtmosphereDirectionalLightShared.hlsli`
     and `ForwardDirectLighting.hlsli` per-pixel branch —
     `ParityTransmittanceLutSample` at the shaded surface point. No
     `TransmittanceMinLightElevationAngle` clamp on this branch.

   - Non-per-pixel direct lighting
     Same files, non-per-pixel branch — continue to consume
     `atmosphere_light*.transmittance_toward_sun_rgb` from Stage-12 bindings.
     This branch is the only place where the authored
     `TransmittanceMinLightElevationAngle` clamp is evaluated (on the CPU, at
     bake time).

3. Remove or retire the legacy samplers once the parity helpers are live:

   - `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Environment/AtmosphereSampling.hlsli::SampleTransmittanceLut`
     and `::SampleTransmittanceOpticalDepthLut` — either delete them if no
     active callers remain, or rename them to a clearly marked legacy helper
     surface if a non-sunset path still depends on them. Confirm zero inbound
     references from the active sunset-relevant consumers before deletion.
   - `Shaders/Vortex/Services/Environment/AtmosphereUeMirrorCommon.hlsli::
     VortexSampleTransmittanceLut` — delete once all callers moved to
     `ParityTransmittanceLutSample`.

**Grep invariants (must all return empty after this gap is closed)**

- `rg -n "SampleTransmittanceLut|SampleTransmittanceOpticalDepthLut|VortexSampleTransmittanceLut" src/Oxygen/Graphics/Direct3D12/Shaders/Vortex`
- `rg -n "cos_zenith < cos_horizon" src/Oxygen/Graphics/Direct3D12/Shaders/Vortex`
- `rg -n "ApplyHalfTexelOffset|FromUnitToSubUvs\s*\(.*transmittance" src/Oxygen/Graphics/Direct3D12/Shaders/Vortex`

### 4.2 Gap B — Closed By UE5.7 Audit: Do Not Jitter LUT Builders

**UE5.7 contract**

- `InterleavedGradientNoise(...)` is enabled in UE5.7 for the full
  sky-ray-marching pixel shader (`FRenderSkyAtmospherePS`) through
  `PER_PIXEL_NOISE=1`.
- UE5.7 does **not** enable `PER_PIXEL_NOISE` for
  `FRenderSkyViewLutCS` or `FRenderCameraAerialPerspectiveVolumeCS`.
- The active sky-view LUT builder and camera-aerial LUT builder therefore keep
  the fixed `DEFAULT_SAMPLE_OFFSET = 0.3f`.

**Current Vortex state**

- Variable sample count exists where UE uses it.
- Quadratic distribution exists where UE uses it.
- The active LUT builders now match UE and keep the fixed `0.3f` segment
  offset.

**Correct remediation**

- Do not add a frame-index payload or active LUT jitter path for this lane.
- Keep `kSegmentSampleOffset = 0.3f` in the active sky-view LUT,
  camera-aerial LUT, multi-scattering LUT, and distant-sky-light LUT builders.
- If a future Vortex full sky ray-marching pixel shader is introduced, its
  noise policy must be audited separately against UE's `FRenderSkyAtmospherePS`
  permutation setup instead of being back-propagated into the LUT builders.

**Grep invariants**

- `rg -n "ComputeSunsetSegmentOffset|deterministic_view|state_frame_index_mod8" src/Oxygen/Graphics/Direct3D12/Shaders/Vortex src/Oxygen/Vortex`
  must return empty for the active atmosphere path.
- `rg -n "SampleSegmentOffset = kSegmentSampleOffset" src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Environment`
  must show the active LUT builders.

### 4.3 Gap C — Vortex Default Authored Clamp Does Not Match UE5.7

**UE5.7 contract**

- `TransmittanceMinLightElevationAngle` defaults to `-90.0`.

**Current Vortex state**

- `src/Oxygen/Scene/Environment/SkyAtmosphere.h` defaults the authored field
  to `-6.0F`.
- `src/Oxygen/Vortex/Environment/Types/AtmosphereModel.h` still carries the
  same `-6.0F` runtime-model default.
- `src/Oxygen/Vortex/Types/EnvironmentViewData.h` and
  `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Contracts/EnvironmentViewData.hlsli`
  still publish `-6.0` in the invalid/default view payload.
- `src/Oxygen/Data/PakFormat_world.h`, DemoShell environment settings defaults,
  and the shipped example `demo_settings.json` files also still carry `-6.0`.

**Resulting behavioral defect**

- The authored default behavior still does not match UE when the non-per-pixel
  clamp path is active.

**Required remediation**

1. Change the authored default to `-90.0`. Exact sites:
   - `src/Oxygen/Scene/Environment/SkyAtmosphere.h` — field
     `transmittance_min_light_elevation_deg_ = -6.0F`
   - `src/Oxygen/Vortex/Environment/Types/AtmosphereModel.h` — field
     `transmittance_min_light_elevation_deg { -6.0F }`
   - `src/Oxygen/Data/PakFormat_world.h` — field
     `transmittance_min_light_elevation_deg = -6.0F`
   - `Examples/DemoShell/Services/EnvironmentSettingsService.h` — both
     `AtmosphereCanonicalState::transmittance_min_light_elevation_deg` and the
     persisted UI default `transmittance_min_light_elevation_deg_`
   - `src/Oxygen/Cooker/Import/Schemas/oxygen.scene-descriptor.schema.json` —
     no `default` key is currently set (the field is permissive); leave the
     schema alone but confirm.

2. Change the invalid/default view payload to `-90.0`:
   - `src/Oxygen/Vortex/Types/EnvironmentViewData.h` —
     `trace_sample_scale_transmittance_min_light_elevation_holdout_mainpass`
     default `glm::vec4 { 1.0F, -6.0F, 0.0F, 1.0F }` → `{ 1.0F, -90.0F, 0.0F,
     1.0F }`.
   - `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Contracts/EnvironmentViewData.hlsli`
     — matching invalid/default HLSL payload `float4(1.0f, -6.0f, 0.0f, 1.0f)`
     → `float4(1.0f, -90.0f, 0.0f, 1.0f)`.

3. Update preset/default hydration paths and fixtures:
   - `src/Oxygen/Cooker/Test/Import/SceneDescriptorJsonSchema_test.cpp` — the
     `-6.0` literal on the fixture at
     `"transmittance_min_light_elevation_deg": -6.0` is only a round-trip
     sample; change it to `-90.0` so fixtures do not silently pin the old
     default.
   - Shipped example presets under `Examples/*/demo_settings.json` that set
     this key must be updated from `-6.0` to `-90.0`.
   - DemoShell persisted/default state hydration in
     `Examples/DemoShell/Services/EnvironmentSettingsService.cpp` must continue
     to load existing user values, but its default-initialized state must move
     to `-90.0`.

4. Do NOT change the unrelated `-6.0F` literals found elsewhere in the tree:
   - `src/Oxygen/Vortex/PostProcess/Types/PostProcessConfig.h` and
     `PostProcessFrameBindings.h` → `auto_exposure_min_ev` (EV stops, not
     elevation).
   - `src/Oxygen/Vortex/Test/SceneRendererDeferredCore_test.cpp` and
     `SceneCameraViewResolver_test.cpp` → position components and masked-far
     fixtures unrelated to atmosphere.

**Grep invariants**

- `rg -n "transmittance_min_light_elevation_deg" src/Oxygen/Scene/Environment/SkyAtmosphere.h src/Oxygen/Vortex/Environment/Types/AtmosphereModel.h src/Oxygen/Data/PakFormat_world.h Examples/DemoShell/Services/EnvironmentSettingsService.h Examples/DemoShell/Services/EnvironmentSettingsService.cpp Examples -g "*.json" -g "*.h" -g "*.cpp"`
  must show `-90.0` / `-90.0F` for every default-bearing site listed above.
- `rg -n "trace_sample_scale_transmittance_min_light_elevation_holdout_mainpass| -6\.0F| -6\.0f" src/Oxygen/Vortex/Types/EnvironmentViewData.h src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Contracts/EnvironmentViewData.hlsli`
  must no longer show the atmosphere invalid/default payload at `-6.0`.

### 4.4 Gap D — Determinism Carve-Outs (Must Not Drift)

This is an explicit non-jitter, non-remap carve-out to prevent future
regressions from over-applying Gap B or over-aligning with legacy samplers.

**UE5.7 contract**

- The transmittance LUT is deterministic across frames; no per-frame jitter.
- The multi-scattering LUT is deterministic; UE uses a fixed hemisphere
  sampling pattern.
- The sky-view LUT UV parameterization deliberately uses the sub-texel remap
  (`FromUnitToSubUvs`) at the LUT texture boundary to avoid zenith-derivative
  artifacts. This is **unrelated** to the transmittance LUT and must be kept
  on the sky-view path.

**Required invariants**

1. `AtmosphereTransmittanceLut.hlsl` must not consume
   `state_frame_index_mod8` and must not call `InterleavedGradientNoise` or
   `ComputeSunsetSegmentOffset`.
2. `AtmosphereMultiScatteringLut.hlsl` must not consume
   `state_frame_index_mod8` and must not call the jitter helper.
3. The sky-view UV sub-texel remap in `AtmosphereParityCommon.hlsli::
   SkyViewLutParamsToUv` (`VortexFromUnitToSubUvs`) must be preserved; it is
   not the same thing as the transmittance LUT half-texel remap that Gap A
   removes.
4. Reflection-capture and any future orthographic shadow-capture views must
   pass `deterministic_view = true` into the jitter helper so the segment
   offset falls back to `0.3` exactly (matches UE's fixed path).

**Grep invariants**

- `rg -n "state_frame_index_mod8|ComputeSunsetSegmentOffset" src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Environment/AtmosphereTransmittanceLut.hlsl`
  must be empty.
- Same grep against `AtmosphereMultiScatteringLut.hlsl` must be empty.
- `rg -n "VortexFromUnitToSubUvs" src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Environment/AtmosphereParityCommon.hlsli`
  must still show at least one use site (the sky-view UV seam guard).

## 5. Work Sequence

The remaining remediation must execute in this order:

1. Correct the design and status docs first.
2. Land the active transmittance-sampling split and remove the remaining
   horizon-policy drift from the active helpers.
3. Rewire single-scatter, sun-disk, and per-pixel direct-light consumers to the
   split parity/analytical helpers.
4. Add the local-fog far-depth sky guard so fog volumes cannot leak onto the
   sky dome.
5. Correct the authored/default clamp to `-90.0` and update invalid payloads
   plus presets/default hydration paths.
6. Re-run forward + deferred sunset validation and capture review.

Do **not** re-open the already-landed direct-light authority work by adding a
new legacy bridge path. The active direct-light branch already consumes the
atmosphere-light model and baked ground transmittance where intended; the
remaining work is to bring every active transmittance lookup onto the same
parity helper split.

## 6. Validation Gate

This scope is not closed until the following are evidenced:

1. **Source-to-source parity check**
   - Vortex transmittance LUT still integrates to top-atmosphere only
   - active transmittance samplers no longer inject horizon policy or
     half-texel remaps where UE does not
   - direct-lighting branch split continues to match UE's non-per-pixel vs
     per-pixel contract
   - analytical planet shadow remains only in the sky/single-scatter paths
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

### 6.1 Capture Observables (What To Look For Frame-By-Frame)

At each sun elevation in the sweep, record and check the following from
the RenderDoc capture at frame 10 (the repo baseline):

1. Transmittance LUT texture dump
   - RGB values vary smoothly across the full U range (`mu in [-1, 1]`)
   - no abrupt zero band near `mu = cos_horizon`
   - no visible half-texel seam at `U = 0` or `U = 1`

2. Sky-view LUT texture dump
   - horizon seam at `V = 0.5` is continuous
   - near-horizon colors show expected sunset reddening and no banding
   - azimuth sub-texel clamp is intact at `U = 0` and `U = 1`

3. Camera-aerial volume (selected near/mid/far slices)
   - no purple/black voxels below the horizon
   - voxels behind the virtual planet still receive the mirrored-across-
     tangent-point reconstruction

4. Sky pass output
   - sun disk disappears cleanly when `dot(sun_dir, up) < -sin(disk_half_angle)`
     with no pop
   - soft edge around the disk is preserved at `+1 deg`

5. Forward / deferred direct-light delta
   - non-per-pixel branch: at authored `TransmittanceMin = -90 deg`, the
     direct-light term tapers continuously with the CPU-baked
     `transmittance_toward_sun_rgb` and never hard-zeros until the sun is
     geometrically below the horizon
   - per-pixel branch: direct-light term matches the parity LUT sample at the
     shaded point; no additional clamp

### 6.2 Backout Plan

Each gap is independently revertible. If a regression surfaces in review:

- Gap A rollback: delete the new `ParityTransmittance.hlsli`, restore the
  horizon-zero samplers. Direct-light branch already tolerated the duplicated
  occlusion, so this is a safe temporary revert.
- Gap B rollback: remove the far-depth early-out from
  `LocalFogVolumeCompose.hlsl`. This intentionally reopens the fog-over-sky
  leak and is only acceptable as a temporary diagnostic revert.
- Gap C rollback: revert the three default initializers to `-6.0F`. The
  change is isolated to authored defaults; no structural surface changes.
- Gap D is invariant, not an implementation — nothing to roll back, only
  review gates to enforce.

## 7. Non-Negotiable Preservation Rules

The following existing Vortex mechanisms already match the target shape closely
enough and must not be regressed while fixing the sunset lane:

- translated planet frame publication
- 1 m sky-camera snap / `PLANET_RADIUS_OFFSET` behavior above the planet
- split-horizon sky-view parameterization with sub-texel clamp
- camera-aerial below-horizon mirroring
- sun-disk soft edge and uniform chromaticity-preserving clamp
- multi-scattering full-range `mu` guards
- km-internal atmosphere-space units and CPU-only boundary conversion
- atmosphere-light authoritative direct-light publication and baked
  ground-transmittance payloads
