# EnvironmentLightingService LLD

**Phase:** 4D
**Deliverable:** D.12
**Status:** `needs-implementation`

## Mandatory Vortex Rule

- For Vortex planning and implementation, `Oxygen.Renderer` is legacy dead
  code. It is not production, not a reference implementation, not a fallback,
  and not a simplification path for any Vortex task.
- Every Vortex task must be designed and implemented as a new Vortex-native
  system that targets maximum parity with UE5.7, grounded in
  `F:\Epic Games\UE_5.7\Engine\Source\Runtime` and
  `F:\Epic Games\UE_5.7\Engine\Shaders`.
- No Vortex task may be marked complete until its parity gate is closed with
  explicit evidence against the relevant UE5.7 source and shader references.
- If maximum parity cannot yet be achieved, the task remains incomplete until
  explicit human approval records the accepted gap and the reason the parity
  gate cannot close.

## Local Fog Correction (2026-04-19)

- The current Vortex local-fog runtime path is **not** at UE5.7 parity and
  must not be described as an already-landed feature that only needs
  hardening.
- The authoritative local-fog contract is the UE5.7 implementation in:
  - `F:\Epic Games\UE_5.7\Engine\Source\Runtime\Renderer\Private\LocalFogVolumeRendering.h`
  - `F:\Epic Games\UE_5.7\Engine\Source\Runtime\Renderer\Private\LocalFogVolumeRendering.cpp`
  - `F:\Epic Games\UE_5.7\Engine\Shaders\Private\LocalFogVolumes\LocalFogVolumeCommon.ush`
  - `F:\Epic Games\UE_5.7\Engine\Shaders\Private\LocalFogVolumes\LocalFogVolumeTiledCulling.usf`
  - `F:\Epic Games\UE_5.7\Engine\Shaders\Private\LocalFogVolumes\LocalFogVolumeSplat.usf`
- The Vortex local-fog rewrite must mirror the UE5.7 per-view pack/sort/cap
  flow, tile-texture culling contract, analytical integral, and composition
  ordering.
- The parity gate remains open until all required integration points are
  evidenced:
  - Stage 14 tiled culling path
  - Stage 15 dedicated splat path
  - height-fog inline composition parity when enabled
  - volumetric-fog injection parity when volumetric fog is enabled
- If Vortex still lacks the upstream height-fog or volumetric-fog subsystems
  needed for those integration points, that is an explicit parity blocker, not
  a reason to call local fog complete.

## 1. Scope and Intent

### 1.1 Goal

Define the full Vortex environment-family design across the approved scope:

- sky atmosphere
- aerial perspective
- exponential height fog
- local fog volumes
- volumetric fog
- sky-light coupling
- dual atmosphere-light support

Explicitly excluded from this design/program:

- volumetric clouds
- heterogeneous volumes

### 1.2 What This Document Must Enable

This document is written so a junior engineer can implement the feature set
without making hidden architecture decisions. It therefore covers:

- authored data ownership in `src/Oxygen/Scene`
- serialized data ownership in `src/Oxygen/Data`
- runtime/application ownership in `Examples/DemoShell`
- renderer ownership in `src/Oxygen/Vortex`
- verification responsibilities

## 2. Architectural Position

### 2.1 Family Ownership

`EnvironmentLightingService` is the renderer-family owner for:

1. atmosphere state translation
2. atmosphere/fog/light environment products
3. stage execution for Stage 14 and Stage 15 environment work

It is **not** the owner of authored scene data. Scene authoring lives in
`src/Oxygen/Scene`, and serialized authoring lives in `src/Oxygen/Data`.

### 2.2 Stage Mapping

Vortex keeps the fixed 23-stage frame structure.

Environment-family execution is split as follows:

- **pre-stage family preparation**
  - update persistent LUTs and capture products
  - prepare per-view atmosphere resources
  - prepare local-fog and volumetric-fog per-view data
- **Stage 14**
  - volumetric-media preparation and execution
  - local fog volume culling through mandatory Screen HZB-backed tiled classification and optional injection
  - volumetric fog froxel integration
- **Stage 15**
  - sky-atmosphere rendering
  - height-fog composition
  - local-fog-volume composition when not injection-only
  - final environment-family fog-over-scene composition

### 2.3 Boundary Rules

- Stage 12 remains direct-lighting only.
- Stage 13 remains the future canonical owner for indirect environment
  evaluation, reflections, AO, and ambient-bridge retirement.
- Stage 14 and Stage 15 are both owned by the Environment family.
- Sky-light coupling required by atmosphere/fog/local-volumetric media is part
  of the Environment family even before Stage 13 exists.
- Clouds remain excluded; this document must not silently pull them into scope.

## 3. Environment Feature Summary

The environment family implements:

- Sky atmosphere as a physical parameter model with:
  - Rayleigh, Mie, and ozone absorption layers
  - planet geometry and ground albedo
  - configurable multi-scattering
