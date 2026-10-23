//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Data/SceneRecordDefaults.h and the initializers of the scene
// component records in Data/PakFormat_world.h.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include <Oxygen/Core/Types/CameraAspectMode.h>
#include <Oxygen/Data/LightValidation.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneRecordDefaults.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Testing/GTest.h>

namespace {

namespace world = oxygen::data::pak::world;
namespace schema = oxygen::data::schema;
using world::MakeDefaultRecord;

template <typename Record> auto IsAllZeroBytes(const Record& record) -> bool
{
  const auto bytes = std::as_bytes(std::span(&record, 1U));
  return std::ranges::all_of(bytes,
    [](const std::byte value) -> bool { return value == std::byte { 0 }; });
}

// The schema owns every default; a record built without one holds none.
NOLINT_TEST(SceneRecordDefaultsTest, ComponentRecordsStartValueInitialized)
{
  EXPECT_TRUE(IsAllZeroBytes(world::PerspectiveCameraRecord {}));
  EXPECT_TRUE(IsAllZeroBytes(world::OrthographicCameraRecord {}));
  EXPECT_TRUE(IsAllZeroBytes(world::LightShadowSettingsRecord {}));
  EXPECT_TRUE(IsAllZeroBytes(world::LightCommonRecord {}));
  EXPECT_TRUE(IsAllZeroBytes(world::DirectionalLightRecord {}));
  EXPECT_TRUE(IsAllZeroBytes(world::PointLightRecord {}));
  EXPECT_TRUE(IsAllZeroBytes(world::SpotLightRecord {}));
  EXPECT_TRUE(IsAllZeroBytes(world::LocalFogVolumeRecord {}));
}

NOLINT_TEST(SceneRecordDefaultsTest, CameraDefaultsAreTheSchemaDefaultsAndValid)
{
  namespace camera = schema::perspective_camera;
  const auto perspective = MakeDefaultRecord<world::PerspectiveCameraRecord>();
  EXPECT_FLOAT_EQ(perspective.fov_y, camera::kFieldOfViewYRadiansDefault);
  EXPECT_FLOAT_EQ(perspective.aspect_ratio, camera::kAspectRatioDefault);
  EXPECT_FLOAT_EQ(perspective.near_plane, camera::kNearPlaneDefault);
  EXPECT_FLOAT_EQ(perspective.far_plane, camera::kFarPlaneDefault);
  EXPECT_FLOAT_EQ(perspective.aperture_f, camera::kApertureFDefault);
  EXPECT_FLOAT_EQ(perspective.shutter_rate, camera::kShutterRateDefault);
  EXPECT_FLOAT_EQ(perspective.iso, camera::kIsoDefault);
  EXPECT_EQ(perspective.aspect_mode, oxygen::CameraAspectMode::kAuto);
  EXPECT_TRUE(world::HasValidPerspectiveCameraValues(perspective));

  namespace ortho = schema::orthographic_camera;
  const auto orthographic
    = MakeDefaultRecord<world::OrthographicCameraRecord>();
  EXPECT_FLOAT_EQ(orthographic.top, ortho::kOrthographicSizeDefault);
  EXPECT_FLOAT_EQ(orthographic.bottom, -ortho::kOrthographicSizeDefault);
  EXPECT_FLOAT_EQ(orthographic.right,
    ortho::kOrthographicSizeDefault * ortho::kAspectRatioDefault);
  EXPECT_FLOAT_EQ(orthographic.left, -orthographic.right);
  EXPECT_FLOAT_EQ(orthographic.near_plane, ortho::kNearPlaneDefault);
  EXPECT_FLOAT_EQ(orthographic.far_plane, ortho::kFarPlaneDefault);
  EXPECT_TRUE(world::HasValidOrthographicCameraValues(orthographic));
}

NOLINT_TEST(SceneRecordDefaultsTest, LightDefaultsAreTheSchemaDefaultsAndValid)
{
  namespace sun_schema = schema::directional_light;
  const auto sun = MakeDefaultRecord<world::DirectionalLightRecord>();
  EXPECT_FLOAT_EQ(sun.intensity_lux, sun_schema::kIntensityLuxDefault);
  EXPECT_FLOAT_EQ(
    sun.angular_size_radians, sun_schema::kAngularSizeRadiansDefault);
  EXPECT_EQ(sun.cascade_count,
    static_cast<std::uint32_t>(sun_schema::kCascadeCountDefault));
  EXPECT_FLOAT_EQ(
    sun.cascade_distances[0], sun_schema::kCascadeDistance0Default);
  EXPECT_FLOAT_EQ(
    sun.cascade_distances[3], sun_schema::kCascadeDistance3Default);
  EXPECT_FLOAT_EQ(
    sun.max_shadow_distance, sun_schema::kMaxShadowDistanceDefault);
  EXPECT_EQ(sun.common.casts_shadows, 1U);
  EXPECT_EQ(sun.common.affects_world, 1U);
  EXPECT_FLOAT_EQ(
    sun.common.shadow.normal_bias, sun_schema::kShadowNormalBiasDefault);
  EXPECT_EQ(sun.common.shadow.resolution_hint,
    std::to_underlying(sun_schema::kShadowResolutionHintDefault));
  EXPECT_EQ(sun.atmosphere_light_slot,
    std::to_underlying(sun_schema::kAtmosphereLightSlotDefault));
  EXPECT_EQ(sun.split_mode, std::to_underlying(sun_schema::kSplitModeDefault));
  EXPECT_TRUE(oxygen::data::IsValidLightRecord(sun));

  namespace point_schema = schema::point_light;
  const auto point = MakeDefaultRecord<world::PointLightRecord>();
  EXPECT_FLOAT_EQ(point.range, point_schema::kRangeDefault);
  EXPECT_FLOAT_EQ(point.luminous_flux_lm, point_schema::kLuminousFluxLmDefault);
  EXPECT_TRUE(oxygen::data::IsValidLightRecord(point));

  namespace spot_schema = schema::spot_light;
  const auto spot = MakeDefaultRecord<world::SpotLightRecord>();
  EXPECT_FLOAT_EQ(spot.range, spot_schema::kRangeDefault);
  EXPECT_FLOAT_EQ(
    spot.inner_cone_angle_radians, spot_schema::kInnerConeAngleRadiansDefault);
  EXPECT_FLOAT_EQ(
    spot.outer_cone_angle_radians, spot_schema::kOuterConeAngleRadiansDefault);
  EXPECT_TRUE(oxygen::data::IsValidLightRecord(spot));
}

NOLINT_TEST(
  SceneRecordDefaultsTest, LocalFogVolumeDefaultsAreTheSchemaDefaultsAndValid)
{
  namespace fog_schema = schema::local_fog_volume;
  const auto fog = MakeDefaultRecord<world::LocalFogVolumeRecord>();
  EXPECT_EQ(fog.enabled, 1U);
  EXPECT_FLOAT_EQ(
    fog.radial_fog_extinction, fog_schema::kRadialFogExtinctionDefault);
  EXPECT_FLOAT_EQ(fog.height_fog_falloff, fog_schema::kHeightFogFalloffDefault);
  EXPECT_FLOAT_EQ(fog.fog_phase_g, fog_schema::kFogPhaseGDefault);
  EXPECT_FLOAT_EQ(fog.fog_albedo[2], fog_schema::kFogAlbedoBDefault);
  EXPECT_FLOAT_EQ(fog.fog_emissive[0], fog_schema::kFogEmissiveRDefault);
  EXPECT_EQ(fog.sort_priority, fog_schema::kSortPriorityDefault);
  EXPECT_TRUE(world::HasValidLocalFogVolumeValues(fog));
}

} // namespace
