//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <span>

#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Serio/Reader.h>

namespace oxygen::serio {

//=== Scene: Nodes & Components (v2/v3) ===----------------------------------//

inline auto Load(AnyReader& reader, data::pak::NodeRecord& record)
  -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);

  CHECK_RESULT(reader.ReadBlobInto(
    std::as_writable_bytes(std::span { record.node_id.guid })));
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

inline auto Load(AnyReader& reader, data::pak::RenderableRecord& record)
  -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);

  CHECK_RESULT(reader.ReadInto(record.node_index));
  CHECK_RESULT(reader.ReadBlobInto(
    std::as_writable_bytes(std::span { record.geometry_key.guid })));
  CHECK_RESULT(reader.ReadInto(record.visible));
  CHECK_RESULT(
    reader.ReadBlobInto(std::as_writable_bytes(std::span { record.reserved })));

  return {};
}

inline auto Load(AnyReader& reader, data::pak::PerspectiveCameraRecord& record)
  -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);

  CHECK_RESULT(reader.ReadInto(record.node_index));
  CHECK_RESULT(reader.ReadInto(record.fov_y));
  CHECK_RESULT(reader.ReadInto(record.aspect_ratio));
  CHECK_RESULT(reader.ReadInto(record.near_plane));
  CHECK_RESULT(reader.ReadInto(record.far_plane));
  CHECK_RESULT(
    reader.ReadBlobInto(std::as_writable_bytes(std::span { record.reserved })));

  return {};
}

inline auto Load(AnyReader& reader, data::pak::OrthographicCameraRecord& record)
  -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);

  CHECK_RESULT(reader.ReadInto(record.node_index));
  CHECK_RESULT(reader.ReadInto(record.left));
  CHECK_RESULT(reader.ReadInto(record.right));
  CHECK_RESULT(reader.ReadInto(record.bottom));
  CHECK_RESULT(reader.ReadInto(record.top));
  CHECK_RESULT(reader.ReadInto(record.near_plane));
  CHECK_RESULT(reader.ReadInto(record.far_plane));
  CHECK_RESULT(
    reader.ReadBlobInto(std::as_writable_bytes(std::span { record.reserved })));

  return {};
}

//=== Scene: Environment (v3+) ===------------------------------------------//

inline auto Load(AnyReader& reader, data::pak::SceneEnvironmentBlockHeader& hdr)
  -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);
  CHECK_RESULT(reader.ReadInto(hdr.byte_size));
  CHECK_RESULT(reader.ReadInto(hdr.systems_count));
  CHECK_RESULT(
    reader.ReadBlobInto(std::as_writable_bytes(std::span { hdr.reserved })));
  return {};
}

inline auto Load(AnyReader& reader,
  data::pak::SceneEnvironmentSystemRecordHeader& hdr) -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);
  CHECK_RESULT(reader.ReadInto(hdr.system_type));
  CHECK_RESULT(reader.ReadInto(hdr.record_size));
  return {};
}

inline auto Load(AnyReader& reader,
  data::pak::SkyAtmosphereEnvironmentRecord& r) -> Result<void>
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
  CHECK_RESULT(reader.ReadInto(r.mie_scale_height_m));
  CHECK_RESULT(reader.ReadInto(r.mie_g));

  for (auto& v : r.absorption_rgb) {
    CHECK_RESULT(reader.ReadInto(v));
  }
  CHECK_RESULT(reader.ReadInto(r.absorption_scale_height_m));

  CHECK_RESULT(reader.ReadInto(r.multi_scattering_factor));
  CHECK_RESULT(reader.ReadInto(r.sun_disk_enabled));
  CHECK_RESULT(reader.ReadInto(r.sun_disk_angular_radius_radians));
  CHECK_RESULT(reader.ReadInto(r.aerial_perspective_distance_scale));
  CHECK_RESULT(
    reader.ReadBlobInto(std::as_writable_bytes(std::span { r._reserved })));

  return {};
}