- Multiple atmosphere products:
  - transmittance LUT
  - multi-scattering LUT
  - sky-view LUT
  - camera aerial-perspective volume
  - distant sky-light LUT
- **Two** atmosphere-light slots in the view uniform buffer.
- Height fog with:
  - two fog layers
  - directional inscattering
  - cubemap inscattering
  - sky-atmosphere contribution
  - volumetric-fog coupling
- Local fog volumes as a separate authored and rendered system with:
  - per-instance transforms
  - depth/HZB-aware tiled culling
  - analytic local media evaluation
  - directional-light and sky-light scattering
- Volumetric fog as a froxel system with temporal reprojection and light
  injection.
- Sky-light coupling through distant-sky-light products, view uniforms, and
  volumetric/local-fog shading.

## 4. Authoring Model in `src/Oxygen/Scene`

This section defines the authored source of truth. The renderer must consume
these scene-authored objects; it must not invent equivalent hidden state.

### 4.1 Scene-Global vs Spatially Local Split

The current `SceneEnvironment` design is scene-global:

- `SceneEnvironment` is a standalone composition
- it hosts `EnvironmentSystem` components
- it is suitable for scene-global environment systems

This remains correct for:

- `Sun`
- `SkyAtmosphere`
- `Fog`
- `SkyLight`
- `SkySphere`
- `VolumetricClouds`
- `PostProcessVolume`

It is **not** correct for local fog volumes, because local fog volumes are:

- spatially local
- multiple per scene
- transform-driven
- individually sorted and culled

### 4.2 Required Scene Types

#### 4.2.1 `scene::environment::SkyAtmosphere`

Current `SkyAtmosphere` is too small. Expand it to cover the target design.

Required authored fields:

- `transform_mode`
- `planet_radius_m`
- `atmosphere_height_m`
- `ground_albedo_rgb`
- `rayleigh_scattering_rgb`
- `rayleigh_scale_height_m`
- `mie_scattering_rgb`
- `mie_absorption_rgb`
- `mie_scale_height_m`
- `mie_anisotropy`
- `ozone_absorption_rgb`
- `ozone_density_profile`
- `multi_scattering_factor`
- `sky_luminance_factor_rgb`
- `sky_and_aerial_perspective_luminance_factor_rgb`
- `aerial_perspective_distance_scale`
- `aerial_scattering_strength`
- `aerial_perspective_start_depth_m`
- `height_fog_contribution`
- `trace_sample_count_scale`
- `transmittance_min_light_elevation_deg`
- `sun_disk_enabled`
- `holdout`
- `render_in_main_pass`

Current file to extend:

- [SkyAtmosphere.h](</F:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Scene/Environment/SkyAtmosphere.h>)

Naming note:

- design name `ozone_absorption_rgb` maps to the current field
  `absorption_rgb_`
- design name `ozone_density_profile` maps to the current field
  `ozone_density_`

Do **not** add duplicate authored fields for these; rename or document the
existing fields during implementation.

#### 4.2.2 `scene::environment::Fog`

Current `Fog` is much too small. It must become the authored source
for both analytic height fog and volumetric-fog controls.

`FogModel` cannot remain authoritative in its current meaning because
volumetric fog is not an alternative to height fog — it is an additional system
that coexists with height fog. The design therefore chooses:

- keep `FogModel` only as a **legacy compatibility input**
- make the canonical runtime meaning:
  - `enable_height_fog`
  - `enable_volumetric_fog`

Migration rule:

- legacy `kExponentialHeight` -> `enable_height_fog=true`,
  `enable_volumetric_fog=false`
- legacy `kVolumetric` -> `enable_height_fog=true`,
  `enable_volumetric_fog=true`

The widened `Fog` component must therefore not treat volumetric fog as
mutually exclusive with exponential height fog.

Required authored fields:

- `enabled`
- legacy compatibility only:
  - `model`
- canonical runtime flags:
  - `enable_height_fog`
  - `enable_volumetric_fog`
- primary fog layer:
  - `fog_density`
  - `fog_height_falloff`
  - `fog_height_offset`
- secondary fog layer:
  - `second_fog_density`
  - `second_fog_height_falloff`
  - `second_fog_height_offset`
- `fog_inscattering_luminance`
- `sky_atmosphere_ambient_contribution_color_scale`
- `inscattering_color_cubemap_resource`
- `inscattering_color_cubemap_angle`
- `inscattering_texture_tint`
- `fully_directional_inscattering_color_distance`
- `non_directional_inscattering_color_distance`
- `directional_inscattering_luminance`
- `directional_inscattering_exponent`
- `directional_inscattering_start_distance`
- `fog_max_opacity`
- `start_distance`
- `end_distance`
- `fog_cutoff_distance`
- volumetric fog settings:
  - `volumetric_fog_scattering_distribution`
  - `volumetric_fog_albedo`
  - `volumetric_fog_emissive`
  - `volumetric_fog_extinction_scale`
  - `volumetric_fog_distance`
  - `volumetric_fog_start_distance`
  - `volumetric_fog_near_fade_in_distance`
  - `volumetric_fog_static_lighting_scattering_intensity`
  - `override_light_colors_with_fog_inscattering_colors`
