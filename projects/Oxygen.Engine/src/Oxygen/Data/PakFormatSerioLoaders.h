//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <span>
#include <type_traits>

#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Serio/Reader.h>

namespace oxygen::serio {

// ResourceIndexT is a NamedType; deserialize from its packed uint32 payload.
inline auto Load(AnyReader& reader, data::pak::core::ResourceIndexT& value)
  -> Result<void>
{
  uint32_t raw_value = 0;
  CHECK_RESULT(reader.ReadInto(raw_value));
  value = data::pak::core::ResourceIndexT { raw_value };
  return {};
}

inline auto Load(AnyReader& reader, data::AssetKey& key) -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);
  auto bytes = std::array<uint8_t, data::AssetKey::kSizeBytes> {};
  CHECK_RESULT(
    reader.ReadBlobInto(std::as_writable_bytes(std::span { bytes })));
  key = data::AssetKey::FromBytes(bytes);
  return {};
}

inline auto Load(AnyReader& reader, data::pak::core::AssetHeader& header)
  -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);
  CHECK_RESULT(reader.ReadInto(header.asset_type));
  CHECK_RESULT(
    reader.ReadBlobInto(std::as_writable_bytes(std::span { header.name })));
  CHECK_RESULT(reader.ReadInto(header.version));
  CHECK_RESULT(reader.ReadInto(header.streaming_priority));
  CHECK_RESULT(reader.ReadBlobInto(
    std::as_writable_bytes(std::span { header.content_hash })));
  CHECK_RESULT(reader.ReadInto(header.variant_flags));
  return {};
}

inline auto Load(AnyReader& reader, data::pak::core::ResourceRegion& region)
  -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);
  CHECK_RESULT(reader.ReadInto(region.offset));
  CHECK_RESULT(reader.ReadInto(region.size));
  return {};
}

inline auto Load(AnyReader& reader, data::pak::core::ResourceTable& table)
  -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);
  CHECK_RESULT(reader.ReadInto(table.offset));
  CHECK_RESULT(reader.ReadInto(table.count));
  CHECK_RESULT(reader.ReadInto(table.entry_size));
  return {};
}

inline auto Load(AnyReader& reader, data::pak::core::PakHeader& header)
  -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);
  CHECK_RESULT(
    reader.ReadBlobInto(std::as_writable_bytes(std::span { header.magic })));
  CHECK_RESULT(reader.ReadInto(header.version));
  CHECK_RESULT(reader.ReadInto(header.content_version));
  CHECK_RESULT(reader.ReadBlobInto(
    std::as_writable_bytes(std::span { header.source_identity })));
  CHECK_RESULT(reader.ReadBlobInto(
    std::as_writable_bytes(std::span { header._reserved })));
  return {};
}

inline auto Load(AnyReader& reader, data::pak::core::PakFooter& footer)
  -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);
  CHECK_RESULT(reader.ReadInto(footer.directory_offset));
  CHECK_RESULT(reader.ReadInto(footer.directory_size));
  CHECK_RESULT(reader.ReadInto(footer.asset_count));
  CHECK_RESULT(reader.ReadInto(footer.texture_region));
  CHECK_RESULT(reader.ReadInto(footer.buffer_region));
  CHECK_RESULT(reader.ReadInto(footer.audio_region));
  CHECK_RESULT(reader.ReadInto(footer.script_region));
  CHECK_RESULT(reader.ReadInto(footer.physics_region));
  CHECK_RESULT(reader.ReadInto(footer.texture_table));
  CHECK_RESULT(reader.ReadInto(footer.buffer_table));
  CHECK_RESULT(reader.ReadInto(footer.audio_table));
  CHECK_RESULT(reader.ReadInto(footer.script_resource_table));
  CHECK_RESULT(reader.ReadInto(footer.script_slot_table));
  CHECK_RESULT(reader.ReadInto(footer.physics_resource_table));
  CHECK_RESULT(reader.ReadInto(footer.browse_index_offset));
  CHECK_RESULT(reader.ReadInto(footer.browse_index_size));
  CHECK_RESULT(reader.ReadBlobInto(
    std::as_writable_bytes(std::span { footer._reserved })));
  CHECK_RESULT(reader.ReadInto(footer.pak_crc32));
  CHECK_RESULT(reader.ReadBlobInto(
    std::as_writable_bytes(std::span { footer.footer_magic })));
  return {};
}
// Note: v7 is the only supported schema for PakHeader/PakFooter in core.

inline auto Load(AnyReader& reader, data::pak::core::AssetDirectoryEntry& entry)
  -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);
  CHECK_RESULT(reader.ReadInto(entry.asset_key));
  CHECK_RESULT(reader.ReadInto(entry.asset_type));
  CHECK_RESULT(reader.ReadInto(entry.entry_offset));
  CHECK_RESULT(reader.ReadInto(entry.desc_offset));
  CHECK_RESULT(reader.ReadInto(entry.desc_size));
  CHECK_RESULT(
    reader.ReadBlobInto(std::as_writable_bytes(std::span { entry._reserved })));
  return {};
}

inline auto Load(AnyReader& reader,
  data::pak::core::PakBrowseIndexHeader& header) -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);
  CHECK_RESULT(
    reader.ReadBlobInto(std::as_writable_bytes(std::span { header.magic })));
  CHECK_RESULT(reader.ReadInto(header.version));
  CHECK_RESULT(reader.ReadInto(header.entry_count));
  CHECK_RESULT(reader.ReadInto(header.string_table_size));
  return {};
}

inline auto Load(AnyReader& reader, data::pak::core::PakBrowseIndexEntry& entry)
  -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);
  CHECK_RESULT(reader.ReadInto(entry.asset_key));
  CHECK_RESULT(reader.ReadInto(entry.virtual_path_offset));
  CHECK_RESULT(reader.ReadInto(entry.virtual_path_length));
  return {};
}