inline auto Load(AnyReader& reader,
  data::pak::VolumetricCloudsEnvironmentRecord& r) -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);

  CHECK_RESULT(reader.ReadInto(r.header));
  CHECK_RESULT(reader.ReadInto(r.enabled));
  CHECK_RESULT(reader.ReadInto(r.base_altitude_m));
  CHECK_RESULT(reader.ReadInto(r.layer_thickness_m));
  CHECK_RESULT(reader.ReadInto(r.coverage));
  CHECK_RESULT(reader.ReadInto(r.density));

  for (auto& v : r.albedo_rgb) {
    CHECK_RESULT(reader.ReadInto(v));
  }
  CHECK_RESULT(reader.ReadInto(r.extinction_scale));
  CHECK_RESULT(reader.ReadInto(r.phase_g));

  for (auto& v : r.wind_dir_ws) {
    CHECK_RESULT(reader.ReadInto(v));
  }
  CHECK_RESULT(reader.ReadInto(r.wind_speed_mps));
  CHECK_RESULT(reader.ReadInto(r.shadow_strength));
  CHECK_RESULT(
    reader.ReadBlobInto(std::as_writable_bytes(std::span { r._reserved })));

  return {};
}

inline auto Load(AnyReader& reader, data::pak::FogEnvironmentRecord& r)
  -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);

  CHECK_RESULT(reader.ReadInto(r.header));
  CHECK_RESULT(reader.ReadInto(r.enabled));
  CHECK_RESULT(reader.ReadInto(r.model));
  CHECK_RESULT(reader.ReadInto(r.density));
  CHECK_RESULT(reader.ReadInto(r.height_falloff));
  CHECK_RESULT(reader.ReadInto(r.height_offset_m));
  CHECK_RESULT(reader.ReadInto(r.start_distance_m));
  CHECK_RESULT(reader.ReadInto(r.max_opacity));
  for (auto& v : r.albedo_rgb) {
    CHECK_RESULT(reader.ReadInto(v));
  }
  CHECK_RESULT(reader.ReadInto(r.anisotropy_g));
  CHECK_RESULT(reader.ReadInto(r.scattering_intensity));
  CHECK_RESULT(
    reader.ReadBlobInto(std::as_writable_bytes(std::span { r._reserved })));

  return {};
}

inline auto Load(AnyReader& reader, data::pak::SkyLightEnvironmentRecord& r)
  -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);

  CHECK_RESULT(reader.ReadInto(r.header));
  CHECK_RESULT(reader.ReadInto(r.enabled));
  CHECK_RESULT(reader.ReadInto(r.source));

  CHECK_RESULT(reader.ReadBlobInto(
    std::as_writable_bytes(std::span { r.cubemap_asset.guid })));

  CHECK_RESULT(reader.ReadInto(r.intensity));
  for (auto& v : r.tint_rgb) {
    CHECK_RESULT(reader.ReadInto(v));
  }
  CHECK_RESULT(reader.ReadInto(r.diffuse_intensity));
  CHECK_RESULT(reader.ReadInto(r.specular_intensity));
  CHECK_RESULT(
    reader.ReadBlobInto(std::as_writable_bytes(std::span { r._reserved })));

  return {};
}

inline auto Load(AnyReader& reader, data::pak::SkySphereEnvironmentRecord& r)
  -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);

  CHECK_RESULT(reader.ReadInto(r.header));
  CHECK_RESULT(reader.ReadInto(r.enabled));
  CHECK_RESULT(reader.ReadInto(r.source));

  CHECK_RESULT(reader.ReadBlobInto(
    std::as_writable_bytes(std::span { r.cubemap_asset.guid })));

  for (auto& v : r.solid_color_rgb) {
    CHECK_RESULT(reader.ReadInto(v));
  }
  CHECK_RESULT(reader.ReadInto(r.intensity));
  CHECK_RESULT(reader.ReadInto(r.rotation_radians));
  for (auto& v : r.tint_rgb) {
    CHECK_RESULT(reader.ReadInto(v));
  }
  CHECK_RESULT(
    reader.ReadBlobInto(std::as_writable_bytes(std::span { r._reserved })));

  return {};
}

