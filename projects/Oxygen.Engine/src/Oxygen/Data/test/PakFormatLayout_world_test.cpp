//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>

#include "PakFormatWireBytes.h"

#include <Oxygen/Core/Types/CameraAspectMode.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/PakFormat.h>
// The camera and post-process `Store` overloads `Encoded` finds by ADL.
#include <Oxygen/Data/PakFormatSerioWriters.h> // IWYU pragma: keep
#include <Oxygen/Data/SceneIds.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Testing/GTest.h>

namespace {

namespace engine = oxygen::engine;
namespace schema = oxygen::data::schema;
namespace world = oxygen::data::pak::world;

using oxygen::CameraAspectMode;
using oxygen::data::NodeId;
using oxygen::data::ResourceReferenceIndex;

using oxygen::data::pak::testing::Digest;
using oxygen::data::pak::testing::Encoded;
using oxygen::data::pak::testing::HeaderBytes;
using oxygen::data::pak::testing::IdBytes;
using oxygen::data::pak::testing::Key;
using oxygen::data::pak::testing::MakeHeader;
using oxygen::data::pak::testing::RawBytes;
using oxygen::data::pak::testing::Surface;
using oxygen::data::pak::testing::WireBytes;
using oxygen::data::pak::testing::ZeroedRecord;

//! A node id whose bytes count up from `first`.
auto Node(const uint8_t first) -> NodeId
{
  return NodeId::FromBytes(IdBytes(first));
}

//! A light's common block with distinct values.
auto MakeCommon() -> world::LightCommonRecord
{
  return world::LightCommonRecord {
    .affects_world = 0x11121314U,
    .color_rgb = { 1.5F, 2.5F, 3.5F, },
    .casts_shadows = 0x21U,
    .shadow = { .bias = 4.5F,
      .normal_bias = 5.5F,
      .contact_shadows = 0x31323334U,
      .resolution_hint = 0x41U, },
    .exposure_compensation_ev = 6.5F,
  };
}

//! The documented bytes of the block `MakeCommon` returns: affects-world flag,
//! color, shadow casting, shadow settings (bias, normal bias, contact shadows,
//! resolution hint) and exposure compensation.
auto CommonBytes() -> WireBytes
{
  auto bytes = WireBytes {};
  bytes.U32(0x11121314U)
    .Floats(1.5F, 3)
    .U8(0x21U)
    .F32(4.5F)
    .F32(5.5F)
    .U32(0x31323334U)
    .U8(0x41U)
    .F32(6.5F);
  return bytes;
}

//! A node reference is its node index and node path.
NOLINT_TEST(PakFormatLayoutWorldTest, NodeRefWireBytes)
{
  const auto ref = world::NodeRef {
    .node_index = 0x11121314U,
    .node_path = 0x21222324U,
  };

  const auto expected = WireBytes {}.U32(0x11121314U).U32(0x21222324U);

  EXPECT_EQ(Encoded(ref), expected.Bytes());
}

//! A node path is its first element index and element count.
NOLINT_TEST(PakFormatLayoutWorldTest, NodePathRecordWireBytes)
{
  const auto path = world::NodePathRecord {
    .first = 0x11121314U,
    .count = 0x21222324U,
  };

  const auto expected = WireBytes {}.U32(0x11121314U).U32(0x21222324U);

  EXPECT_EQ(Encoded(path), expected.Bytes());
}

//! A node path element is a node id.
NOLINT_TEST(PakFormatLayoutWorldTest, NodePathElementWireBytes)
{
  const auto element = world::NodePathElement {
    .node_id = Node(0x10U),
  };

  const auto expected = WireBytes {}.Run(0x10U, 16);

  EXPECT_EQ(Encoded(element), expected.Bytes());
}

//! A scene data table is its offset, entry count and entry size.
NOLINT_TEST(PakFormatLayoutWorldTest, SceneDataTableWireBytes)
{
  const auto table = world::SceneDataTable {
    .offset = 0x0102030405060708U,
    .count = 0x11121314U,
    .entry_size = 0x21222324U,
  };

  const auto expected
    = WireBytes {}.U64(0x0102030405060708U).U32(0x11121314U).U32(0x21222324U);

  EXPECT_EQ(Encoded(table), expected.Bytes());
}

//! A scene is its header, node table, component table directory offset and
//! count, script slot table, flags and starting camera reference.
NOLINT_TEST(PakFormatLayoutWorldTest, SceneAssetDescWireBytes)
{
  const auto scene = world::SceneAssetDesc {
    .header = MakeHeader(),
    .nodes = { .offset = 0x0102030405060708U,
      .count = 0x81828384U,
      .entry_size = 0x91929394U, },
    .component_table_directory_offset = 0x1112131415161718U,
    .component_table_count = 0xA1A2A3A4U,
    .script_slots = { .offset = 0x2122232425262728U,
      .count = 0xB1B2B3B4U,
      .entry_size = 0xC1C2C3C4U, },
    .flags = world::SceneAssetFlags::kModelScene,
    .starting_camera = { .node_index = 0xD1D2D3D4U,
      .node_path = 0xE1E2E3E4U, },
  };

  auto expected = WireBytes {};
  expected.Append(HeaderBytes())
    .U64(0x0102030405060708U)
    .U32(0x81828384U)
    .U32(0x91929394U)
    .U64(0x1112131415161718U)
    .U32(0xA1A2A3A4U)
    .U64(0x2122232425262728U)
    .U32(0xB1B2B3B4U)
    .U32(0xC1C2C3C4U)
    .U32(1U)
    .U32(0xD1D2D3D4U)
    .U32(0xE1E2E3E4U);

  EXPECT_EQ(Encoded(scene), expected.Bytes());
}

//! A component table descriptor is its component type and data table.
NOLINT_TEST(PakFormatLayoutWorldTest, SceneComponentTableDescWireBytes)
{
  const auto desc = world::SceneComponentTableDesc {
    .component_type = 0x11121314U,
    .table = { .offset = 0x0102030405060708U,
      .count = 0x21222324U,
      .entry_size = 0x31323334U, },
  };

  const auto expected = WireBytes {}
                          .U32(0x11121314U)
                          .U64(0x0102030405060708U)
                          .U32(0x21222324U)
                          .U32(0x31323334U);

  EXPECT_EQ(Encoded(desc), expected.Bytes());
}

//! A node is its id, name offset, parent index, node flags, inherited flags,
//! translation, rotation quaternion and scale.
NOLINT_TEST(PakFormatLayoutWorldTest, NodeRecordWireBytes)
{
  const auto node = world::NodeRecord {
    .node_id = Node(0x10U),
    .scene_name_offset = 0x21222324U,
    .parent_index = 0x31323334U,
    .node_flags = 0x41424344U,
    .inherited_flags = 0x51525354U,
    .translation = { 1.5F, 2.5F, 3.5F, },
    .rotation = { 4.5F, 5.5F, 6.5F, 7.5F, },
    .scale = { 8.5F, 9.5F, 10.5F, },
  };

  const auto expected = WireBytes {}
                          .Run(0x10U, 16)
                          .U32(0x21222324U)
                          .U32(0x31323334U)
                          .U32(0x41424344U)
                          .U32(0x51525354U)
                          .Floats(1.5F, 3)
                          .Floats(4.5F, 4)
                          .Floats(8.5F, 3);

  EXPECT_EQ(Encoded(node), expected.Bytes());
}

//! A renderable is its node index and geometry key.
NOLINT_TEST(PakFormatLayoutWorldTest, RenderableRecordWireBytes)
{
  const auto renderable = world::RenderableRecord {
    .node_index = 0x11121314U,
    .geometry_key = Key(0x20U),
  };

  const auto expected = WireBytes {}.U32(0x11121314U).Run(0x20U, 16);

  EXPECT_EQ(Encoded(renderable), expected.Bytes());
}

//! A scene instance is its node index and scene key.
NOLINT_TEST(PakFormatLayoutWorldTest, SceneInstanceRecordWireBytes)
{
  const auto instance = world::SceneInstanceRecord {
    .node_index = 0x11121314U,
    .scene_key = Key(0x20U),
  };

  const auto expected = WireBytes {}.U32(0x11121314U).Run(0x20U, 16);

  EXPECT_EQ(Encoded(instance), expected.Bytes());
}

//! A starting input context is its context key and priority.
NOLINT_TEST(PakFormatLayoutWorldTest, StartingInputContextRecordWireBytes)
{
  const auto context = world::StartingInputContextRecord {
    .context_key = Key(0x10U),
    .priority = 0x21222324,
  };

  const auto expected = WireBytes {}.Run(0x10U, 16).U32(0x21222324U);

  EXPECT_EQ(Encoded(context), expected.Bytes());
}

//! A zeroed override with distinct target, property, part and layout revision;
//! each test sets the value arm.
auto MakeOverride() -> world::OverrideRecord
{
  auto record = ZeroedRecord<world::OverrideRecord>();
  record.target = world::NodeRef {
    .node_index = 0x11121314U,
    .node_path = 0x21222324U,
  };
  record.override_id = schema::OverrideId::kGeometrySurfaceCastsShadows;
  record.part_id = Surface(0x30U);
  record.layout_revision = Digest(0x40U);
  return record;
}

//! The documented start of an override: target, property id 3, part id and
//! layout revision of the record `MakeOverride` returns.
auto OverridePrefix() -> WireBytes
{
  auto bytes = WireBytes {};
  bytes.U32(0x11121314U).U32(0x21222324U).U16(3U).Run(0x30U, 16).Run(0x40U, 32);
  return bytes;
}

//! An override with a bool value is its target, property, part, layout
//! revision and the bool arm, zero-padded to the 16 bytes of the value.
NOLINT_TEST(PakFormatLayoutWorldTest, OverrideRecordBoolArmWireBytes)
{
  auto record = MakeOverride();
  record.value.as_bool = 1U;

  auto expected = OverridePrefix();
  expected.U8(1U).Zeros(15);

  EXPECT_EQ(Encoded(record), expected.Bytes());
}

//! An override with an int32 value is its target, property, part, layout
//! revision and the int32 arm, zero-padded to 16 bytes.
NOLINT_TEST(PakFormatLayoutWorldTest, OverrideRecordInt32ArmWireBytes)
{
  auto record = MakeOverride();
  record.value.as_int32 = 0x51525354;

  auto expected = OverridePrefix();
  expected.U32(0x51525354U).Zeros(12);

  EXPECT_EQ(Encoded(record), expected.Bytes());
}

//! An override with a float value is its target, property, part, layout
//! revision and the float arm, zero-padded to 16 bytes.
NOLINT_TEST(PakFormatLayoutWorldTest, OverrideRecordFloatArmWireBytes)
{
  auto record = MakeOverride();
  record.value.as_float = 1.5F;

  auto expected = OverridePrefix();
  expected.F32(1.5F).Zeros(12);

  EXPECT_EQ(Encoded(record), expected.Bytes());
}

//! An override with a vec3 value is its target, property, part, layout
//! revision and the three-float arm, zero-padded to 16 bytes.
NOLINT_TEST(PakFormatLayoutWorldTest, OverrideRecordVec3ArmWireBytes)
{
  auto record = MakeOverride();
  record.value.as_vec3[0] = 1.5F;
  record.value.as_vec3[1] = 2.5F;
  record.value.as_vec3[2] = 3.5F;

  auto expected = OverridePrefix();
  expected.Floats(1.5F, 3).Zeros(4);

  EXPECT_EQ(Encoded(record), expected.Bytes());
}

//! An override with a quaternion value is its target, property, part, layout
//! revision and the four-float arm.
NOLINT_TEST(PakFormatLayoutWorldTest, OverrideRecordQuaternionArmWireBytes)
{
  auto record = MakeOverride();
  record.value.as_quat[0] = 1.5F;
  record.value.as_quat[1] = 2.5F;
  record.value.as_quat[2] = 3.5F;
  record.value.as_quat[3] = 4.5F;

  auto expected = OverridePrefix();
  expected.Floats(1.5F, 4);

  EXPECT_EQ(Encoded(record), expected.Bytes());
}

//! An override with an asset value is its target, property, part, layout
//! revision and the asset key arm.
NOLINT_TEST(PakFormatLayoutWorldTest, OverrideRecordAssetArmWireBytes)
{
  auto record = MakeOverride();
  record.value.as_asset = Key(0x70U);

  auto expected = OverridePrefix();
  expected.Run(0x70U, 16);

  EXPECT_EQ(Encoded(record), expected.Bytes());
}

//! An override with a string value is its target, property, part, layout
//! revision and the string offset arm, zero-padded to 16 bytes.
NOLINT_TEST(PakFormatLayoutWorldTest, OverrideRecordStringArmWireBytes)
{
  auto record = MakeOverride();
  record.value.as_string_offset = 0x61626364U;

  auto expected = OverridePrefix();
  expected.U32(0x61626364U).Zeros(12);

  EXPECT_EQ(Encoded(record), expected.Bytes());
}

//! An override with a record index value is its target, property, part, layout
//! revision and the record index arm, zero-padded to 16 bytes.
NOLINT_TEST(PakFormatLayoutWorldTest, OverrideRecordRecordIndexArmWireBytes)
{
  auto record = MakeOverride();
  record.value.as_record_index = world::OverrideRecordIndex { 0x71727374U };

  auto expected = OverridePrefix();
  expected.U32(0x71727374U).Zeros(12);

  EXPECT_EQ(Encoded(record), expected.Bytes());
}

//! A local fog volume is its node index, enable flag, radial and height fog
//! terms, phase, albedo, emissive color and sort priority. It is not a Serio
//! record, so its in-memory packed bytes are compared.
NOLINT_TEST(PakFormatLayoutWorldTest, LocalFogVolumeRecordWireBytes)
{
  const auto fog = world::LocalFogVolumeRecord {
    .node_index = 0x11121314U,
    .enabled = 0x21222324U,
    .radial_fog_extinction = 1.5F,
    .height_fog_extinction = 2.5F,
    .height_fog_falloff = 3.5F,
    .height_fog_offset = 4.5F,
    .fog_phase_g = 5.5F,
    .fog_albedo = { 6.5F, 7.5F, 8.5F, },
    .fog_emissive = { 9.5F, 10.5F, 11.5F, },
    .sort_priority = 0x31323334,
  };

  const auto expected = WireBytes {}
                          .U32(0x11121314U)
                          .U32(0x21222324U)
                          .Floats(1.5F, 5)
                          .Floats(6.5F, 3)
                          .Floats(9.5F, 3)
                          .U32(0x31323334U);

  EXPECT_EQ(RawBytes(fog), expected.Bytes());
}

//! A perspective camera is its node index, vertical field of view, aspect
//! ratio, clip planes, aperture, shutter rate, ISO and aspect mode. The
//! values pass the camera's `Store` validation.
NOLINT_TEST(PakFormatLayoutWorldTest, PerspectiveCameraRecordWireBytes)
{
  const auto camera = world::PerspectiveCameraRecord {
    .node_index = 0x11121314U,
    .fov_y = 1.0F,
    .aspect_ratio = 2.0F,
    .near_plane = 3.0F,
    .far_plane = 4.0F,
    .aperture_f = 5.0F,
    .shutter_rate = 6.0F,
    .iso = 7.0F,
    .aspect_mode = CameraAspectMode::kFixed,
  };

  const auto expected = WireBytes {}.U32(0x11121314U).Floats(1.0F, 7).U8(1U);

  EXPECT_EQ(Encoded(camera), expected.Bytes());
}

//! An orthographic camera is its node index, box extents, clip planes,
//! aperture, shutter rate, ISO and aspect mode. The values pass the camera's
//! `Store` validation.
NOLINT_TEST(PakFormatLayoutWorldTest, OrthographicCameraRecordWireBytes)
{
  const auto camera = world::OrthographicCameraRecord {
    .node_index = 0x11121314U,
    .left = -2.5F,
    .right = 3.5F,
    .bottom = -1.5F,
    .top = 4.5F,
    .near_plane = 5.5F,
    .far_plane = 6.5F,
    .aperture_f = 7.5F,
    .shutter_rate = 8.5F,
    .iso = 9.5F,
    .aspect_mode = CameraAspectMode::kFixed,
  };

  const auto expected = WireBytes {}
                          .U32(0x11121314U)
                          .F32(-2.5F)
                          .F32(3.5F)
                          .F32(-1.5F)
                          .F32(4.5F)
                          .Floats(5.5F, 5)
                          .U8(1U);

  EXPECT_EQ(Encoded(camera), expected.Bytes());
}

//! An environment block header is its byte size and system count.
NOLINT_TEST(PakFormatLayoutWorldTest, SceneEnvironmentBlockHeaderWireBytes)
{
  const auto header = world::SceneEnvironmentBlockHeader {
    .byte_size = 0x11121314U,
    .systems_count = 0x21222324U,
  };

  const auto expected = WireBytes {}.U32(0x11121314U).U32(0x21222324U);

  EXPECT_EQ(Encoded(header), expected.Bytes());
}

//! An environment system record header is its system type and record size.
NOLINT_TEST(
  PakFormatLayoutWorldTest, SceneEnvironmentSystemRecordHeaderWireBytes)
{
  const auto header = world::SceneEnvironmentSystemRecordHeader {
    .system_type = 0x11121314U,
    .record_size = 0x21222324U,
  };

  const auto expected = WireBytes {}.U32(0x11121314U).U32(0x21222324U);

  EXPECT_EQ(Encoded(header), expected.Bytes());
}

//! A sky atmosphere is its system header, enable flag, thirty-six physical
//! parameters (planet, scattering, absorption and aerial perspective terms) and
//! three trailing flags.
NOLINT_TEST(PakFormatLayoutWorldTest, SkyAtmosphereEnvironmentRecordWireBytes)
{
  const auto sky = world::SkyAtmosphereEnvironmentRecord {
    .header = { .system_type = 0x11121314U, .record_size = 0x21222324U, },
    .enabled = 0x31323334U,
    .planet_radius_m = 1.5F,
    .atmosphere_height_m = 2.5F,
    .ground_albedo_rgb = { 3.5F, 4.5F, 5.5F, },
    .rayleigh_scattering_rgb = { 6.5F, 7.5F, 8.5F, },
    .rayleigh_scale_height_m = 9.5F,
    .mie_scattering_rgb = { 10.5F, 11.5F, 12.5F, },
    .mie_absorption_rgb = { 13.5F, 14.5F, 15.5F, },
    .mie_scale_height_m = 16.5F,
    .mie_g = 17.5F,
    .absorption_rgb = { 18.5F, 19.5F, 20.5F, },
    .ozone_density_profile = { 21.5F, 22.5F, 23.5F, },
    .multi_scattering_factor = 24.5F,
    .sky_luminance_factor_rgb = { 25.5F, 26.5F, 27.5F, },
    .sky_and_aerial_perspective_luminance_factor_rgb
    = { 28.5F, 29.5F, 30.5F, },
    .aerial_perspective_distance_scale = 31.5F,
    .aerial_scattering_strength = 32.5F,
    .aerial_perspective_start_depth_m = 33.5F,
    .height_fog_contribution = 34.5F,
    .trace_sample_count_scale = 35.5F,
    .transmittance_min_light_elevation_deg = 36.5F,
    .sun_disk_enabled = 0x41424344U,
    .holdout = 0x51525354U,
    .render_in_main_pass = 0x61626364U,
  };

  const auto expected = WireBytes {}
                          .U32(0x11121314U)
                          .U32(0x21222324U)
                          .U32(0x31323334U)
                          .Floats(1.5F, 36)
                          .U32(0x41424344U)
                          .U32(0x51525354U)
                          .U32(0x61626364U);

  EXPECT_EQ(Encoded(sky), expected.Bytes());
}

//! A volumetric clouds record is its system header, enable flag, layer
//! altitude, thickness, coverage, extinction, albedo, phase, wind direction,
//! wind speed and shadow strength.
NOLINT_TEST(
  PakFormatLayoutWorldTest, VolumetricCloudsEnvironmentRecordWireBytes)
{
  const auto clouds = world::VolumetricCloudsEnvironmentRecord {
    .header = { .system_type = 0x11121314U, .record_size = 0x21222324U, },
    .enabled = 0x31323334U,
    .base_altitude_m = 1.5F,
    .layer_thickness_m = 2.5F,
    .coverage = 3.5F,
    .extinction_sigma_t_per_m = 4.5F,
    .single_scattering_albedo_rgb = { 5.5F, 6.5F, 7.5F, },
    .phase_g = 8.5F,
    .wind_dir_ws = { 9.5F, 10.5F, 11.5F, },
    .wind_speed_mps = 12.5F,
    .shadow_strength = 13.5F,
  };

  const auto expected = WireBytes {}
                          .U32(0x11121314U)
                          .U32(0x21222324U)
                          .U32(0x31323334U)
                          .Floats(1.5F, 13);

  EXPECT_EQ(Encoded(clouds), expected.Bytes());
}

//! A fog record is its system header, enable flag, model, height fog terms,
//! scattering, second fog, inscattering colors and cubemap, directional
//! inscattering, distances, volumetric fog terms and five trailing flags.
NOLINT_TEST(PakFormatLayoutWorldTest, FogEnvironmentRecordWireBytes)
{
  const auto fog = world::FogEnvironmentRecord {
    .header = { .system_type = 0x11121314U, .record_size = 0x21222324U, },
    .enabled = 0x31323334U,
    .model = 0x41424344U,
    .extinction_sigma_t_per_m = 1.5F,
    .height_falloff_per_m = 2.5F,
    .height_offset_m = 3.5F,
    .start_distance_m = 4.5F,
    .max_opacity = 5.5F,
    .single_scattering_albedo_rgb = { 6.5F, 7.5F, 8.5F, },
    .anisotropy_g = 9.5F,
    .enable_height_fog = 0x51525354U,
    .enable_volumetric_fog = 0x61626364U,
    .second_fog_density = 10.5F,
    .second_fog_height_falloff = 11.5F,
    .second_fog_height_offset = 12.5F,
    .fog_inscattering_luminance = { 13.5F, 14.5F, 15.5F, },
    .sky_atmosphere_ambient_contribution_color_scale = { 16.5F, 17.5F, 18.5F, },
    .inscattering_color_cubemap = ResourceReferenceIndex { 0x71727374U },
    .inscattering_color_cubemap_angle = 19.5F,
    .inscattering_texture_tint = { 20.5F, 21.5F, 22.5F, },
    .fully_directional_inscattering_color_distance = 23.5F,
    .non_directional_inscattering_color_distance = 24.5F,
    .directional_inscattering_luminance = { 25.5F, 26.5F, 27.5F, },
    .directional_inscattering_exponent = 28.5F,
    .directional_inscattering_start_distance = 29.5F,
    .end_distance_m = 30.5F,
    .fog_cutoff_distance_m = 31.5F,
    .volumetric_fog_scattering_distribution = 32.5F,
    .volumetric_fog_albedo = { 33.5F, 34.5F, 35.5F, },
    .volumetric_fog_emissive = { 36.5F, 37.5F, 38.5F, },
    .volumetric_fog_extinction_scale = 39.5F,
    .volumetric_fog_distance = 40.5F,
    .volumetric_fog_start_distance = 41.5F,
    .volumetric_fog_near_fade_in_distance = 42.5F,
    .volumetric_fog_static_lighting_scattering_intensity = 43.5F,
    .override_light_colors_with_fog_inscattering_colors = 0x81828384U,
    .holdout = 0x91929394U,
    .render_in_main_pass = 0xA1A2A3A4U,
    .visible_in_reflection_captures = 0xB1B2B3B4U,
    .visible_in_real_time_sky_captures = 0xC1C2C3C4U,
  };

  const auto expected = WireBytes {}
                          .U32(0x11121314U)
                          .U32(0x21222324U)
                          .U32(0x31323334U)
                          .U32(0x41424344U)
                          .Floats(1.5F, 5)
                          .Floats(6.5F, 3)
                          .F32(9.5F)
                          .U32(0x51525354U)
                          .U32(0x61626364U)
                          .Floats(10.5F, 3)
                          .Floats(13.5F, 3)
                          .Floats(16.5F, 3)
                          .U32(0x71727374U)
                          .F32(19.5F)
                          .Floats(20.5F, 3)
                          .F32(23.5F)
                          .F32(24.5F)
                          .Floats(25.5F, 3)
                          .Floats(28.5F, 4)
                          .Floats(32.5F, 1)
                          .Floats(33.5F, 3)
                          .Floats(36.5F, 3)
                          .Floats(39.5F, 5)
                          .U32(0x81828384U)
                          .U32(0x91929394U)
                          .U32(0xA1A2A3A4U)
                          .U32(0xB1B2B3B4U)
                          .U32(0xC1C2C3C4U);

  EXPECT_EQ(Encoded(fog), expected.Bytes());
}

//! A sky light is its system header, enable flag, source, cubemap, illuminance,
//! intensity, tint, diffuse and specular intensity, lower hemisphere color,
//! volumetric scattering, reflections flag, cubemap angle, solid-color flag
//! and blend alpha.
NOLINT_TEST(PakFormatLayoutWorldTest, SkyLightEnvironmentRecordWireBytes)
{
  const auto light = world::SkyLightEnvironmentRecord {
    .header = { .system_type = 0x11121314U, .record_size = 0x21222324U, },
    .enabled = 0x31323334U,
    // A pattern no catalog value has, so the field's byte order shows.
    .source = static_cast<world::SkyLightSource>(0x41424344U),
    .cubemap = ResourceReferenceIndex { 0x51525354U },
    .illuminance_lux = 1.5F,
    .intensity = 2.5F,
    .tint_rgb = { 3.5F, 4.5F, 5.5F, },
    .diffuse_intensity = 6.5F,
    .specular_intensity = 7.5F,
    .lower_hemisphere_color = { 8.5F, 9.5F, 10.5F, },
    .volumetric_scattering_intensity = 11.5F,
    .affect_reflections = 0x61626364U,
    .source_cubemap_angle_radians = 12.5F,
    .lower_hemisphere_is_solid_color = 0x71727374U,
    .lower_hemisphere_blend_alpha = 13.5F,
  };

  const auto expected = WireBytes {}
                          .U32(0x11121314U)
                          .U32(0x21222324U)
                          .U32(0x31323334U)
                          .U32(0x41424344U)
                          .U32(0x51525354U)
                          .F32(1.5F)
                          .Floats(2.5F, 10)
                          .U32(0x61626364U)
                          .F32(12.5F)
                          .U32(0x71727374U)
                          .F32(13.5F);

  EXPECT_EQ(Encoded(light), expected.Bytes());
}

//! A sky sphere is its system header, enable flag, source, cubemap,
//! illuminance, solid color, intensity, rotation and tint.
NOLINT_TEST(PakFormatLayoutWorldTest, SkySphereEnvironmentRecordWireBytes)
{
  const auto sphere = world::SkySphereEnvironmentRecord {
    .header = { .system_type = 0x11121314U, .record_size = 0x21222324U, },
    .enabled = 0x31323334U,
    // A pattern no catalog value has, so the field's byte order shows.
    .source = static_cast<world::SkySphereSource>(0x41424344U),
    .cubemap = ResourceReferenceIndex { 0x51525354U },
    .illuminance_lux = 1.5F,
    .solid_color_rgb = { 2.5F, 3.5F, 4.5F, },
    .intensity = 5.5F,
    .rotation_radians = 6.5F,
    .tint_rgb = { 7.5F, 8.5F, 9.5F, },
  };

  const auto expected = WireBytes {}
                          .U32(0x11121314U)
                          .U32(0x21222324U)
                          .U32(0x31323334U)
                          .U32(0x41424344U)
                          .U32(0x51525354U)
                          .F32(1.5F)
                          .Floats(2.5F, 8);

  EXPECT_EQ(Encoded(sphere), expected.Bytes());
}

//! An exposure compensation key is its metered and compensation exposure
//! values.
NOLINT_TEST(PakFormatLayoutWorldTest, ExposureCompensationKeyRecordWireBytes)
{
  const auto key = world::ExposureCompensationKeyRecord {
    .metered_ev = 1.5F,
    .compensation_ev = 2.5F,
  };

  const auto expected = WireBytes {}.Floats(1.5F, 2);

  EXPECT_EQ(Encoded(key), expected.Bytes());
}

//! A post process volume is its system header, enable flag, tone mapper,
//! exposure mode and settings, bloom, color grading, auto exposure metering,
//! exposure extension, metering mask and curve key count. The values pass the
//! record's `Store` validation, which writes this 124-byte prefix only; the
//! record size counts the two curve keys that follow it.
NOLINT_TEST(
  PakFormatLayoutWorldTest, PostProcessVolumeEnvironmentRecordWireBytes)
{
  const auto volume = world::PostProcessVolumeEnvironmentRecord {
    .header = { .system_type = 5U, .record_size = 140U, },
    .enabled = 1U,
    .tone_mapper = engine::ToneMapper::kFilmic,
    .exposure_mode = engine::ExposureMode::kManualCamera,
    .exposure_compensation_ev = 1.5F,
    .auto_exposure_min_ev = 2.5F,
    .auto_exposure_max_ev = 3.5F,
    .auto_exposure_speed_up = 4.5F,
    .auto_exposure_speed_down = 5.5F,
    .bloom_intensity = 6.5F,
    .bloom_threshold = 7.5F,
    .saturation = 8.5F,
    .contrast = 9.5F,
    .vignette_intensity = 0.75F,
    .exposure_enabled = 0U,
    .exposure_key = 10.5F,
    .manual_exposure_ev = 11.5F,
    .auto_exposure_metering_mode = engine::MeteringMode::kSpot,
    .auto_exposure_low_percentile = 0.125F,
    .auto_exposure_high_percentile = 0.875F,
    .auto_exposure_min_log_luminance = -12.5F,
    .auto_exposure_log_luminance_range = 20.5F,
    .auto_exposure_target_luminance = 0.25F,
    .auto_exposure_spot_meter_radius = 0.5F,
    .display_gamma = 13.5F,
    .exposure_extension_version = world::kExposureExtensionVersion,
    .auto_exposure_black_influence = 0.625F,
    .auto_exposure_transition_distance_ev = 14.5F,
    .auto_exposure_metering_mask = ResourceReferenceIndex { 0x41424344U },
    .curve_key_count = 2U,
  };

  const auto expected = WireBytes {}
                          .U32(5U)
                          .U32(140U)
                          .U32(1U)
                          .U32(2U)
                          .U32(1U)
                          .Floats(1.5F, 9)
                          .F32(0.75F)
                          .U32(0U)
                          .F32(10.5F)
                          .F32(11.5F)
                          .U32(2U)
                          .F32(0.125F)
                          .F32(0.875F)
                          .F32(-12.5F)
                          .F32(20.5F)
                          .F32(0.25F)
                          .F32(0.5F)
                          .F32(13.5F)
                          .U32(world::kExposureExtensionVersion)
                          .F32(0.625F)
                          .F32(14.5F)
                          .U32(0x41424344U)
                          .U32(2U);

  EXPECT_EQ(Encoded(volume), expected.Bytes());
}

//! A background is its system header, enable flag and color.
NOLINT_TEST(PakFormatLayoutWorldTest, BackgroundEnvironmentRecordWireBytes)
{
  const auto background = world::BackgroundEnvironmentRecord {
    .header = { .system_type = 0x11121314U, .record_size = 0x21222324U, },
    .enabled = 0x31323334U,
    .color_rgb = { 1.5F, 2.5F, 3.5F, },
  };

  const auto expected = WireBytes {}
                          .U32(0x11121314U)
                          .U32(0x21222324U)
                          .U32(0x31323334U)
                          .Floats(1.5F, 3);

  EXPECT_EQ(Encoded(background), expected.Bytes());
}

//! A light's shadow settings are its bias, normal bias, contact shadows flag
//! and resolution hint.
NOLINT_TEST(PakFormatLayoutWorldTest, LightShadowSettingsRecordWireBytes)
{
  const auto shadow = world::LightShadowSettingsRecord {
    .bias = 1.5F,
    .normal_bias = 2.5F,
    .contact_shadows = 0x11121314U,
    .resolution_hint = 0x21U,
  };

  const auto expected
    = WireBytes {}.F32(1.5F).F32(2.5F).U32(0x11121314U).U8(0x21U);

  EXPECT_EQ(Encoded(shadow), expected.Bytes());
}

//! A light's common block is its affects-world flag, color, shadow casting,
//! shadow settings and exposure compensation.
NOLINT_TEST(PakFormatLayoutWorldTest, LightCommonRecordWireBytes)
{
  const auto common = MakeCommon();

  EXPECT_EQ(Encoded(common), CommonBytes().Bytes());
}

//! A directional light is its node index, common block, angular size,
//! atmosphere light slot, per-pixel transmittance flag, disk luminance scale,
//! cascade count and distances, distribution exponent, split mode, shadow
//! distance terms and intensity. It is not a Serio record, so its in-memory
//! packed bytes are compared.
NOLINT_TEST(PakFormatLayoutWorldTest, DirectionalLightRecordWireBytes)
{
  const auto light = world::DirectionalLightRecord {
    .node_index = 0x51525354U,
    .common = MakeCommon(),
    .angular_size_radians = 7.5F,
    .atmosphere_light_slot = 0x61U,
    .use_per_pixel_atmosphere_transmittance = 0x71U,
    .atmosphere_disk_luminance_scale_rgb = { 8.5F, 9.5F, 10.5F, },
    .cascade_count = 0x81828384U,
    .cascade_distances = { 11.5F, 12.5F, 13.5F, 14.5F, },
    .distribution_exponent = 15.5F,
    .split_mode = 0x91U,
    .max_shadow_distance = 16.5F,
    .transition_fraction = 17.5F,
    .distance_fadeout_fraction = 18.5F,
    .intensity_lux = 19.5F,
  };

  auto expected = WireBytes {};
  expected.U32(0x51525354U)
    .Append(CommonBytes())
    .F32(7.5F)
    .U8(0x61U)
    .U8(0x71U)
    .Floats(8.5F, 3)
    .U32(0x81828384U)
    .Floats(11.5F, 4)
    .F32(15.5F)
    .U8(0x91U)
    .Floats(16.5F, 4);

  EXPECT_EQ(RawBytes(light), expected.Bytes());
}

//! A point light is its node index, common block, range, source radius and
//! luminous flux. It is not a Serio record, so its in-memory packed bytes are
//! compared.
NOLINT_TEST(PakFormatLayoutWorldTest, PointLightRecordWireBytes)
{
  const auto light = world::PointLightRecord {
    .node_index = 0x51525354U,
    .common = MakeCommon(),
    .range = 7.5F,
    .source_radius = 8.5F,
    .luminous_flux_lm = 9.5F,
  };

  auto expected = WireBytes {};
  expected.U32(0x51525354U).Append(CommonBytes()).Floats(7.5F, 3);

  EXPECT_EQ(RawBytes(light), expected.Bytes());
}

//! A spot light is its node index, common block, range, inner and outer cone
//! angles, source radius and luminous flux. It is not a Serio record, so its
//! in-memory packed bytes are compared.
NOLINT_TEST(PakFormatLayoutWorldTest, SpotLightRecordWireBytes)
{
  const auto light = world::SpotLightRecord {
    .node_index = 0x51525354U,
    .common = MakeCommon(),
    .range = 7.5F,
    .inner_cone_angle_radians = 8.5F,
    .outer_cone_angle_radians = 9.5F,
    .source_radius = 10.5F,
    .luminous_flux_lm = 11.5F,
  };

  auto expected = WireBytes {};
  expected.U32(0x51525354U).Append(CommonBytes()).Floats(7.5F, 5);

  EXPECT_EQ(RawBytes(light), expected.Bytes());
}

} // namespace