inline auto Load(AnyReader& reader, data::pak::core::BufferResourceDesc& desc)
  -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);
  CHECK_RESULT(reader.ReadInto(desc.data_offset));
  CHECK_RESULT(reader.ReadInto(desc.size_bytes));
  CHECK_RESULT(reader.ReadInto(desc.usage_flags));
  CHECK_RESULT(reader.ReadInto(desc.element_stride));
  CHECK_RESULT(reader.ReadInto(desc.element_format));
  CHECK_RESULT(reader.ReadInto(desc.content_hash));
  return {};
}

inline auto Load(AnyReader& reader, data::pak::core::TextureResourceDesc& desc)
  -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);
  CHECK_RESULT(reader.ReadInto(desc.data_offset));
  CHECK_RESULT(reader.ReadInto(desc.size_bytes));
  CHECK_RESULT(reader.ReadInto(desc.texture_type));
  CHECK_RESULT(reader.ReadInto(desc.compression_type));
  CHECK_RESULT(reader.ReadInto(desc.width));
  CHECK_RESULT(reader.ReadInto(desc.height));
  CHECK_RESULT(reader.ReadInto(desc.depth));
  CHECK_RESULT(reader.ReadInto(desc.array_layers));
  CHECK_RESULT(reader.ReadInto(desc.mip_levels));
  CHECK_RESULT(reader.ReadInto(desc.format));
  CHECK_RESULT(reader.ReadInto(desc.alignment));
  CHECK_RESULT(reader.ReadInto(desc.content_hash));
  return {};
}

inline auto Load(AnyReader& reader, data::pak::geometry::MeshViewDesc& desc)
  -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);
  CHECK_RESULT(reader.ReadInto(desc.first_index));
  CHECK_RESULT(reader.ReadInto(desc.index_count));
  CHECK_RESULT(reader.ReadInto(desc.first_vertex));
  CHECK_RESULT(reader.ReadInto(desc.vertex_count));
  return {};
}

inline auto Load(AnyReader& reader,
  data::pak::render::ShaderReferenceDesc& desc) -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);
  CHECK_RESULT(reader.ReadInto(desc.shader_type));
  CHECK_RESULT(reader.ReadBlobInto(
    std::as_writable_bytes(std::span { desc.source_path })));
  CHECK_RESULT(reader.ReadBlobInto(
    std::as_writable_bytes(std::span { desc.entry_point })));
  CHECK_RESULT(
    reader.ReadBlobInto(std::as_writable_bytes(std::span { desc.defines })));
  CHECK_RESULT(reader.ReadInto(desc.shader_hash));
  return {};
}

inline auto Load(AnyReader& reader, engine::ToneMapper& value) -> Result<void>
{
  std::underlying_type_t<engine::ToneMapper> raw_value = 0;
  CHECK_RESULT(reader.ReadInto(raw_value));
  value = static_cast<engine::ToneMapper>(raw_value);
  return {};
}

inline auto Load(AnyReader& reader, engine::ExposureMode& value) -> Result<void>
{
  std::underlying_type_t<engine::ExposureMode> raw_value = 0;
  CHECK_RESULT(reader.ReadInto(raw_value));
  value = static_cast<engine::ExposureMode>(raw_value);
  return {};
}

//=== Scene: Nodes & Components (v2/v3) ===----------------------------------//

inline auto Load(AnyReader& reader, data::pak::world::SceneDataTable& table)
  -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);

  CHECK_RESULT(reader.ReadInto(table.offset));
  CHECK_RESULT(reader.ReadInto(table.count));
  CHECK_RESULT(reader.ReadInto(table.entry_size));

  return {};
}

inline auto Load(AnyReader& reader, data::pak::world::SceneStringTable& table)
  -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);

  CHECK_RESULT(reader.ReadInto(table.offset));
  CHECK_RESULT(reader.ReadInto(table.size));

  return {};
}

inline auto Load(AnyReader& reader, data::pak::world::SceneAssetDesc& desc)
  -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);

  CHECK_RESULT(reader.ReadInto(desc.header));
  CHECK_RESULT(reader.ReadInto(desc.nodes));
  CHECK_RESULT(reader.ReadInto(desc.scene_strings));
  CHECK_RESULT(reader.ReadInto(desc.component_table_directory_offset));
  CHECK_RESULT(reader.ReadInto(desc.component_table_count));

  return {};
}

inline auto Load(AnyReader& reader,
  data::pak::world::SceneComponentTableDesc& desc) -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);

  CHECK_RESULT(reader.ReadInto(desc.component_type));
  CHECK_RESULT(reader.ReadInto(desc.table));

  return {};
}

inline auto Load(AnyReader& reader, data::pak::world::NodeRecord& record)
  -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);

  CHECK_RESULT(reader.ReadInto(record.node_id));
  CHECK_RESULT(reader.ReadInto(record.scene_name_offset));
  CHECK_RESULT(reader.ReadInto(record.parent_index));
  CHECK_RESULT(reader.ReadInto(record.node_flags));

  for (auto& v : record.translation) {
    CHECK_RESULT(reader.ReadInto(v));
  }
  for (auto& v : record.rotation) {
    CHECK_RESULT(reader.ReadInto(v));
  }
  for (auto& v : record.scale) {
    CHECK_RESULT(reader.ReadInto(v));
  }

  return {};
}

inline auto Load(AnyReader& reader, data::pak::world::RenderableRecord& record)
  -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);

  CHECK_RESULT(reader.ReadInto(record.node_index));
  CHECK_RESULT(reader.ReadInto(record.geometry_key));
  CHECK_RESULT(reader.ReadInto(record.material_key));
  CHECK_RESULT(reader.ReadInto(record.visible));

  return {};
}

inline auto Load(AnyReader& reader,
  data::pak::world::LocalFogVolumeRecord& record) -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);

  CHECK_RESULT(reader.ReadInto(record.node_index));
  CHECK_RESULT(reader.ReadInto(record.enabled));
  CHECK_RESULT(reader.ReadInto(record.radial_fog_extinction));
  CHECK_RESULT(reader.ReadInto(record.height_fog_extinction));
  CHECK_RESULT(reader.ReadInto(record.height_fog_falloff));
  CHECK_RESULT(reader.ReadInto(record.height_fog_offset));
  CHECK_RESULT(reader.ReadInto(record.fog_phase_g));
  for (auto& v : record.fog_albedo) {
    CHECK_RESULT(reader.ReadInto(v));
  }
  for (auto& v : record.fog_emissive) {
    CHECK_RESULT(reader.ReadInto(v));
  }
  CHECK_RESULT(reader.ReadInto(record.sort_priority));

  return {};
}