- visibility flags:
  - `holdout`
  - `render_in_main_pass`
  - `visible_in_reflection_captures`
  - `visible_in_real_time_sky_captures`

Current file to expand:

- [Fog.h](</F:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Scene/Environment/Fog.h>)

#### 4.2.3 `scene::environment::SkyLight`

Current `SkyLight` must be extended for environment-family coupling.

Required authored fields:

- `enabled`
- `source`
- `cubemap_resource`
- `intensity_mul`
- `tint_rgb`
- `diffuse_intensity`
- `specular_intensity`
- `real_time_capture_enabled`
- `lower_hemisphere_color`
- `volumetric_scattering_intensity`
- `affect_reflections`
- `affect_global_illumination`

Current file to expand:

- [SkyLight.h](</F:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Scene/Environment/SkyLight.h>)

#### 4.2.4 `scene::environment::Sun`

`Sun` remains the scene-global authored primary sun abstraction, but it must no
longer imply that the whole atmosphere family only has one light slot.

`Sun` remains responsible for:

- authored azimuth/elevation
- authored color / temperature / illuminance
- disk size
- shadow authoring defaults
- synthetic vs scene-driven resolution

`Sun` is **not** sufficient for the second atmosphere-light slot. That must be
represented in directional-light authoring.

Current file:

- [Sun.h](</F:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Scene/Environment/Sun.h>)

#### 4.2.5 `scene::DirectionalLight`

Current `DirectionalLight` has:

- `environment_contribution`
- `is_sun_light`
- cascaded-shadow settings

For the full environment design, it must be extended to support explicit
atmosphere-light slot selection.

Required new authored fields:

- `atmosphere_light_slot`
  - `none`
  - `primary`
  - `secondary`
- `use_per_pixel_atmosphere_transmittance`
- `atmosphere_disk_luminance_scale` or equivalent renderer-facing authoring
  hook if needed by the final renderer contract

Rules:

- `Sun` resolves the primary authored sun workflow for DemoShell and scenes.
- `DirectionalLight.atmosphere_light_slot` resolves the full two-slot renderer
  contract.
- A scene may bind:
  - one primary atmosphere light
  - one optional secondary atmosphere light
- `Sun` does **not** auto-create a second atmosphere light.
- Canonical slot resolution:
  1. gather enabled directional lights with `environment_contribution=true`
  2. apply explicit `atmosphere_light_slot` bindings first
  3. if no explicit primary exists:
     - use the first `DirectionalLight` with `is_sun_light=true`
     - otherwise use the first environment-contributing directional light
  4. if multiple lights explicitly claim the same slot:
     - log an error
     - keep the first light in deterministic scene traversal order
     - ignore the later conflicting claimants
  5. secondary is optional; if unresolved, it remains disabled / black

Deterministic traversal order for slot resolution must use the existing scene
traversal surface, not ad-hoc container iteration.

Current file to extend:

- [DirectionalLight.h](</F:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Scene/Light/DirectionalLight.h>)

#### 4.2.6 New `scene::LocalFogVolume`

Local fog volumes must be a new **node-attached** component, not a
`SceneEnvironment` system.

Required placement:

- `src/Oxygen/Scene/Environment/LocalFogVolume.h`
- `src/Oxygen/Scene/Environment/LocalFogVolume.cpp`

Required traits:

- derives from `Component`
- requires `detail::TransformComponent`

Required authored fields:

- `enabled`
- `radial_fog_extinction`
- `height_fog_extinction`
- `height_fog_falloff`
- `height_fog_offset`
- `fog_phase_g`
- `fog_albedo`
- `fog_emissive`
- `sort_priority`

This component exists on scene nodes because:

- there can be many of them
- they require transforms
- they must be independently sorted and culled

### 4.3 `SceneEnvironment` Rules

`SceneEnvironment` remains the owner of:

- `Sun`
- `SkyAtmosphere`
- `Fog`
- `SkyLight`
- `SkySphere`
- `VolumetricClouds`
- `PostProcessVolume`

It does **not** own `LocalFogVolume` instances.

This means the junior implementation must preserve:

- scene-global environment systems in `SceneEnvironment`
- local media instances on regular scene nodes

### 4.3.1 `PostProcessVolume` Scope Note

`PostProcessVolume` remains a `SceneEnvironment` system, but it is **out of
scope** for this design except where post-process consumption depends on the
new environment-family products.

This document does not require widening `PostProcessVolume` itself.

## 5. Serialized Data in `src/Oxygen/Data`

### 5.1 Current Limitation

Current `SceneAsset` environment records are too small for the target design:

