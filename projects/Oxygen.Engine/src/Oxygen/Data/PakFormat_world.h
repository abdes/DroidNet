//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include <Oxygen/Base/Compilers.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Data/PakFormat_core.h>

// packed structs intentionally embed unaligned NamedType ResourceIndexT fields
OXYGEN_DIAGNOSTIC_PUSH
OXYGEN_DIAGNOSTIC_DISABLE_MSVC(4315)
// NOLINTBEGIN(*-avoid-c-arrays,*-magic-numbers)

//! Oxygen PAK format world domain schema.
/*!
 Owns scene graph descriptors and world component/environment/light records.
*/
namespace oxygen::data::pak::world {

//! Scene node flags for `NodeRecord::node_flags`.
[[maybe_unused]] constexpr uint32_t kSceneNodeFlag_Visible = (1U << 0);
[[maybe_unused]] constexpr uint32_t kSceneNodeFlag_Static = (1U << 1);
[[maybe_unused]] constexpr uint32_t kSceneNodeFlag_CastsShadows = (1U << 2);
[[maybe_unused]] constexpr uint32_t kSceneNodeFlag_ReceivesShadows = (1U << 3);
[[maybe_unused]] constexpr uint32_t kSceneNodeFlag_RayCastingSelectable
  = (1U << 4);
[[maybe_unused]] constexpr uint32_t kSceneNodeFlag_IgnoreParentTransform
  = (1U << 5);

//! Scene asset descriptor version for current PAK schema.
//!
//! @note Scene descriptors include a trailing SceneEnvironment block (empty
//! allowed).
[[maybe_unused]] constexpr uint8_t kSceneAssetVersion = 3;

//! Index type for scene node tables.
using SceneNodeIndexT = uint32_t;

//! Index type for scene component tables.
using SceneComponentIndexT = uint32_t;

//! Scene data table descriptor.
/*!
 Describes a packed array of records inside a scene descriptor.

 Offsets are bytes relative to the start of the scene descriptor payload.
*/
#pragma pack(push, 1)
struct SceneDataTable {
  core::OffsetT offset = 0;
  uint32_t count = 0;
  uint32_t entry_size = 0;
};
#pragma pack(pop)
static_assert(sizeof(SceneDataTable) == 16);

//! Scene node table descriptor (alias).
using SceneNodeTable = SceneDataTable;

//! Scene component table descriptor (alias).
using SceneComponentTable = SceneDataTable;

//! Scene string table descriptor.
/*!
 Describes the packed scene string table blob.
 Offsets are relative to the start of the scene descriptor payload.
*/
#pragma pack(push, 1)
struct SceneStringTable {
  core::StringTableOffsetT offset = 0;
  core::StringTableSizeT size = 0;
};
#pragma pack(pop)
static_assert(sizeof(SceneStringTable) == 8);

//! Scene asset descriptor
/*!
 Describes a scene (level) asset. As with all asset descriptors in this file,
 `AssetHeader` is the first field.

 The descriptor payload is a packed byte blob (no implicit padding) and is
 followed by:

 - `NodeRecord nodes[nodes.count];` at `nodes.offset`
 - a packed, NUL-terminated UTF-8 scene string table blob described by
   `scene_strings`
 - optional component tables (e.g. `RenderableRecord[]`) described by the
   component table directory at `component_table_directory_offset`

 `nodes.entry_size` MUST match the corresponding struct size for the scene
 format version. Component tables declare their own `entry_size` via
 `SceneComponentTableDesc::table.entry_size`.

 Strings are stored back-to-back and sized to their actual length.
 `NodeRecord::scene_name_offset` is a byte offset into the scene string table.
 The scene string table MUST start with a single `\0` byte so that offset `0`
 refers to the empty string.

 @note Scene graph indices have no sentinel values by contract. Indices are
   always valid for their type; out-of-range indices are treated as errors by
   loaders/tooling.
*/
#pragma pack(push, 1)
struct SceneAssetDesc {
  core::AssetHeader header;

  SceneNodeTable nodes = {};
  SceneStringTable scene_strings = {};