inline auto Load(AnyReader& reader,
  data::pak::world::PerspectiveCameraRecord& record) -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);

  CHECK_RESULT(reader.ReadInto(record.node_index));
  CHECK_RESULT(reader.ReadInto(record.fov_y));
  CHECK_RESULT(reader.ReadInto(record.aspect_ratio));
  CHECK_RESULT(reader.ReadInto(record.near_plane));
  CHECK_RESULT(reader.ReadInto(record.far_plane));

  return {};
}

inline auto Load(AnyReader& reader,
  data::pak::world::OrthographicCameraRecord& record) -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);

  CHECK_RESULT(reader.ReadInto(record.node_index));
  CHECK_RESULT(reader.ReadInto(record.left));
  CHECK_RESULT(reader.ReadInto(record.right));
  CHECK_RESULT(reader.ReadInto(record.bottom));
  CHECK_RESULT(reader.ReadInto(record.top));
  CHECK_RESULT(reader.ReadInto(record.near_plane));
  CHECK_RESULT(reader.ReadInto(record.far_plane));

  return {};
}

inline auto Load(AnyReader& reader,
  data::pak::scripting::ScriptingComponentRecord& record) -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);

  CHECK_RESULT(reader.ReadInto(record.node_index));
  CHECK_RESULT(reader.ReadInto(record.flags));
  CHECK_RESULT(reader.ReadInto(record.slot_start_index));
  CHECK_RESULT(reader.ReadInto(record.slot_count));

  return {};
}

//=== Scene: Environment (v3+) ===------------------------------------------//

inline auto Load(AnyReader& reader,
  data::pak::world::SceneEnvironmentBlockHeader& hdr) -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);
  CHECK_RESULT(reader.ReadInto(hdr.byte_size));
  CHECK_RESULT(reader.ReadInto(hdr.systems_count));
  return {};
}

inline auto Load(AnyReader& reader,
  data::pak::world::SceneEnvironmentSystemRecordHeader& hdr) -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);
  CHECK_RESULT(reader.ReadInto(hdr.system_type));
  CHECK_RESULT(reader.ReadInto(hdr.record_size));
  return {};
}

inline auto Load(AnyReader& reader,
  data::pak::world::SkyAtmosphereEnvironmentRecord& r) -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);

  CHECK_RESULT(reader.ReadInto(r.header));
  CHECK_RESULT(reader.ReadInto(r.enabled));
  CHECK_RESULT(reader.ReadInto(r.planet_radius_m));
  CHECK_RESULT(reader.ReadInto(r.atmosphere_height_m));

  for (auto& v : r.ground_albedo_rgb) {
    CHECK_RESULT(reader.ReadInto(v));
  }

  for (auto& v : r.rayleigh_scattering_rgb) {
    CHECK_RESULT(reader.ReadInto(v));
  }
  CHECK_RESULT(reader.ReadInto(r.rayleigh_scale_height_m));

  for (auto& v : r.mie_scattering_rgb) {
    CHECK_RESULT(reader.ReadInto(v));
  }
  for (auto& v : r.mie_absorption_rgb) {
    CHECK_RESULT(reader.ReadInto(v));
  }
  CHECK_RESULT(reader.ReadInto(r.mie_scale_height_m));
  CHECK_RESULT(reader.ReadInto(r.mie_g));

  for (auto& v : r.absorption_rgb) {
    CHECK_RESULT(reader.ReadInto(v));
  }
  for (auto& v : r.ozone_density_profile) {
    CHECK_RESULT(reader.ReadInto(v));
  }

  CHECK_RESULT(reader.ReadInto(r.multi_scattering_factor));
  for (auto& v : r.sky_luminance_factor_rgb) {
    CHECK_RESULT(reader.ReadInto(v));
  }
  for (auto& v : r.sky_and_aerial_perspective_luminance_factor_rgb) {
    CHECK_RESULT(reader.ReadInto(v));
  }
  CHECK_RESULT(reader.ReadInto(r.aerial_perspective_distance_scale));
  CHECK_RESULT(reader.ReadInto(r.aerial_scattering_strength));
  CHECK_RESULT(reader.ReadInto(r.aerial_perspective_start_depth_m));
  CHECK_RESULT(reader.ReadInto(r.height_fog_contribution));
  CHECK_RESULT(reader.ReadInto(r.trace_sample_count_scale));
  CHECK_RESULT(reader.ReadInto(r.transmittance_min_light_elevation_deg));
  CHECK_RESULT(reader.ReadInto(r.sun_disk_enabled));
  CHECK_RESULT(reader.ReadInto(r.holdout));
  CHECK_RESULT(reader.ReadInto(r.render_in_main_pass));

  return {};
}

inline auto Load(AnyReader& reader,
  data::pak::world::VolumetricCloudsEnvironmentRecord& r) -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);

  CHECK_RESULT(reader.ReadInto(r.header));
  CHECK_RESULT(reader.ReadInto(r.enabled));
  CHECK_RESULT(reader.ReadInto(r.base_altitude_m));
  CHECK_RESULT(reader.ReadInto(r.layer_thickness_m));
  CHECK_RESULT(reader.ReadInto(r.coverage));
  CHECK_RESULT(reader.ReadInto(r.extinction_sigma_t_per_m));

  for (auto& v : r.single_scattering_albedo_rgb) {
    CHECK_RESULT(reader.ReadInto(v));
  }
  CHECK_RESULT(reader.ReadInto(r.phase_g));

  for (auto& v : r.wind_dir_ws) {
    CHECK_RESULT(reader.ReadInto(v));
  }
  CHECK_RESULT(reader.ReadInto(r.wind_speed_mps));
  CHECK_RESULT(reader.ReadInto(r.shadow_strength));

  return {};
}

