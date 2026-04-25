# VTX-M04D.3 — UE5.7 Local Fog Volume Parity

Status: `validated`

## Purpose

VTX-M04D.3 closes local fog volumes before VTX-M04D.4 injects local-fog
participating media into volumetric fog. The existing Oxygen path has Stage 14
tiled culling and Stage 15 composition, but that is not enough to claim UE5.7
parity or to use local fog as a trusted volumetric-fog input.

## UE5.7 Grounding

Implementation and review must check these UE5.7 source families:

| Area | UE5.7 reference |
| --- | --- |
| Component authoring/defaults | `Engine/Source/Runtime/Engine/Classes/Components/LocalFogVolumeComponent.h` |
| Runtime scene proxy | `Engine/Source/Runtime/Engine/Public/LocalFogVolumeSceneProxy.h`, `Engine/Source/Runtime/Engine/Private/LocalFogVolumeSceneProxy.cpp` |
| Renderer setup, sorting, capping, CVars | `Engine/Source/Runtime/Renderer/Private/LocalFogVolumeRendering.{h,cpp}` |
| Tiled culling shader | `Engine/Shaders/Private/LocalFogVolumes/LocalFogVolumeTiledCulling.usf` |
| Analytical integral and lighting | `Engine/Shaders/Private/LocalFogVolumes/LocalFogVolumeCommon.ush` |
| Splat/compose pass | `Engine/Shaders/Private/LocalFogVolumes/LocalFogVolumeSplat.usf` |
| Height-fog integration | `Engine/Shaders/Private/HeightFogPixelShader.usf` |
| Volumetric-fog media injection | `Engine/Shaders/Private/VolumetricFog.usf` |

## Oxygen Divergences

- Oxygen uses meters and translated world space instead of UE's centimeter-space
  shader convention. UE's `500 cm` base local-fog sphere maps to Oxygen's
  `5 m` base radius, and UE's default `2000 cm` global start distance maps to
  Oxygen's `20 m`.
- Oxygen uses bindless structured buffers/textures instead of UE RDG uniform
  buffer bindings. The CPU/HLSL payload must remain lockstep even when the
  binding mechanism differs.
- Oxygen currently has a dedicated Stage 15 local-fog pass. UE can either
  compose LFV in the height-fog pass or render an independent tiled pass, and
  can inject LFV into volumetric fog. Oxygen must not claim those integration
  modes until each mode has implementation and proof.
- Oxygen's HZB path uses the generic Stage 5 Screen HZB publication. UE's
  implementation uses the furthest HZB through renderer view data.

## Required Work

1. Preserve UE-shaped authoring defaults and sanitize authored values before GPU
   packing: non-negative densities, non-negative emissive, clamped albedo,
   clamped scattering distribution, and `[-127, 127]` sort priority.
2. Preserve UE's CPU instance sorting and capping contract:
   priority first, distance second, stable source index last, then discard the
   lowest-priority/farthest excess instances when the per-view/tile cap is hit.
3. Preserve the tiled culling contract over the view rectangle: tile planes,
   optional furthest-HZB rejection, tile count slice, per-tile instance index
   slices, occupied-tile buffer, and indirect draw argument generation.
4. Preserve analytical local-fog evaluation: radial integral, height integral,
   coverage/transmittance combination, lighting, albedo, and emissive behavior.
5. Preserve composition order: height fog, then local fog, then volumetric fog
   unless a later integration mode explicitly composes LFV inside the height or
   volumetric fog pass.
6. Add validation for local-fog authoring/clamping, cap behavior, HZB and no-HZB
   culling states, sub-viewport tile resolution, sky-depth exclusion, and
   RenderDoc/runtime proof.

## Current Slices

The first implementation slice corrects the local-fog authoring/cap contract
before volumetric integration:

- Record the UE5.7 source-to-target mapping in Vortex docs/status.
- Align Oxygen's local-fog max-instance cap with UE's valid range of `1..256`.
- Sanitize authored local-fog values before CPU packing so shader payloads
  cannot receive negative density, invalid phase/albedo, or out-of-range sort
  priority values.
- Add focused tests for authoring clamps and per-view cap behavior.

The second implementation slice corrects the Stage 14/15 indirect draw contract
against UE5.7:

- UE `LocalFogVolumeTiledCulling.usf` writes a single draw-indirect argument
  packet and increments the packet's instance count for each occupied tile.
  UE `LocalFogVolumeSplat.usf` then uses `SV_InstanceID` to index the occupied
  tile buffer in one tiled draw.
- Oxygen now follows that shape: Stage 14 initializes
  `LocalFogTileDrawArgs = {6, 0, 0, 0}`, increments `args[1]` for occupied
  tiles, and Stage 15 executes one indirect draw command without a separate
  count buffer.
- RenderDoc proof tooling now validates the UE-shaped draw-args packet and
  records the captured occupied-tile instance count.

## Validation Closure

VTX-M04D.3 is validated for Oxygen's analytical local-fog volume path as of
2026-04-25:

- implementation exists for UE-shaped authoring sanitization, sorting/capping,
  tiled culling, single draw-indirect tiled rendering, analytical integral,
  lighting/albedo/emissive composition, and far-depth/sky exclusion;
- focused Scene and EnvironmentLightingService tests pass, including authoring
  clamps, cap behavior, HZB/no-HZB state, and sub-viewport tile resolution;
- ShaderBake/catalog validation passes for the local-fog shader changes;
- VortexBasic runtime/capture validation proves local-fog instance publication,
  HZB-backed tiled culling, UE-shaped draw args, Stage 15 local-fog SceneColor
  contribution, far-depth no-op behavior, Stage 15 ordering, and final
  presentation;
- VTX-M04D.4 remains the owner for injecting local-fog participating media into
  volumetric fog.