- `SkyAtmosphereEnvironmentRecord` lacks many required atmosphere parameters
- `FogEnvironmentRecord` lacks second layer, inscattering cubemap, directional
  inscattering, atmosphere coupling, and volumetric fog settings
- `SkyLightEnvironmentRecord` lacks real-time capture and volumetric-scattering
  fields
- there is no local fog volume record at all

Relevant files:

- [PakFormat_world.h](</F:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Data/PakFormat_world.h>)
- [SceneAsset.h](</F:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Data/SceneAsset.h>)

### 5.1.1 Screen HZB Mandate For Local Fog

The first-wave local-fog renderer path is not allowed to reinterpret missing
screen-HZB plumbing as optional. If Vortex or the Graphics backend does not yet
publish Screen HZB in the UE5.7 shape needed by local fog, the implementation
must add that backend/publication path cleanly before local fog can be
considered complete.

Rules:

- `depth/HZB if available` is not an implementation escape hatch for local fog.
- Stage 5 Screen HZB publication is first-wave work for the environment lane
  whenever local fog depends on it.
- The implementation must mirror UE5.7 screen-HZB semantics rather than
  improvising a reduced substitute or reusing unrelated HZB products.
- The required Screen HZB shape is the UE5.7 default path: mip 0 is
  half-resolution, rounded up to power-of-two extents, and the published mip
  count is derived from that reduced extent rather than from full-resolution
  SceneDepth.

### 5.2 Required Serialization Changes

#### 5.2.1 Environment Block Versioning

The current loader in
[SceneAsset.cpp](</F:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Data/SceneAsset.cpp>)
validates environment record sizes with **exact** `record_size == sizeof(T)`
checks.

Because this design requires major record widening, the design chooses a
**breaking scene-asset version bump** rather than pretending forward-compatible
widening already exists.

The junior implementation must:

- bump `kSceneAssetVersion` from `2` to `3`
- widen the environment record structs to their new v3 shapes
- update the environment-block loader to accept only v3 record sizes for the
  widened records
- require a batch re-cook of existing scene assets

This design does **not** rely on `>= sizeof(T)` validation or per-record
version tags. If a future compatibility layer is wanted, it must be designed
explicitly as a follow-up, not improvised during implementation.

#### 5.2.2 Expanded Environment Records

Expand:

- `SkyAtmosphereEnvironmentRecord`
- `FogEnvironmentRecord`
- `SkyLightEnvironmentRecord`

Add fields matching the authored Scene types in Section 4.

`SkyAtmosphereEnvironmentRecord` must explicitly add:

- `mie_absorption_rgb[3]`
- `sky_luminance_factor_rgb[3]`
- `sky_and_aerial_perspective_luminance_factor_rgb[3]`
- `aerial_scattering_strength`
- `aerial_perspective_start_depth_m`
- `height_fog_contribution`
- `trace_sample_count_scale`
- `transmittance_min_light_elevation_deg`
- holdout / main-pass visibility flags

`FogEnvironmentRecord` must explicitly add:

- second fog layer fields
- fog inscattering luminance
- sky-atmosphere ambient contribution color scale
- inscattering cubemap asset key
- cubemap angle / tint / directional-distance fields
- directional inscattering luminance / exponent / start distance
- `end_distance`
- `fog_cutoff_distance`
- full volumetric-fog fields
- holdout / main-pass / reflection-capture / real-time-sky-capture flags

`SkyLightEnvironmentRecord` must explicitly add:

- `real_time_capture_enabled`
- `lower_hemisphere_color`
- `volumetric_scattering_intensity`
- `affect_reflections`
- `affect_global_illumination`

#### 5.2.3 Local Fog Volume Record

Add a new component record for node-attached local fog volumes.

This should be a **component table record**, not an environment-block record,
because local fog volumes are:

- attached to nodes
- spatially local
- multiple per scene

Required new data type:

- `pak::world::LocalFogVolumeRecord`
- new `ComponentType::kLocalFogVolume`
  - FourCC: `0x474F464C` // `'LFOG'`

Required loader/parser changes:

- `src/Oxygen/Core/Meta/Data/ComponentType.inc`
- `src/Oxygen/Data/ComponentType.h`
- `PakFormat_world.h`
- `PakFormatSerioLoaders.h`
- `SceneAsset.h`
- `SceneAsset.cpp`
- `ComponentTraits<>`
- any scene-loader mapping code that instantiates scene-node components

Placement rule:

- `LocalFogVolumeRecord` lives in the **normal component table directory**
  alongside `Renderable`, camera, and light records
- it does **not** live in the trailing environment-system block

#### 5.2.4 Migration Strategy

This design requires an explicit migration path.

Chosen strategy:

1. introduce scene asset version `3`
2. re-cook target scenes to v3
3. runtime loader accepts v3 for the widened environment shapes
4. v2 assets remain readable only by the pre-widening shape; they are not
   considered valid for the widened environment design

Implications:

- batch re-cook is required
- no hidden runtime widening/compatibility shim is assumed
- tooling may offer offline migration helpers later, but implementation
  must not depend on them