inline auto Load(AnyReader& reader, data::pak::world::FogEnvironmentRecord& r)
  -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);

  CHECK_RESULT(reader.ReadInto(r.header));
  CHECK_RESULT(reader.ReadInto(r.enabled));
  CHECK_RESULT(reader.ReadInto(r.model));
  CHECK_RESULT(reader.ReadInto(r.extinction_sigma_t_per_m));
  CHECK_RESULT(reader.ReadInto(r.height_falloff_per_m));
  CHECK_RESULT(reader.ReadInto(r.height_offset_m));
  CHECK_RESULT(reader.ReadInto(r.start_distance_m));
  CHECK_RESULT(reader.ReadInto(r.max_opacity));
  for (auto& v : r.single_scattering_albedo_rgb) {
    CHECK_RESULT(reader.ReadInto(v));
  }
  CHECK_RESULT(reader.ReadInto(r.anisotropy_g));
  CHECK_RESULT(reader.ReadInto(r.enable_height_fog));
  CHECK_RESULT(reader.ReadInto(r.enable_volumetric_fog));
  CHECK_RESULT(reader.ReadInto(r.second_fog_density));
  CHECK_RESULT(reader.ReadInto(r.second_fog_height_falloff));
  CHECK_RESULT(reader.ReadInto(r.second_fog_height_offset));
  for (auto& v : r.fog_inscattering_luminance) {
    CHECK_RESULT(reader.ReadInto(v));
  }
  for (auto& v : r.sky_atmosphere_ambient_contribution_color_scale) {
    CHECK_RESULT(reader.ReadInto(v));
  }
  CHECK_RESULT(reader.ReadInto(r.inscattering_color_cubemap_asset));
  CHECK_RESULT(reader.ReadInto(r.inscattering_color_cubemap_angle));
  for (auto& v : r.inscattering_texture_tint) {
    CHECK_RESULT(reader.ReadInto(v));
  }
  CHECK_RESULT(reader.ReadInto(r.fully_directional_inscattering_color_distance));
  CHECK_RESULT(reader.ReadInto(r.non_directional_inscattering_color_distance));
  for (auto& v : r.directional_inscattering_luminance) {
    CHECK_RESULT(reader.ReadInto(v));
  }
  CHECK_RESULT(reader.ReadInto(r.directional_inscattering_exponent));
  CHECK_RESULT(reader.ReadInto(r.directional_inscattering_start_distance));
  CHECK_RESULT(reader.ReadInto(r.end_distance_m));
  CHECK_RESULT(reader.ReadInto(r.fog_cutoff_distance_m));
  CHECK_RESULT(reader.ReadInto(r.volumetric_fog_scattering_distribution));
  for (auto& v : r.volumetric_fog_albedo) {
    CHECK_RESULT(reader.ReadInto(v));
  }
  for (auto& v : r.volumetric_fog_emissive) {
    CHECK_RESULT(reader.ReadInto(v));
  }
  CHECK_RESULT(reader.ReadInto(r.volumetric_fog_extinction_scale));
  CHECK_RESULT(reader.ReadInto(r.volumetric_fog_distance));
  CHECK_RESULT(reader.ReadInto(r.volumetric_fog_start_distance));
  CHECK_RESULT(reader.ReadInto(r.volumetric_fog_near_fade_in_distance));
  CHECK_RESULT(
    reader.ReadInto(r.volumetric_fog_static_lighting_scattering_intensity));
  CHECK_RESULT(
    reader.ReadInto(r.override_light_colors_with_fog_inscattering_colors));
  CHECK_RESULT(reader.ReadInto(r.holdout));
  CHECK_RESULT(reader.ReadInto(r.render_in_main_pass));
  CHECK_RESULT(reader.ReadInto(r.visible_in_reflection_captures));
  CHECK_RESULT(reader.ReadInto(r.visible_in_real_time_sky_captures));

  return {};
}

inline auto Load(AnyReader& reader,
  data::pak::world::SkyLightEnvironmentRecord& r) -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);

  CHECK_RESULT(reader.ReadInto(r.header));
  CHECK_RESULT(reader.ReadInto(r.enabled));
  CHECK_RESULT(reader.ReadInto(r.source));

  CHECK_RESULT(reader.ReadInto(r.cubemap_asset));

  CHECK_RESULT(reader.ReadInto(r.intensity));
  for (auto& v : r.tint_rgb) {
    CHECK_RESULT(reader.ReadInto(v));
  }
  CHECK_RESULT(reader.ReadInto(r.diffuse_intensity));
  CHECK_RESULT(reader.ReadInto(r.specular_intensity));
  CHECK_RESULT(reader.ReadInto(r.real_time_capture_enabled));
  for (auto& v : r.lower_hemisphere_color) {
    CHECK_RESULT(reader.ReadInto(v));
  }
  CHECK_RESULT(reader.ReadInto(r.volumetric_scattering_intensity));
  CHECK_RESULT(reader.ReadInto(r.affect_reflections));
  CHECK_RESULT(reader.ReadInto(r.affect_global_illumination));

  return {};
}

inline auto Load(AnyReader& reader,
  data::pak::world::SkySphereEnvironmentRecord& r) -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);

  CHECK_RESULT(reader.ReadInto(r.header));
  CHECK_RESULT(reader.ReadInto(r.enabled));
  CHECK_RESULT(reader.ReadInto(r.source));

  CHECK_RESULT(reader.ReadInto(r.cubemap_asset));

  for (auto& v : r.solid_color_rgb) {
    CHECK_RESULT(reader.ReadInto(v));
  }
  CHECK_RESULT(reader.ReadInto(r.intensity));
  CHECK_RESULT(reader.ReadInto(r.rotation_radians));
  for (auto& v : r.tint_rgb) {
    CHECK_RESULT(reader.ReadInto(v));
  }

  return {};
}