  // Directory of component tables.
  // Points to an array of `SceneComponentTableDesc` entries.
  core::OffsetT component_table_directory_offset = 0;
  uint32_t component_table_count = 0;
};
#pragma pack(pop)
static_assert(sizeof(SceneAssetDesc) == 139);

//! Scene component table directory entry.
/*!
 Describes an optional component table attached to scene nodes.

 All offsets are bytes relative to the start of the descriptor payload.

 @note This is a forward-compatible extension point. Loaders may ignore unknown
   component types. Known component tables are typically sorted by `node_index`
   for efficient loading.
*/
#pragma pack(push, 1)
struct SceneComponentTableDesc {
  uint32_t component_type = 0; // Format-defined component kind
  SceneComponentTable table = {};
};
#pragma pack(pop)
static_assert(sizeof(SceneComponentTableDesc) == 20);

#pragma pack(push, 1)

//! Node record used by the cooked scene descriptor.
/*!
  Describes a single node in the scene hierarchy. Nodes are stored in a flat
  array in the `SceneAssetDesc`.

  ### Hierarchy
  - The node at index 0 is always the root node.
  - `parent_index` refers to the index of the parent node in the same table.
  - If `parent_index` equals the node's own index, the node has no parent (is a
    root).

  ### Transform
  - Transforms are local to the parent.
  - Rotation is stored as a quaternion (x, y, z, w).

  @see SceneAssetDesc
*/
struct NodeRecord {
  AssetKey node_id; // Stable GUID for the node
  core::StringTableOffsetT scene_name_offset
    = 0; // Offset into scene string table

  SceneNodeIndexT parent_index = 0; // Index of parent node (or self if root)

  //! Bitfield of `kSceneNodeFlag_*` constants
  uint32_t node_flags = 0;

  // Local Transform (TRS)
  float translation[3] = { 0.0F, 0.0F, 0.0F };
  float rotation[4] = { 0.0F, 0.0F, 0.0F, 1.0F }; // Quaternion (XYZW)
  float scale[3] = { 1.0F, 1.0F, 1.0F };
};
#pragma pack(pop)
static_assert(sizeof(NodeRecord) == 68);

#pragma pack(push, 1)

//! Renderable component record.
/*!
  Attaches a geometry asset to a scene node.

  ### Relationships
  - Links to a `NodeRecord` via `node_index`.
  - References a `GeometryAsset` via `geometry_key`.
  - Optionally references a scene-authored material override via
    `material_key`.

  @note Component tables are typically sorted by `node_index` for efficient
  loading.
*/
struct RenderableRecord {
  SceneNodeIndexT node_index = 0; // Index of the owner node
  AssetKey geometry_key; // Geometry asset to render
  AssetKey material_key; // Optional material override; nil means no override
  uint32_t visible = 1; // Visibility flag (boolean)
};
#pragma pack(pop)
static_assert(sizeof(RenderableRecord) == 40);

#pragma pack(push, 1)

//! Node-attached local fog volume component record.
struct LocalFogVolumeRecord {
  SceneNodeIndexT node_index = 0;
  uint32_t enabled = 1;