## 6. DemoShell Authoring and Runtime Surface

This section is mandatory. The design is incomplete unless the
authoring/debug runtime surface is specified.

### 6.1 Owner Files

The DemoShell environment authoring stack is:

- [EnvironmentSettingsService.h](</F:/projects/DroidNet/projects/Oxygen.Engine/Examples/DemoShell/Services/EnvironmentSettingsService.h>)
- [EnvironmentSettingsService.cpp](</F:/projects/DroidNet/projects/Oxygen.Engine/Examples/DemoShell/Services/EnvironmentSettingsService.cpp>)
- [EnvironmentVm.h](</F:/projects/DroidNet/projects/Oxygen.Engine/Examples/DemoShell/UI/EnvironmentVm.h>)
- [EnvironmentVm.cpp](</F:/projects/DroidNet/projects/Oxygen.Engine/Examples/DemoShell/UI/EnvironmentVm.cpp>)
- [EnvironmentDebugPanel.h](</F:/projects/DroidNet/projects/Oxygen.Engine/Examples/DemoShell/UI/EnvironmentDebugPanel.h>)
- [EnvironmentDebugPanel.cpp](</F:/projects/DroidNet/projects/Oxygen.Engine/Examples/DemoShell/UI/EnvironmentDebugPanel.cpp>)

### 6.2 `EnvironmentSettingsService` Responsibilities

`EnvironmentSettingsService` is the runtime orchestration and persistence owner
for the environment panel. It already:

- hydrates scene-authored environment state from `SceneAsset`
- stores UI-side cached environment state
- manages dirty domains
- applies pending changes to the active scene
- owns preset application
- handles synthetic sun fallback
- tracks atmosphere LUT status from the renderer

Hydration ownership note:

- `SceneAsset` is a read-only data view only
- `EnvironmentSettingsService::HydrateEnvironment(...)` is a DemoShell/runtime
  consumer, not a Data-layer API
- scene-global environment records hydrate through:
  - `EnvironmentSettingsService::HydrateEnvironment(...)`
  - the scene-loader environment build step
- node-attached local fog volume records hydrate through the scene-node build
  path, not through `SceneEnvironment`

For the full environment design it must be expanded to manage:

- full atmosphere authored state
- full fog/volumetric-fog authored state
- full sky-light authored state
- two atmosphere-light slots
- local fog volume editing state
- renderer debug state for:
  - atmosphere LUT validity
  - aerial-perspective volume state
  - sky-light capture state
  - local-fog-volume GPU state
  - volumetric-fog history/state

### 6.3 Dirty-Domain Expansion

Current dirty domains are too coarse.

Replace or expand with explicit domains:

- `kAtmosphereModel`
- `kAtmosphereLights`
- `kHeightFog`
- `kLocalFogVolumes`
- `kVolumetricFog`
- `kSkyLight`
- `kSkybox`
- `kSkySphere`
- `kSun`
- `kRendererFlags`
- `kPreset`

Rules:

- sun edits that affect atmosphere-light slot 0 must dirty both `kSun` and
  `kAtmosphereLights`
- atmosphere-light slot edits must dirty atmosphere products that depend on
  them
- fog edits that affect volumetric-fog integration must dirty both
  `kHeightFog` and `kVolumetricFog` where applicable
- local-fog-volume list edits must dirty:
  - `kLocalFogVolumes`
  - `kVolumetricFog` when injection is enabled
  - `kPreset` if a preset owns the current local fog volume set

### 6.4 `EnvironmentVm` Contract

`EnvironmentVm` must expose explicit getters/setters for all authored
fields. It must stop treating key renderer-owned state as opaque where the user
must control authoring.

Required new VM surfaces:

- atmosphere:
  - transform mode
  - sky luminance factor
  - sky+aerial luminance factor
  - aerial start depth
  - height-fog contribution
  - trace sample count scale
  - transmittance minimum light elevation
  - holdout / main-pass visibility
- atmosphere lights:
  - slot 0/1 enable
  - scene light binding or manual override
  - per-slot direction override
  - per-slot transmittance mode
- fog:
  - second fog layer
  - cubemap inscattering controls
  - directional inscattering controls
  - end distance / cutoff distance
  - full volumetric-fog settings
  - visibility flags
- sky light:
  - real-time capture enable
  - lower hemisphere color
  - volumetric scattering intensity
- local fog volumes:
  - list/add/remove/select
  - selected-volume parameter editing

### 6.5 `EnvironmentDebugPanel` Requirements

The panel must be expanded from a partial environment debug UI into a complete
authoring and diagnostics surface.

Required top-level sections:

1. `Renderer State`
   - LUT generation state
   - aerial-perspective state
   - distant-sky-light / sky-capture state
   - local-fog-volume count / tile-culling status
   - volumetric-fog state / history validity
2. `Atmosphere Lights`
   - slot 0
   - slot 1
   - resolved scene bindings / overrides