inline auto Load(AnyReader& reader,
  data::pak::world::PostProcessVolumeEnvironmentRecord& r) -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);

  CHECK_RESULT(reader.ReadInto(r.header));
  CHECK_RESULT(reader.ReadInto(r.enabled));
  CHECK_RESULT(reader.ReadInto(r.tone_mapper));
  CHECK_RESULT(reader.ReadInto(r.exposure_mode));
  CHECK_RESULT(reader.ReadInto(r.exposure_compensation_ev));
  CHECK_RESULT(reader.ReadInto(r.auto_exposure_min_ev));
  CHECK_RESULT(reader.ReadInto(r.auto_exposure_max_ev));
  CHECK_RESULT(reader.ReadInto(r.auto_exposure_speed_up));
  CHECK_RESULT(reader.ReadInto(r.auto_exposure_speed_down));
  CHECK_RESULT(reader.ReadInto(r.bloom_intensity));
  CHECK_RESULT(reader.ReadInto(r.bloom_threshold));
  CHECK_RESULT(reader.ReadInto(r.saturation));
  CHECK_RESULT(reader.ReadInto(r.contrast));
  CHECK_RESULT(reader.ReadInto(r.vignette_intensity));

  return {};
}

//=== Scene: Lights (v3+) ===------------------------------------------------//

inline auto Load(AnyReader& reader,
  data::pak::world::LightShadowSettingsRecord& r) -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);

  CHECK_RESULT(reader.ReadInto(r.bias));
  CHECK_RESULT(reader.ReadInto(r.normal_bias));
  CHECK_RESULT(reader.ReadInto(r.contact_shadows));
  CHECK_RESULT(reader.ReadInto(r.resolution_hint));

  return {};
}

inline auto Load(AnyReader& reader, data::pak::world::LightCommonRecord& r)
  -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);

  CHECK_RESULT(reader.ReadInto(r.affects_world));
  for (auto& v : r.color_rgb) {
    CHECK_RESULT(reader.ReadInto(v));
  }
  // intensity REMOVED from common - now in specific light records

  CHECK_RESULT(reader.ReadInto(r.mobility));
  CHECK_RESULT(reader.ReadInto(r.casts_shadows));

  CHECK_RESULT(reader.ReadInto(r.shadow));
  CHECK_RESULT(reader.ReadInto(r.exposure_compensation_ev));

  return {};
}

inline auto Load(AnyReader& reader, data::pak::world::DirectionalLightRecord& r)
  -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);

  CHECK_RESULT(reader.ReadInto(r.node_index));
  CHECK_RESULT(reader.ReadInto(r.common));
  CHECK_RESULT(reader.ReadInto(r.angular_size_radians));
  CHECK_RESULT(reader.ReadInto(r.environment_contribution));
  CHECK_RESULT(reader.ReadInto(r.is_sun_light));

  CHECK_RESULT(reader.ReadInto(r.cascade_count));
  for (auto& v : r.cascade_distances) {
    CHECK_RESULT(reader.ReadInto(v));
  }
  CHECK_RESULT(reader.ReadInto(r.distribution_exponent));
  CHECK_RESULT(reader.ReadInto(r.split_mode));
  CHECK_RESULT(reader.ReadInto(r.max_shadow_distance));
  CHECK_RESULT(reader.ReadInto(r.transition_fraction));
  CHECK_RESULT(reader.ReadInto(r.distance_fadeout_fraction));
  CHECK_RESULT(reader.ReadInto(r.intensity_lux));

  return {};
}

inline auto Load(AnyReader& reader, data::pak::world::PointLightRecord& r)
  -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);

  CHECK_RESULT(reader.ReadInto(r.node_index));
  CHECK_RESULT(reader.ReadInto(r.common));
  CHECK_RESULT(reader.ReadInto(r.range));
  CHECK_RESULT(reader.ReadInto(r.decay_exponent));
  CHECK_RESULT(reader.ReadInto(r.source_radius));
  CHECK_RESULT(reader.ReadInto(r.luminous_flux_lm));
  CHECK_RESULT(reader.ReadInto(r.attenuation_model));

  return {};
}

inline auto Load(AnyReader& reader, data::pak::world::SpotLightRecord& r)
  -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);

  CHECK_RESULT(reader.ReadInto(r.node_index));
  CHECK_RESULT(reader.ReadInto(r.common));
  CHECK_RESULT(reader.ReadInto(r.range));
  CHECK_RESULT(reader.ReadInto(r.decay_exponent));
  CHECK_RESULT(reader.ReadInto(r.inner_cone_angle_radians));
  CHECK_RESULT(reader.ReadInto(r.outer_cone_angle_radians));
  CHECK_RESULT(reader.ReadInto(r.source_radius));
  CHECK_RESULT(reader.ReadInto(r.luminous_flux_lm));
  CHECK_RESULT(reader.ReadInto(r.attenuation_model));

  return {};
}

//=== Input (v6) ===---------------------------------------------------------//

inline auto Load(AnyReader& reader, data::pak::input::InputDataTable& record)
  -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);

  CHECK_RESULT(reader.ReadInto(record.offset));
  CHECK_RESULT(reader.ReadInto(record.count));
  CHECK_RESULT(reader.ReadInto(record.entry_size));

  return {};
}

inline auto Load(AnyReader& reader,
  data::pak::input::InputActionMappingRecord& record) -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);

  CHECK_RESULT(reader.ReadInto(record.action_asset_key));
  CHECK_RESULT(reader.ReadInto(record.slot_name_offset));
  CHECK_RESULT(reader.ReadInto(record.trigger_start_index));
  CHECK_RESULT(reader.ReadInto(record.trigger_count));
  CHECK_RESULT(reader.ReadInto(record.flags));
  for (auto& v : record.scale) {
    CHECK_RESULT(reader.ReadInto(v));
  }
  for (auto& v : record.bias) {
    CHECK_RESULT(reader.ReadInto(v));
  }

  return {};
}