  float radial_fog_extinction = 1.0F;
  float height_fog_extinction = 1.0F;
  float height_fog_falloff = 1000.0F;
  float height_fog_offset = 0.0F;
  float fog_phase_g = 0.2F;
  float fog_albedo[3] = { 1.0F, 1.0F, 1.0F };
  float fog_emissive[3] = { 0.0F, 0.0F, 0.0F };
  int32_t sort_priority = 0;
};
#pragma pack(pop)
static_assert(sizeof(LocalFogVolumeRecord) == 56);

#pragma pack(push, 1)

//! Perspective camera component record.
/*!
  Attaches a perspective camera to a scene node.

  ### Coordinate System
  - The camera looks down the -Z axis in its local space.
  - FOV is vertical, in radians.
*/
struct PerspectiveCameraRecord {
  SceneNodeIndexT node_index = 0; // Index of the owner node
  float fov_y = 0.785398F; // Vertical FOV in radians (~45 deg)
  float aspect_ratio = 1.777778F; // Width / Height (default 16:9)
  float near_plane = 0.1F; // Distance to near clipping plane
  float far_plane = 1000.0F; // Distance to far clipping plane
};
#pragma pack(pop)
static_assert(sizeof(PerspectiveCameraRecord) == 20);

#pragma pack(push, 1)

//! Orthographic camera component record.
/*!
  Attaches an orthographic camera to a scene node.

  ### Volume
  - Defined by a box (left, right, bottom, top, near, far) in local space.
*/
struct OrthographicCameraRecord {
  SceneNodeIndexT node_index = 0; // Index of the owner node
  float left = -10.0F;
  float right = 10.0F;
  float bottom = -10.0F;
  float top = 10.0F;
  float near_plane = -100.0F;
  float far_plane = 100.0F;
};
#pragma pack(pop)
static_assert(sizeof(OrthographicCameraRecord) == 28);

//=== Scene: Lights and Environment -----------------------------------------//

//! Environment system type tags used by scene persistence.
enum class EnvironmentComponentType : uint32_t { // NOLINT(*-enum-size)
  kSkyAtmosphere = 0,
  kVolumetricClouds = 1,
  kFog = 2,
  kSkyLight = 3,
  kSkySphere = 4,
  kPostProcessVolume = 5,
};

//! Header for the trailing SceneEnvironment block.
/*!
 The environment block is stored immediately after the Scene payload.

 - `byte_size` includes this header and all following system records.
 - The block is always present for scene descriptors in this layer.
   A scene with "no environment" uses `systems_count == 0` and
   `byte_size == sizeof(SceneEnvironmentBlockHeader)`.
*/
#pragma pack(push, 1)
struct SceneEnvironmentBlockHeader {
  uint32_t byte_size = 0;
  uint32_t systems_count = 0;
};
#pragma pack(pop)
static_assert(sizeof(SceneEnvironmentBlockHeader) == 8);

//! Header for a single environment-system record in the trailing block.
/*!
 - `system_type` is an `EnvironmentComponentType` enum value.
 - `record_size` is the byte size of the entire record, including this header.
   Unknown `system_type` values can be skipped using `record_size`.
*/
#pragma pack(push, 1)
struct SceneEnvironmentSystemRecordHeader {
  uint32_t system_type = 0;
  uint32_t record_size = 0;
};
#pragma pack(pop)
static_assert(sizeof(SceneEnvironmentSystemRecordHeader) == 8);

//! Packed SkyAtmosphere environment record.
#pragma pack(push, 1)
struct SkyAtmosphereEnvironmentRecord {
  SceneEnvironmentSystemRecordHeader header = {
    .system_type = nostd::to_underlying(EnvironmentComponentType::kSkyAtmosphere),
    .record_size = sizeof(SkyAtmosphereEnvironmentRecord),
  };

  uint32_t enabled = 1; //!< Whether this system is enabled (boolean).

  float planet_radius_m = 6360000.0F;
  float atmosphere_height_m = 80000.0F;

  float ground_albedo_rgb[3] = { 0.4F, 0.4F, 0.4F };

  float rayleigh_scattering_rgb[3] = { 5.8e-6F, 13.5e-6F, 33.1e-6F };
  float rayleigh_scale_height_m = 8000.0F;

  float mie_scattering_rgb[3] = { 21.0e-6F, 21.0e-6F, 21.0e-6F };
  float mie_absorption_rgb[3] = { 0.0F, 0.0F, 0.0F };
  float mie_scale_height_m = 1200.0F;
  float mie_g = 0.8F;

  float absorption_rgb[3] = { 0.0F, 0.0F, 0.0F };
  float ozone_density_profile[3] = { 25000.0F, 0.0F, 0.0F };

  float multi_scattering_factor = 1.0F;
  float sky_luminance_factor_rgb[3] = { 1.0F, 1.0F, 1.0F };
  float sky_and_aerial_perspective_luminance_factor_rgb[3]
    = { 1.0F, 1.0F, 1.0F };

  float aerial_perspective_distance_scale = 1.0F;
  float aerial_scattering_strength = 1.0F;
  float aerial_perspective_start_depth_m = 0.0F;
  float height_fog_contribution = 1.0F;
  float trace_sample_count_scale = 1.0F;
  float transmittance_min_light_elevation_deg = -90.0F;

  uint32_t sun_disk_enabled = 1;
  uint32_t holdout = 0;
  uint32_t render_in_main_pass = 1;
};
#pragma pack(pop)
static_assert(sizeof(SkyAtmosphereEnvironmentRecord) == 168);

//! Packed VolumetricClouds environment record.
#pragma pack(push, 1)
struct VolumetricCloudsEnvironmentRecord {
  SceneEnvironmentSystemRecordHeader header = {
    .system_type
    = nostd::to_underlying(EnvironmentComponentType::kVolumetricClouds),
    .record_size = sizeof(VolumetricCloudsEnvironmentRecord),
  };