3. `Sun`
   - remains the authoring surface for the primary artistic sun workflow
4. `Sky Atmosphere`
   - all atmosphere model parameters
5. `Height Fog`
   - both fog layers
   - cubemap and directional inscattering
   - atmosphere/fog coupling controls
6. `Local Fog Volumes`
   - volume list
   - selected volume editor
   - create/delete controls
   - visualization toggles
7. `Sky Light`
   - source/capture/volumetric scattering controls
8. `Sky Sphere`
   - retained fallback path

### 6.6 Preset Responsibilities

Current presets only cover a reduced atmosphere/fog set. The design now
requires:

- presets to explicitly set both atmosphere-light slots or disable slot 1
- presets to declare whether local fog volumes exist
- presets to define volumetric-fog defaults
- presets to define sky-light capture behavior

If presets do not support some fields initially, the implementation must define
default safe values in the preset loader rather than leaving them
undefined.

Required expanded preset schema:

```cpp
struct EnvironmentPresetDataV2 {
  std::string_view name;

  // Mandatory
  SunPreset primary_sun;
  AtmospherePreset atmosphere;
  HeightFogPreset height_fog;
  SkyLightPreset sky_light;

  // Mandatory renderer/family defaults
  VolumetricFogPreset volumetric_fog;
  AtmosphereLightSlotPreset primary_atmosphere_light;
  AtmosphereLightSlotPreset secondary_atmosphere_light;

  // Optional
  std::vector<LocalFogVolumePreset> local_fog_volumes;
  SkySpherePreset sky_sphere;
  SkyboxPreset skybox;
};
```

Preset container note:

- `std::vector<LocalFogVolumePreset>` is intentional here to keep the design
  simple for variable-sized authored local-fog-volume sets.
- If the implementation wants compile-time constant presets, it may replace
  that field with `std::span<const LocalFogVolumePreset>` backed by static
  arrays without changing the authoring semantics of this design.

Preset rules:

- mandatory groups must always be authored in each preset
- optional groups must either:
  - be fully specified, or
  - explicitly opt into `inherit_current_scene_value`
- presets must never leave slot 1 / volumetric fog / local fog volumes in an
  ambiguous partially-authored state

### 6.7 Persistence Schema

`EnvironmentSettingsService` currently uses a settings schema version key.

For the full environment design, bump the schema version and add persisted keys for:

- all widened atmosphere fields
- all widened fog fields
- all widened sky-light fields
- atmosphere-light slot configuration
- local fog volume authored list
- renderer-debug convenience toggles where user-controlled

Do **not** silently drop widened fields on save/load.

## 7. Renderer Translation in `src/Oxygen/Vortex`

### 7.1 Input Ownership

`EnvironmentLightingService` consumes:

- scene-global authored environment systems from `SceneEnvironment`
- node-attached `LocalFogVolume` components from the active scene graph
- selected atmosphere-light directional lights from the scene light set
- sky-light authored state
- scene textures and view state

Local-fog discovery mechanism:

- use the existing `Scene::Traverse()` / `SceneTraversal` surface to gather
  `LocalFogVolume` components from the active scene graph
- perform this gather in the per-frame or change-driven environment gather pass
  using deterministic traversal order
- do **not** invent a separate component registry for the first
  implementation unless a later optimization plan explicitly adds one

### 7.2 Translation Stages

#### 7.2.1 Scene -> Stable Environment State

Per scene / change-driven:

1. read `SkyAtmosphere`
2. build canonical atmosphere coefficients and transform state
3. read `Fog`
4. read `SkyLight`
5. resolve atmosphere-light slot bindings from directional lights using the
   rules in Section 4.2.5
6. detect local-fog-volume instances and build a stable authored snapshot

#### 7.2.2 Stable State -> Cached Products

On invalidation:

1. regenerate transmittance LUT
2. regenerate multi-scattering LUT
3. regenerate distant sky-light LUT
4. refresh sky-light processed capture products

#### 7.2.3 View -> Per-View Products

Per view:

1. build sky-view LUT
2. build camera aerial-perspective volume
3. sort and upload local fog volumes
4. prepare volumetric-fog resources and history state

#### 7.2.4 Stage 14/15 Pass Order and Resource Flow

The implementation must follow this order unless a later approved design
revision replaces it.