inline auto Load(AnyReader& reader,
  data::pak::input::InputTriggerRecord& record) -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);

  CHECK_RESULT(reader.ReadInto(record.type));
  CHECK_RESULT(reader.ReadInto(record.behavior));
  CHECK_RESULT(reader.ReadInto(record.flags));
  CHECK_RESULT(reader.ReadInto(record.actuation_threshold));
  CHECK_RESULT(reader.ReadInto(record.linked_action_asset_key));
  CHECK_RESULT(reader.ReadInto(record.aux_start_index));
  CHECK_RESULT(reader.ReadInto(record.aux_count));
  for (auto& v : record.fparams) {
    CHECK_RESULT(reader.ReadInto(v));
  }
  for (auto& v : record.uparams) {
    CHECK_RESULT(reader.ReadInto(v));
  }

  return {};
}

inline auto Load(AnyReader& reader,
  data::pak::input::InputTriggerAuxRecord& record) -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);

  CHECK_RESULT(reader.ReadInto(record.action_asset_key));
  CHECK_RESULT(reader.ReadInto(record.completion_states));
  CHECK_RESULT(reader.ReadInto(record.time_to_complete_ns));
  CHECK_RESULT(reader.ReadInto(record.flags));

  return {};
}

//=== Physics (v7) ===-------------------------------------------------------//

inline auto Load(AnyReader& reader,
  data::pak::physics::PhysicsResourceFormat& value) -> Result<void>
{
  std::underlying_type_t<data::pak::physics::PhysicsResourceFormat> raw_value
    = 0;
  CHECK_RESULT(reader.ReadInto(raw_value));
  value = static_cast<data::pak::physics::PhysicsResourceFormat>(raw_value);
  return {};
}

inline auto Load(AnyReader& reader,
  data::pak::physics::PhysicsCombineMode& value) -> Result<void>
{
  std::underlying_type_t<data::pak::physics::PhysicsCombineMode> raw_value = 0;
  CHECK_RESULT(reader.ReadInto(raw_value));
  value = static_cast<data::pak::physics::PhysicsCombineMode>(raw_value);
  return {};
}

inline auto Load(AnyReader& reader, data::pak::physics::ShapeType& value)
  -> Result<void>
{
  std::underlying_type_t<data::pak::physics::ShapeType> raw_value = 0;
  CHECK_RESULT(reader.ReadInto(raw_value));
  value = static_cast<data::pak::physics::ShapeType>(raw_value);
  return {};
}

inline auto Load(AnyReader& reader, data::pak::physics::ShapePayloadType& value)
  -> Result<void>
{
  std::underlying_type_t<data::pak::physics::ShapePayloadType> raw_value = 0;
  CHECK_RESULT(reader.ReadInto(raw_value));
  value = static_cast<data::pak::physics::ShapePayloadType>(raw_value);
  return {};
}

inline auto Load(AnyReader& reader,
  data::pak::physics::WorldBoundaryMode& value) -> Result<void>
{
  std::underlying_type_t<data::pak::physics::WorldBoundaryMode> raw_value = 0;
  CHECK_RESULT(reader.ReadInto(raw_value));
  value = static_cast<data::pak::physics::WorldBoundaryMode>(raw_value);
  return {};
}

inline auto Load(AnyReader& reader, data::pak::physics::PhysicsBodyType& value)
  -> Result<void>
{
  std::underlying_type_t<data::pak::physics::PhysicsBodyType> raw_value = 0;
  CHECK_RESULT(reader.ReadInto(raw_value));
  value = static_cast<data::pak::physics::PhysicsBodyType>(raw_value);
  return {};
}

inline auto Load(AnyReader& reader,
  data::pak::physics::PhysicsMotionQuality& value) -> Result<void>
{
  std::underlying_type_t<data::pak::physics::PhysicsMotionQuality> raw_value
    = 0;
  CHECK_RESULT(reader.ReadInto(raw_value));
  value = static_cast<data::pak::physics::PhysicsMotionQuality>(raw_value);
  return {};
}

inline auto Load(AnyReader& reader,
  data::pak::physics::SoftBodyTetherMode& value) -> Result<void>
{
  std::underlying_type_t<data::pak::physics::SoftBodyTetherMode> raw_value = 0;
  CHECK_RESULT(reader.ReadInto(raw_value));
  value = static_cast<data::pak::physics::SoftBodyTetherMode>(raw_value);
  return {};
}

inline auto Load(AnyReader& reader,
  data::pak::physics::AggregateAuthority& value) -> Result<void>
{
  std::underlying_type_t<data::pak::physics::AggregateAuthority> raw_value = 0;
  CHECK_RESULT(reader.ReadInto(raw_value));
  value = static_cast<data::pak::physics::AggregateAuthority>(raw_value);
  return {};
}

inline auto Load(AnyReader& reader,
  data::pak::physics::PhysicsBindingType& value) -> Result<void>
{
  std::underlying_type_t<data::pak::physics::PhysicsBindingType> raw_value = 0;
  CHECK_RESULT(reader.ReadInto(raw_value));
  value = static_cast<data::pak::physics::PhysicsBindingType>(raw_value);
  return {};
}

inline auto Load(AnyReader& reader,
  data::pak::physics::PhysicsResourceDesc& record) -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);
  CHECK_RESULT(reader.ReadInto(record.data_offset));
  CHECK_RESULT(reader.ReadInto(record.size_bytes));
  CHECK_RESULT(reader.ReadInto(record.format));
  CHECK_RESULT(reader.ReadInto(record.resource_asset_key));
  CHECK_RESULT(reader.ReadBlobInto(
    std::as_writable_bytes(std::span { record.content_hash })));
  return {};
}

inline auto Load(AnyReader& reader,
  data::pak::physics::PhysicsMaterialAssetDesc& record) -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);
  CHECK_RESULT(reader.ReadInto(record.header));
  CHECK_RESULT(reader.ReadInto(record.static_friction));
  CHECK_RESULT(reader.ReadInto(record.dynamic_friction));
  CHECK_RESULT(reader.ReadInto(record.restitution));
  CHECK_RESULT(reader.ReadInto(record.density));
  CHECK_RESULT(reader.ReadInto(record.combine_mode_friction));
  CHECK_RESULT(reader.ReadInto(record.combine_mode_restitution));
  return {};
}