  uint32_t enabled = 1; //!< Whether this system is enabled (boolean).

  float base_altitude_m = 1500.0F;
  float layer_thickness_m = 4000.0F;

  float coverage = 0.5F;
  float extinction_sigma_t_per_m = 1.0e-3F;

  float single_scattering_albedo_rgb[3] = { 0.9F, 0.9F, 0.9F };
  float phase_g = 0.6F;

  float wind_dir_ws[3] = { 1.0F, 0.0F, 0.0F };
  float wind_speed_mps = 10.0F;

  float shadow_strength = 0.8F;
};
#pragma pack(pop)
static_assert(sizeof(VolumetricCloudsEnvironmentRecord) == 64);

//! Packed Fog environment record.
#pragma pack(push, 1)
struct FogEnvironmentRecord {
  SceneEnvironmentSystemRecordHeader header = {
    .system_type = nostd::to_underlying(EnvironmentComponentType::kFog),
    .record_size = sizeof(FogEnvironmentRecord),
  };

  uint32_t enabled = 1; //!< Whether this system is enabled (boolean).

  uint32_t model = 0; //!< FogModel (0=ExponentialHeight, 1=Volumetric).
  float extinction_sigma_t_per_m = 0.002F;
  float height_falloff_per_m = 0.02F;
  float height_offset_m = 0.0F;
  float start_distance_m = 0.0F;
  float max_opacity = 1.0F;
  float single_scattering_albedo_rgb[3] = { 1.0F, 1.0F, 1.0F };
  float anisotropy_g = 0.0F;

  uint32_t enable_height_fog = 1;
  uint32_t enable_volumetric_fog = 0;

  float second_fog_density = 0.0F;
  float second_fog_height_falloff = 0.0F;
  float second_fog_height_offset = 0.0F;

  float fog_inscattering_luminance[3] = { 0.0F, 0.0F, 0.0F };
  float sky_atmosphere_ambient_contribution_color_scale[3]
    = { 1.0F, 1.0F, 1.0F };
  AssetKey inscattering_color_cubemap_asset;
  float inscattering_color_cubemap_angle = 0.0F;
  float inscattering_texture_tint[3] = { 1.0F, 1.0F, 1.0F };
  float fully_directional_inscattering_color_distance = 0.0F;
  float non_directional_inscattering_color_distance = 0.0F;
  float directional_inscattering_luminance[3] = { 0.0F, 0.0F, 0.0F };
  float directional_inscattering_exponent = 4.0F;
  float directional_inscattering_start_distance = 10000.0F;
  float end_distance_m = 0.0F;
  float fog_cutoff_distance_m = 0.0F;

  float volumetric_fog_scattering_distribution = 0.0F;
  float volumetric_fog_albedo[3] = { 1.0F, 1.0F, 1.0F };
  float volumetric_fog_emissive[3] = { 0.0F, 0.0F, 0.0F };
  float volumetric_fog_extinction_scale = 1.0F;
  float volumetric_fog_distance = 0.0F;
  float volumetric_fog_start_distance = 0.0F;
  float volumetric_fog_near_fade_in_distance = 0.0F;
  float volumetric_fog_static_lighting_scattering_intensity = 1.0F;
  uint32_t override_light_colors_with_fog_inscattering_colors = 0;

  uint32_t holdout = 0;
  uint32_t render_in_main_pass = 1;
  uint32_t visible_in_reflection_captures = 1;
  uint32_t visible_in_real_time_sky_captures = 1;
};
#pragma pack(pop)
static_assert(sizeof(FogEnvironmentRecord) == 232);

//! Packed SkyLight (IBL) environment record.
#pragma pack(push, 1)
struct SkyLightEnvironmentRecord {
  SceneEnvironmentSystemRecordHeader header = {
    .system_type = nostd::to_underlying(EnvironmentComponentType::kSkyLight),
    .record_size = sizeof(SkyLightEnvironmentRecord),
  };

  uint32_t enabled = 1; //!< Whether this system is enabled (boolean).