| Order | Stage | Pass / Step | Inputs | Outputs | Required state transition |
| --- | --- | --- | --- | --- | --- |
| 1 | pre-stage | atmosphere-state translation | `SkyAtmosphere`, `Fog`, `SkyLight`, atmosphere lights | stable family state | CPU only |
| 2 | pre-stage | atmosphere LUT invalidation/update | stable atmosphere state | transmittance + multi-scatter + distant skylight LUTs | UAV write -> SRV |
| 3 | pre-stage | per-view sky-view LUT build | stable atmosphere state, per-view camera | `SkyViewLut` | UAV write -> SRV |
| 4 | pre-stage | per-view camera aerial perspective build | LUTs, atmosphere lights, per-view camera | `CameraAerialPerspectiveVolume` | UAV write -> SRV |
| 5 | Stage 14 | local fog volume gather/sort/upload | scene traversal results | GPU instance + tile inputs | upload -> SRV/UAV |
| 6 | Stage 14 | local fog tiled culling | local fog GPU instance data, SceneDepth, Stage 5 Screen HZB products | tile buffers/textures + draw-indirect buffers | UAV write -> SRV/UAV |
| 7 | Stage 14 | volumetric fog grid allocate/clear | fog model, view data | empty froxel resources | UAV write |
| 8 | Stage 14 | volumetric light injection | froxel grid, atmosphere lights, local lights, shadows | scattering/extinction intermediates | UAV write |
| 9 | Stage 14 | optional local-fog injection into volumetric fog | local fog data, froxel grid | updated volumetric media | UAV write |
| 10 | Stage 14 | volumetric final integration | intermediates | `IntegratedLightScattering` | UAV write -> SRV |
| 11 | Stage 15 | sky-atmosphere render | SceneColor, SceneDepth, LUTs, atmosphere lights | updated SceneColor | RTV + depth-read |
| 12 | Stage 15 | height fog composite | SceneColor, SceneDepth, fog model, LUT/AP products, optional volumetric SRV | updated SceneColor | RTV + depth-read + SRV |
| 13 | Stage 15 | local fog volume composite when not injection-only | SceneColor, SceneDepth, tile buffers/textures, local fog data | updated SceneColor | RTV + depth-read + SRV |
| 14 | Stage 15 | final environment-family closeout | stage outputs | stable `SceneColor` for later stages | RTV -> later stage SRV/RTV as needed |

Junior implementation rule:

- every Stage 14 product consumed by Stage 15 must have an explicit
  write-to-read transition
- do not rely on implicit resource-state promotion between the volumetric and
  composition passes
- do not treat Screen HZB as optional for local fog. If the Vortex/Graphics
  path is missing it, implement it before claiming Stage 14 local-fog culling
  is aligned with UE5.7.

### 7.3 Required Vortex Runtime Types

Add or expand:

- `AtmosphereModel`
- `AtmosphereLightModel[2]`
- `HeightFogModel`
- `SkyLightEnvironmentModel`
- `LocalFogVolumeInstance`
- `VolumetricFogModel`
- `EnvironmentFrameBindings`
- `EnvironmentViewProducts`

### 7.4 Required Vortex Service Internal Subsystems

Split renderer internals into clear owners:

- `AtmosphereState`
- `AtmosphereLightState`
- `AtmosphereLutCache`
- `SkyLightCaptureState`
- `HeightFogState`
- `LocalFogVolumeState`
- `VolumetricFogState`

This prevents the implementation from collapsing everything into one giant
service file.

### 7.5 Shader Design Responsibilities

This document includes shader responsibilities as part of the design; no
separate shader-spec document is required for the first implementation pass.

Required shader file families:

- `AtmosphereTransmittanceLutCS`
  - input: stable atmosphere coefficients
  - output: transmittance LUT
- `AtmosphereMultiScatteringLutCS`
  - input: atmosphere coefficients + transmittance LUT
  - output: multi-scattering LUT
- `AtmosphereSkyViewLutCS`
  - input: atmosphere coefficients + atmosphere lights + LUTs
  - output: sky-view LUT
- `AtmosphereCameraAerialPerspectiveCS`
  - input: atmosphere coefficients + atmosphere lights + LUTs + per-view camera
  - output: camera aerial-perspective volume
- `DistantSkyLightLutCS`
  - input: atmosphere coefficients + atmosphere lights + LUTs
  - output: distant sky-light LUT
- `SkyAtmospherePS`
  - input: sky-view LUT / atmosphere lights / holdout flags
  - output: sky-atmosphere scene contribution
- `HeightFogCommon`
  - input: dual fog layers, cubemap inscattering, atmosphere contribution,
    optional volumetric fog SRV
  - output: fog color + transmittance
- `LocalFogVolumeCommon`
  - input: local fog instance data, directional light, sky light, `PhaseG`
  - output: local medium color + coverage
- `LocalFogVolumeTiledCullingCS`
  - input: local fog instances + per-tile frustum planes + Screen HZB bindings
  - classification: cull against tile frustum planes before the HZB visibility
    test; do not fall back to screen-rect overlap as the primary tile test
  - output: tile lists / draw-indirect buffers
- `LocalFogVolumeComposePS`
  - input: SceneColor, SceneDepth, local fog tile/instance data
  - output: local fog composite into SceneColor
- `VolumetricFogInjectCS`
  - input: froxel grid + lights + shadows
  - output: scattering/extinction intermediates
- `VolumetricFogFinalIntegrationCS`
  - input: volumetric intermediates
  - output: `IntegratedLightScattering`