inline auto Load(AnyReader& reader,
  data::pak::physics::CookedShapePayloadRef& record) -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);
  CHECK_RESULT(reader.ReadInto(record.payload_asset_key));
  CHECK_RESULT(reader.ReadInto(record.payload_type));
  return {};
}

inline auto Load(AnyReader& reader, data::pak::physics::ShapeParams& record)
  -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);
  CHECK_RESULT(
    reader.ReadBlobInto(std::as_writable_bytes(std::span { record.raw })));
  return {};
}

inline auto Load(AnyReader& reader,
  data::pak::physics::CompoundShapeChildDesc& record) -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);
  CHECK_RESULT(reader.ReadInto(record.shape_type));
  CHECK_RESULT(reader.ReadInto(record.radius));
  CHECK_RESULT(reader.ReadInto(record.half_height));
  for (auto& v : record.half_extents) {
    CHECK_RESULT(reader.ReadInto(v));
  }
  for (auto& v : record.normal) {
    CHECK_RESULT(reader.ReadInto(v));
  }
  CHECK_RESULT(reader.ReadInto(record.distance));
  CHECK_RESULT(reader.ReadInto(record.boundary_mode));
  for (auto& v : record.limits_min) {
    CHECK_RESULT(reader.ReadInto(v));
  }
  for (auto& v : record.limits_max) {
    CHECK_RESULT(reader.ReadInto(v));
  }
  for (auto& v : record.local_position) {
    CHECK_RESULT(reader.ReadInto(v));
  }
  for (auto& v : record.local_rotation) {
    CHECK_RESULT(reader.ReadInto(v));
  }
  for (auto& v : record.local_scale) {
    CHECK_RESULT(reader.ReadInto(v));
  }
  CHECK_RESULT(reader.ReadInto(record.payload_asset_key));
  CHECK_RESULT(reader.ReadBlobInto(
    std::as_writable_bytes(std::span { record._reserved })));
  return {};
}

inline auto Load(AnyReader& reader,
  data::pak::physics::CollisionShapeAssetDesc& record) -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);
  CHECK_RESULT(reader.ReadInto(record.header));
  CHECK_RESULT(reader.ReadInto(record.shape_type));
  for (auto& v : record.local_position) {
    CHECK_RESULT(reader.ReadInto(v));
  }
  for (auto& v : record.local_rotation) {
    CHECK_RESULT(reader.ReadInto(v));
  }
  for (auto& v : record.local_scale) {
    CHECK_RESULT(reader.ReadInto(v));
  }
  CHECK_RESULT(reader.ReadInto(record.is_sensor));
  CHECK_RESULT(reader.ReadInto(record.collision_own_layer));
  CHECK_RESULT(reader.ReadInto(record.collision_target_layers));
  CHECK_RESULT(reader.ReadInto(record.material_asset_key));
  CHECK_RESULT(reader.ReadInto(record.shape_params));
  CHECK_RESULT(reader.ReadInto(record.cooked_shape_ref));
  return {};
}

inline auto Load(AnyReader& reader,
  data::pak::physics::PhysicsComponentTableDesc& record) -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);
  CHECK_RESULT(reader.ReadInto(record.binding_type));
  CHECK_RESULT(reader.ReadInto(record.table));
  return {};
}

inline auto Load(AnyReader& reader,
  data::pak::physics::PhysicsSceneAssetDesc& record) -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);
  CHECK_RESULT(reader.ReadInto(record.header));
  CHECK_RESULT(reader.ReadInto(record.target_scene_key));
  CHECK_RESULT(reader.ReadInto(record.target_node_count));
  CHECK_RESULT(reader.ReadInto(record.component_table_count));
  CHECK_RESULT(reader.ReadInto(record.component_table_directory_offset));
  CHECK_RESULT(reader.ReadBlobInto(
    std::as_writable_bytes(std::span { record.target_scene_content_hash })));
  return {};
}

inline auto Load(AnyReader& reader,
  data::pak::physics::RigidBodyBackendScalars& record) -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);
  CHECK_RESULT(
    reader.ReadBlobInto(std::as_writable_bytes(std::span { record.raw })));
  return {};
}

inline auto Load(AnyReader& reader,
  data::pak::physics::CharacterBackendScalars& record) -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);
  CHECK_RESULT(
    reader.ReadBlobInto(std::as_writable_bytes(std::span { record.raw })));
  return {};
}

inline auto Load(AnyReader& reader,
  data::pak::physics::SoftBodyBackendScalars& record) -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);
  CHECK_RESULT(
    reader.ReadBlobInto(std::as_writable_bytes(std::span { record.raw })));
  return {};
}

inline auto Load(AnyReader& reader,
  data::pak::physics::JointBackendScalars& record) -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);
  CHECK_RESULT(
    reader.ReadBlobInto(std::as_writable_bytes(std::span { record.raw })));
  return {};
}

inline auto Load(AnyReader& reader,
  data::pak::physics::VehicleWheelBackendScalars& record) -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);
  CHECK_RESULT(
    reader.ReadBlobInto(std::as_writable_bytes(std::span { record.raw })));
  return {};
}

inline auto Load(AnyReader& reader,
  data::pak::physics::RigidBodyBindingRecord& record) -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);
  CHECK_RESULT(reader.ReadInto(record.node_index));
  CHECK_RESULT(reader.ReadInto(record.body_type));
  CHECK_RESULT(reader.ReadInto(record.motion_quality));
  CHECK_RESULT(reader.ReadInto(record.collision_layer));
  CHECK_RESULT(reader.ReadInto(record.collision_mask));
  CHECK_RESULT(reader.ReadInto(record.mass));
  CHECK_RESULT(reader.ReadInto(record.linear_damping));
  CHECK_RESULT(reader.ReadInto(record.angular_damping));
  CHECK_RESULT(reader.ReadInto(record.gravity_factor));
  CHECK_RESULT(reader.ReadInto(record.initial_activation));
  CHECK_RESULT(reader.ReadInto(record.is_sensor));
  CHECK_RESULT(reader.ReadInto(record.shape_asset_key));
  CHECK_RESULT(reader.ReadInto(record.material_asset_key));
  CHECK_RESULT(reader.ReadBlobInto(
    std::as_writable_bytes(std::span { record.com_override })));
  CHECK_RESULT(reader.ReadInto(record.has_com_override));
  CHECK_RESULT(reader.ReadBlobInto(
    std::as_writable_bytes(std::span { record.inertia_override })));
  CHECK_RESULT(reader.ReadInto(record.has_inertia_override));
  CHECK_RESULT(reader.ReadInto(record.max_linear_velocity));
  CHECK_RESULT(reader.ReadInto(record.max_angular_velocity));
  CHECK_RESULT(reader.ReadInto(record.allowed_dof_flags));
  CHECK_RESULT(reader.ReadInto(record.backend_scalars));
  return {};
}