  uint32_t source = 0; // SkyLightSource

  AssetKey cubemap_asset;

  float intensity = 1.0F;
  float tint_rgb[3] = { 1.0F, 1.0F, 1.0F };

  float diffuse_intensity = 1.0F;
  float specular_intensity = 1.0F;
  uint32_t real_time_capture_enabled = 0;
  float lower_hemisphere_color[3] = { 0.0F, 0.0F, 0.0F };
  float volumetric_scattering_intensity = 1.0F;
  uint32_t affect_reflections = 1;
  uint32_t affect_global_illumination = 1;
};
#pragma pack(pop)
static_assert(sizeof(SkyLightEnvironmentRecord) == 84);

//! Packed SkySphere environment record.
#pragma pack(push, 1)
struct SkySphereEnvironmentRecord {
  SceneEnvironmentSystemRecordHeader header = {
    .system_type = nostd::to_underlying(EnvironmentComponentType::kSkySphere),
    .record_size = sizeof(SkySphereEnvironmentRecord),
  };

  uint32_t enabled = 1; //!< Whether this system is enabled (boolean).

  uint32_t source = 0; // SkySphereSource

  AssetKey cubemap_asset;

  float solid_color_rgb[3] = { 0.0F, 0.0F, 0.0F };
  float intensity = 1.0F;
  float rotation_radians = 0.0F;
  float tint_rgb[3] = { 1.0F, 1.0F, 1.0F };
};
#pragma pack(pop)
static_assert(sizeof(SkySphereEnvironmentRecord) == 64);

//! Packed PostProcessVolume environment record.
#pragma pack(push, 1)
struct PostProcessVolumeEnvironmentRecord {
  SceneEnvironmentSystemRecordHeader header = {
    .system_type
    = nostd::to_underlying(EnvironmentComponentType::kPostProcessVolume),
    .record_size = sizeof(PostProcessVolumeEnvironmentRecord),
  };

  uint32_t enabled = 1; //!< Whether this system is enabled (boolean).

  engine::ToneMapper tone_mapper = engine::ToneMapper::kAcesFitted;
  engine::ExposureMode exposure_mode = engine::ExposureMode::kAuto;
  float exposure_compensation_ev = 0.0F;

  float auto_exposure_min_ev = -6.0F;
  float auto_exposure_max_ev = 16.0F;
  float auto_exposure_speed_up = 3.0F;
  float auto_exposure_speed_down = 1.0F;

  float bloom_intensity = 0.0F;
  float bloom_threshold = 1.0F;

  float saturation = 1.0F;
  float contrast = 1.0F;
  float vignette_intensity = 0.0F;
};
#pragma pack(pop)
static_assert(sizeof(PostProcessVolumeEnvironmentRecord) == 60);

//! Known SceneEnvironment record tags and their exact packed sizes.
struct EnvironmentRecordSizeEntry {
  uint32_t system_type = 0;
  size_t record_size = 0;
};

inline constexpr std::array kKnownEnvironmentRecordSizes {
  EnvironmentRecordSizeEntry {
    .system_type = nostd::to_underlying(
      EnvironmentComponentType::kSkyAtmosphere),
    .record_size = sizeof(SkyAtmosphereEnvironmentRecord),
  },
  EnvironmentRecordSizeEntry {
    .system_type = nostd::to_underlying(
      EnvironmentComponentType::kVolumetricClouds),
    .record_size = sizeof(VolumetricCloudsEnvironmentRecord),
  },
  EnvironmentRecordSizeEntry {
    .system_type = nostd::to_underlying(EnvironmentComponentType::kFog),
    .record_size = sizeof(FogEnvironmentRecord),
  },
  EnvironmentRecordSizeEntry {
    .system_type = nostd::to_underlying(EnvironmentComponentType::kSkyLight),
    .record_size = sizeof(SkyLightEnvironmentRecord),
  },
  EnvironmentRecordSizeEntry {
    .system_type = nostd::to_underlying(EnvironmentComponentType::kSkySphere),
    .record_size = sizeof(SkySphereEnvironmentRecord),
  },
  EnvironmentRecordSizeEntry {
    .system_type = nostd::to_underlying(
      EnvironmentComponentType::kPostProcessVolume),
    .record_size = sizeof(PostProcessVolumeEnvironmentRecord),
  },
};

[[nodiscard]] inline constexpr auto ExpectedEnvironmentRecordSize(
  const uint32_t system_type) noexcept -> std::optional<size_t>
{
  for (const auto& entry : kKnownEnvironmentRecordSizes) {
    if (entry.system_type == system_type) {
      return entry.record_size;
    }
  }
  return std::nullopt;
}

//! Common shadow settings packed into light component records.
#pragma pack(push, 1)
struct LightShadowSettingsRecord {
  float bias = 0.0F;
  float normal_bias = 0.0F;
  uint32_t contact_shadows = 0;
  uint8_t resolution_hint = 1; // ShadowResolutionHint
};
#pragma pack(pop)
static_assert(sizeof(LightShadowSettingsRecord) == 13);

//! Common authored properties shared by all light records.
/*!
  @note Intensity fields are now in specific light records:
  - DirectionalLightRecord::intensity_lux (lux, lm/m²)
  - PointLightRecord::luminous_flux_lm (lumens)
  - SpotLightRecord::luminous_flux_lm (lumens)
*/
#pragma pack(push, 1)
struct LightCommonRecord {
  uint32_t affects_world = 1;
  float color_rgb[3] = { 1.0F, 1.0F, 1.0F };
  // intensity REMOVED - now in specific light records with physical units