inline auto Load(AnyReader& reader,
  data::pak::PostProcessVolumeEnvironmentRecord& r) -> Result<void>
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
  CHECK_RESULT(
    reader.ReadBlobInto(std::as_writable_bytes(std::span { r._reserved })));

  return {};
}

//=== Scene: Lights (v3+) ===------------------------------------------------//

inline auto Load(AnyReader& reader, data::pak::LightShadowSettingsRecord& r)
  -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);

  CHECK_RESULT(reader.ReadInto(r.bias));
  CHECK_RESULT(reader.ReadInto(r.normal_bias));
  CHECK_RESULT(reader.ReadInto(r.contact_shadows));
  CHECK_RESULT(reader.ReadInto(r.resolution_hint));
  CHECK_RESULT(
    reader.ReadBlobInto(std::as_writable_bytes(std::span { r.reserved })));

  return {};
}

inline auto Load(AnyReader& reader, data::pak::LightCommonRecord& r)
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
  CHECK_RESULT(
    reader.ReadBlobInto(std::as_writable_bytes(std::span { r.reserved0 })));

  CHECK_RESULT(reader.ReadInto(r.shadow));
  CHECK_RESULT(reader.ReadInto(r.exposure_compensation_ev));
  CHECK_RESULT(
    reader.ReadBlobInto(std::as_writable_bytes(std::span { r.reserved1 })));

  return {};
}

inline auto Load(AnyReader& reader, data::pak::DirectionalLightRecord& r)
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
  CHECK_RESULT(reader.ReadInto(r.intensity_lux));
  CHECK_RESULT(
    reader.ReadBlobInto(std::as_writable_bytes(std::span { r.reserved })));

  return {};
}

inline auto Load(AnyReader& reader, data::pak::PointLightRecord& r)
  -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);

  CHECK_RESULT(reader.ReadInto(r.node_index));
  CHECK_RESULT(reader.ReadInto(r.common));
  CHECK_RESULT(reader.ReadInto(r.range));
  CHECK_RESULT(reader.ReadInto(r.attenuation_model));
  CHECK_RESULT(
    reader.ReadBlobInto(std::as_writable_bytes(std::span { r.reserved0 })));
  CHECK_RESULT(reader.ReadInto(r.decay_exponent));
  CHECK_RESULT(reader.ReadInto(r.source_radius));
  CHECK_RESULT(reader.ReadInto(r.luminous_flux_lm));
  CHECK_RESULT(
    reader.ReadBlobInto(std::as_writable_bytes(std::span { r.reserved1 })));

  return {};
}

inline auto Load(AnyReader& reader, data::pak::SpotLightRecord& r)
  -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);

  CHECK_RESULT(reader.ReadInto(r.node_index));
  CHECK_RESULT(reader.ReadInto(r.common));
  CHECK_RESULT(reader.ReadInto(r.range));
  CHECK_RESULT(reader.ReadInto(r.attenuation_model));
  CHECK_RESULT(
    reader.ReadBlobInto(std::as_writable_bytes(std::span { r.reserved0 })));
  CHECK_RESULT(reader.ReadInto(r.decay_exponent));
  CHECK_RESULT(reader.ReadInto(r.inner_cone_angle_radians));
  CHECK_RESULT(reader.ReadInto(r.outer_cone_angle_radians));
  CHECK_RESULT(reader.ReadInto(r.source_radius));
  CHECK_RESULT(reader.ReadInto(r.luminous_flux_lm));
  CHECK_RESULT(
    reader.ReadBlobInto(std::as_writable_bytes(std::span { r.reserved1 })));

  return {};
}

} // namespace oxygen::serio