inline auto Load(AnyReader& reader,
  data::pak::physics::ColliderBindingRecord& record) -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);
  CHECK_RESULT(reader.ReadInto(record.node_index));
  CHECK_RESULT(reader.ReadInto(record.shape_asset_key));
  CHECK_RESULT(reader.ReadInto(record.material_asset_key));
  CHECK_RESULT(reader.ReadInto(record.collision_layer));
  CHECK_RESULT(reader.ReadInto(record.collision_mask));
  CHECK_RESULT(reader.ReadInto(record.is_sensor));
  return {};
}

inline auto Load(AnyReader& reader,
  data::pak::physics::CharacterBindingRecord& record) -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);
  CHECK_RESULT(reader.ReadInto(record.node_index));
  CHECK_RESULT(reader.ReadInto(record.shape_asset_key));
  CHECK_RESULT(reader.ReadInto(record.mass));
  CHECK_RESULT(reader.ReadInto(record.max_slope_angle));
  CHECK_RESULT(reader.ReadInto(record.step_height));
  CHECK_RESULT(reader.ReadInto(record.step_down_distance));
  CHECK_RESULT(reader.ReadInto(record.max_strength));
  CHECK_RESULT(reader.ReadInto(record.skin_width));
  CHECK_RESULT(reader.ReadInto(record.predictive_contact_distance));
  CHECK_RESULT(reader.ReadInto(record.collision_layer));
  CHECK_RESULT(reader.ReadInto(record.collision_mask));
  CHECK_RESULT(reader.ReadInto(record.inner_shape_asset_key));
  CHECK_RESULT(reader.ReadInto(record.backend_scalars));
  return {};
}

inline auto Load(AnyReader& reader,
  data::pak::physics::SoftBodyBindingRecord& record) -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);
  CHECK_RESULT(reader.ReadInto(record.node_index));
  CHECK_RESULT(reader.ReadInto(record.edge_compliance));
  CHECK_RESULT(reader.ReadInto(record.shear_compliance));
  CHECK_RESULT(reader.ReadInto(record.bend_compliance));
  CHECK_RESULT(reader.ReadInto(record.volume_compliance));
  CHECK_RESULT(reader.ReadInto(record.pressure_coefficient));
  CHECK_RESULT(reader.ReadInto(record.global_damping));
  CHECK_RESULT(reader.ReadInto(record.restitution));
  CHECK_RESULT(reader.ReadInto(record.friction));
  CHECK_RESULT(reader.ReadInto(record.vertex_radius));
  CHECK_RESULT(reader.ReadInto(record.tether_max_distance_multiplier));
  CHECK_RESULT(reader.ReadInto(record.solver_iteration_count));
  CHECK_RESULT(reader.ReadInto(record.collision_layer));
  CHECK_RESULT(reader.ReadInto(record.collision_mask));
  CHECK_RESULT(reader.ReadInto(record.topology_asset_key));
  CHECK_RESULT(reader.ReadInto(record.pinned_vertex_count));
  CHECK_RESULT(reader.ReadInto(record.pinned_vertex_byte_offset));
  CHECK_RESULT(reader.ReadInto(record.kinematic_vertex_count));
  CHECK_RESULT(reader.ReadInto(record.kinematic_vertex_byte_offset));
  CHECK_RESULT(reader.ReadInto(record.backend_scalars));
  CHECK_RESULT(reader.ReadInto(record.tether_mode));
  CHECK_RESULT(reader.ReadInto(record.topology_format));
  CHECK_RESULT(reader.ReadInto(record.self_collision));
  return {};
}

inline auto Load(AnyReader& reader,
  data::pak::physics::JointBindingRecord& record) -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);
  CHECK_RESULT(reader.ReadInto(record.node_index_a));
  CHECK_RESULT(reader.ReadInto(record.node_index_b));
  CHECK_RESULT(reader.ReadInto(record.constraint_asset_key));
  CHECK_RESULT(reader.ReadInto(record.backend_scalars));
  return {};
}

inline auto Load(AnyReader& reader,
  data::pak::physics::VehicleBindingRecord& record) -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);
  CHECK_RESULT(reader.ReadInto(record.node_index));
  CHECK_RESULT(reader.ReadInto(record.constraint_asset_key));
  CHECK_RESULT(reader.ReadInto(record.controller_type));
  CHECK_RESULT(reader.ReadInto(record.wheel_slice_offset));
  CHECK_RESULT(reader.ReadInto(record.wheel_slice_count));
  return {};
}

inline auto Load(AnyReader& reader,
  data::pak::physics::AggregateBindingRecord& record) -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);
  CHECK_RESULT(reader.ReadInto(record.node_index));
  CHECK_RESULT(reader.ReadInto(record.max_bodies));
  CHECK_RESULT(reader.ReadInto(record.filter_overlap));
  CHECK_RESULT(reader.ReadInto(record.authority));
  return {};
}

inline auto Load(AnyReader& reader,
  data::pak::physics::VehicleWheelBindingRecord& record) -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);
  CHECK_RESULT(reader.ReadInto(record.vehicle_node_index));
  CHECK_RESULT(reader.ReadInto(record.wheel_node_index));
  CHECK_RESULT(reader.ReadInto(record.axle_index));
  CHECK_RESULT(reader.ReadInto(record.side));
  CHECK_RESULT(reader.ReadInto(record.backend_scalars));
  return {};
}

} // namespace oxygen::serio