  uint8_t mobility = 0; // LightMobility
  uint8_t casts_shadows = 0;

  LightShadowSettingsRecord shadow = {};
  float exposure_compensation_ev = 0.0F;
};
#pragma pack(pop)
static_assert(sizeof(LightCommonRecord) == 35);

//! Packed directional light component record.
/*!
  Contains `intensity_lux` for physical illuminance in lux (lm/m²).

 * Typical values: 100,000 lux (bright sun), 10,000 lux (overcast).
*/
#pragma pack(push, 1)
struct DirectionalLightRecord {
  world::SceneNodeIndexT node_index = 0;
  LightCommonRecord common = {};
  float angular_size_radians = 0.0F;
  uint32_t environment_contribution = 0;

  uint32_t is_sun_light = 0;

  uint32_t cascade_count = 4;
  float cascade_distances[4] = { 8.0F, 24.0F, 64.0F, 160.0F };
  float distribution_exponent = 3.0F;
  uint8_t split_mode = 0;
  float max_shadow_distance = 160.0F;
  float transition_fraction = 0.1F;
  float distance_fadeout_fraction = 0.1F;

  float intensity_lux = 100000.0F; //!< Illuminance in lux (lm/m²)
};
#pragma pack(pop)
static_assert(sizeof(DirectionalLightRecord) == 92);

//! Packed point light component record.
/*!
  Contains `luminous_flux_lm` for physical luminous flux in lumens.
  Typical values: 800 lm (~60W incandescent), 1600 lm (~100W).
*/
#pragma pack(push, 1)
struct PointLightRecord {
  world::SceneNodeIndexT node_index = 0;
  LightCommonRecord common = {};
  float range = 10.0F;
  float decay_exponent = 2.0F;
  float source_radius = 0.0F;
  float luminous_flux_lm = 800.0F; //!< Luminous flux in lumens (lm)
  uint8_t attenuation_model = 0; // AttenuationModel
};
#pragma pack(pop)
static_assert(sizeof(PointLightRecord) == 56);

//! Packed spot light component record.
/*!
  Contains `luminous_flux_lm` for physical luminous flux in lumens.
  Typical values: 800 lm (~60W incandescent), 1600 lm (~100W).
*/
#pragma pack(push, 1)
struct SpotLightRecord {
  world::SceneNodeIndexT node_index = 0;
  LightCommonRecord common = {};
  float range = 10.0F;
  float decay_exponent = 2.0F;
  float inner_cone_angle_radians = 0.4F;
  float outer_cone_angle_radians = 0.6F;
  float source_radius = 0.0F;
  float luminous_flux_lm = 800.0F; //!< Luminous flux in lumens (lm)
  uint8_t attenuation_model = 0; // AttenuationModel
};
#pragma pack(pop)
static_assert(sizeof(SpotLightRecord) == 64);

} // namespace oxygen::data::pak::world

// NOLINTEND(*-avoid-c-arrays,*-magic-numbers)
OXYGEN_DIAGNOSTIC_POP