Shader design rule:

- the junior implementation must mirror the ownership split in C++:
  atmosphere LUT generation, height fog, local fog, and volumetric fog are
  separate shader families, not one catch-all environment shader blob
- local fog tiled culling must consume the full UE5.7-shaped Screen HZB
  contract, including active view-rect mapping; it must not assume the HZB
  covers the full scene-texture extent for sub-viewport views

## 8. Execution Order and File Ownership

This is the implementation order a junior engineer should follow.

### 8.1 Authoring Layer First

1. widen Scene auth types:
   - `SkyAtmosphere`
   - `Fog`
   - `SkyLight`
   - `DirectionalLight`
2. add new node component:
   - `LocalFogVolume`
3. update Scene docs:
   - `src/Oxygen/Scene/Environment/README.md`

### 8.2 Serialization Layer Second

1. widen `PakFormat_world.h` environment records
2. add `LocalFogVolumeRecord`
3. update loaders and `SceneAsset`
4. update scene-loader hydration

### 8.3 DemoShell Layer Third

1. widen `EnvironmentSettingsService`
2. widen `EnvironmentVm`
3. widen `EnvironmentDebugPanel`
4. bump persistence schema
5. update presets

### 8.4 Renderer Layer Fourth

1. implement stable environment-state translation
2. implement atmosphere-light slot translation
3. implement LUT pipeline
4. implement sky-atmosphere rendering
5. implement camera aerial perspective
6. implement Stage 5 Screen HZB publication for environment consumers
   - this includes the active view-rect mapping surface and sub-viewport-safe
     Stage 14 local-fog consumption, not only raw HZB extents
7. implement full height fog
8. implement local fog volumes
9. implement volumetric fog
10. implement sky-light coupling
11. implement full publication bindings

Implementation note:

- Local fog must not resume on a placeholder Stage 5. If Screen HZB is missing
  in Vortex, this renderer-layer sequence must fill that gap before local-fog
  culling/proof is treated as production-credible.

### 8.5 Proof and Validation Last

1. source-level tests
2. scene-hydration tests
3. DemoShell environment service/panel tests
4. VortexBasic runtime validation tests
5. Async runtime validation tests

## 9. Validation Requirements

### 9.1 Scene Layer

Add tests for:

- widened `SkyAtmosphere`
- widened `Fog`
- widened `SkyLight`
- `DirectionalLight` atmosphere-light slot behavior
- new `LocalFogVolume`

### 9.2 Data Layer

Add tests for:

- widened environment record round-tripping
- `LocalFogVolumeRecord` parsing
- scene asset compatibility / version migration behavior

### 9.3 DemoShell Layer

Add tests for:

- `EnvironmentSettingsService` hydration and apply
- dirty-domain behavior
- atmosphere-light slot resolution
- local fog volume list editing
- preset correctness
- persistence schema migration

### 9.4 Renderer Layer

Add tests and runtime proof for:

- atmosphere LUT invalidation/versioning
- dual atmosphere-light data flow
- camera aerial perspective
- height-fog + sky-atmosphere coupling
- local fog volume sorting/HZB culling/shading
- volumetric fog integration
- sky-light coupling

### 9.5 Validation Proof

Validation must compare against reference
artifacts, not only Vortex self-baselines.

Required proof surfaces:

- VortexBasic controlled environment scene
- Async migrated runtime scene
- Reference captures / measurements for:
  - sky atmosphere
  - height fog
  - local fog volumes
  - volumetric fog
  - sky-light contribution

## 10. Deferred Items

Still deferred:

- volumetric clouds
- heterogeneous volumes
- Stage-13 canonical indirect-light ownership and ambient-bridge retirement

Not deferred:

- atmosphere model coverage
- two atmosphere-light slots
- full fog coverage
- local fog volumes
- volumetric fog
- sky-light coupling
- DemoShell authoring and diagnostics support

## 11. Junior Implementation Checklist

- [ ] Expand `scene::environment::SkyAtmosphere`
- [ ] Expand `scene::environment::Fog`
- [ ] Expand `scene::environment::SkyLight`
- [ ] Expand `scene::DirectionalLight` with atmosphere-light slot support
- [ ] Add `scene::LocalFogVolume`
- [ ] Update `SceneEnvironment` docs
- [ ] Widen `SceneAsset` environment records
- [ ] Add `LocalFogVolumeRecord`
- [ ] Update scene-loader hydration
- [ ] Widen `EnvironmentSettingsService`
- [ ] Widen `EnvironmentVm`
- [ ] Widen `EnvironmentDebugPanel`
- [ ] Bump environment settings persistence schema
- [ ] Implement atmosphere/LUT/service runtime translation
- [ ] Implement camera aerial perspective
- [ ] Implement full height fog
- [ ] Implement local fog volumes
- [ ] Implement volumetric fog
- [ ] Implement sky-light coupling
- [ ] Add source-level, hydration, DemoShell, and runtime validation tests
