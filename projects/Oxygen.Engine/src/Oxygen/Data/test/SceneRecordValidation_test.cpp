//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Data/LightValidation.h and the camera and local fog volume validators
// of Data/PakFormat_world.h.

#include <numbers>

#include <Oxygen/Data/LightValidation.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneRecordDefaults.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Testing/GTest.h>

namespace {

namespace world = oxygen::data::pak::world;
namespace schema = oxygen::data::schema;
using oxygen::data::IsValidLightRecord;
using world::MakeDefaultRecord;

// The common light properties take the ranges the scene schema declares.
NOLINT_TEST(SceneRecordValidationTest, LightCommonValuesFollowTheSchemaRanges)
{
  const auto with = [](const auto change) -> bool {
    auto light = MakeDefaultRecord<world::PointLightRecord>();
    change(light.common);
    return IsValidLightRecord(light);
  };

  EXPECT_FALSE(with([](auto& common) -> void { common.color_rgb[0] = 1.5F; }));
  EXPECT_TRUE(with([](auto& common) -> void { common.color_rgb[0] = 1.0F; }));
  EXPECT_TRUE(with([](auto& common) -> void { common.color_rgb[1] = 0.0F; }));
  EXPECT_FALSE(with(
    [](auto& common) -> void { common.exposure_compensation_ev = 11.0F; }));
  EXPECT_TRUE(with(
    [](auto& common) -> void { common.exposure_compensation_ev = 10.0F; }));
  EXPECT_TRUE(with(
    [](auto& common) -> void { common.exposure_compensation_ev = -10.0F; }));
  EXPECT_FALSE(with([](auto& common) -> void { common.shadow.bias = 11.0F; }));
  EXPECT_TRUE(with([](auto& common) -> void { common.shadow.bias = 10.0F; }));
  EXPECT_FALSE(
    with([](auto& common) -> void { common.shadow.normal_bias = -1.0F; }));
  EXPECT_FALSE(
    with([](auto& common) -> void { common.shadow.resolution_hint = 4U; }));
  EXPECT_TRUE(
    with([](auto& common) -> void { common.shadow.resolution_hint = 3U; }));
  EXPECT_FALSE(with([](auto& common) -> void { common.casts_shadows = 2U; }));
}

NOLINT_TEST(SceneRecordValidationTest, LocalLightsNeedAPositiveRangeAndFlux)
{
  auto point = MakeDefaultRecord<world::PointLightRecord>();
  point.range = 0.0F;
  EXPECT_FALSE(IsValidLightRecord(point));
  point = MakeDefaultRecord<world::PointLightRecord>();
  point.luminous_flux_lm = 0.0F;
  EXPECT_FALSE(IsValidLightRecord(point));
  point = MakeDefaultRecord<world::PointLightRecord>();
  point.source_radius = -1.0F;
  EXPECT_FALSE(IsValidLightRecord(point));

  auto spot = MakeDefaultRecord<world::SpotLightRecord>();
  spot.outer_cone_angle_radians = std::numbers::pi_v<float>;
  EXPECT_FALSE(IsValidLightRecord(spot));
  // Each angle is in range, but the cone pair is not ordered.
  spot = MakeDefaultRecord<world::SpotLightRecord>();
  spot.inner_cone_angle_radians = 0.6F;
  spot.outer_cone_angle_radians = 0.5F;
  EXPECT_FALSE(IsValidLightRecord(spot));
}

NOLINT_TEST(SceneRecordValidationTest, DirectionalValuesFollowTheSchema)
{
  const auto with = [](const auto change) -> bool {
    auto light = MakeDefaultRecord<world::DirectionalLightRecord>();
    change(light);
    return IsValidLightRecord(light);
  };

  EXPECT_TRUE(with([](auto& light) -> void { light.intensity_lux = 0.0F; }));
  EXPECT_FALSE(with([](auto& light) -> void { light.intensity_lux = -1.0F; }));
  EXPECT_FALSE(
    with([](auto& light) -> void { light.atmosphere_light_slot = 3U; }));
  EXPECT_FALSE(with([](auto& light) -> void { light.split_mode = 2U; }));
  EXPECT_FALSE(with([](auto& light) -> void { light.cascade_count = 0U; }));
  EXPECT_FALSE(with([](auto& light) -> void { light.cascade_count = 5U; }));
  EXPECT_TRUE(with([](auto& light) -> void { light.cascade_count = 1U; }));
  EXPECT_FALSE(
    with([](auto& light) -> void { light.angular_size_radians = 3.2F; }));
  EXPECT_FALSE(
    with([](auto& light) -> void { light.transition_fraction = 1.5F; }));
  EXPECT_FALSE(with([](auto& light) -> void {
    light.atmosphere_disk_luminance_scale_rgb[2] = -1.0F;
  }));
  // An unused cascade distance still lies in its range.
  EXPECT_FALSE(with([](auto& light) -> void {
    light.cascade_count = 1U;
    light.cascade_distances[3] = 0.0F;
  }));
  // The distances of the cascades in use increase.
  EXPECT_FALSE(
    with([](auto& light) -> void { light.cascade_distances[1] = 4.0F; }));
}

NOLINT_TEST(SceneRecordValidationTest, CameraValuesFollowTheSchemaRanges)
{
  namespace camera = schema::perspective_camera;
  const auto min_fov = camera::kFieldOfViewYRadiansRange.min.value_or(0.0F);
  auto perspective = MakeDefaultRecord<world::PerspectiveCameraRecord>();
  perspective.fov_y = min_fov;
  EXPECT_TRUE(world::HasValidPerspectiveCameraValues(perspective));
  // Half a degree, below the schema's one degree.
  perspective.fov_y = std::numbers::pi_v<float> / 360.0F;
  EXPECT_FALSE(world::HasValidPerspectiveCameraValues(perspective));
  perspective = MakeDefaultRecord<world::PerspectiveCameraRecord>();
  perspective.far_plane = perspective.near_plane;
  EXPECT_FALSE(world::HasValidPerspectiveCameraValues(perspective));

  auto orthographic = MakeDefaultRecord<world::OrthographicCameraRecord>();
  orthographic.near_plane = 0.0F;
  EXPECT_FALSE(world::HasValidOrthographicCameraValues(orthographic));
  orthographic.near_plane = 0.1F;
  EXPECT_TRUE(world::HasValidOrthographicCameraValues(orthographic));
  orthographic.right = orthographic.left;
  EXPECT_FALSE(world::HasValidOrthographicCameraValues(orthographic));
}

NOLINT_TEST(SceneRecordValidationTest, LocalFogValuesFollowTheSchemaRanges)
{
  namespace fog = schema::local_fog_volume;
  const auto with = [](const auto change) -> bool {
    auto volume = MakeDefaultRecord<world::LocalFogVolumeRecord>();
    change(volume);
    return world::HasValidLocalFogVolumeValues(volume);
  };

  EXPECT_FALSE(with([](auto& volume) -> void { volume.fog_phase_g = 1.0F; }));
  EXPECT_FALSE(with([](auto& volume) -> void { volume.fog_albedo[1] = 1.5F; }));
  EXPECT_FALSE(
    with([](auto& volume) -> void { volume.radial_fog_extinction = -1.0F; }));
  EXPECT_FALSE(with([](auto& volume) -> void { volume.enabled = 2U; }));
  EXPECT_FALSE(with([](auto& volume) -> void { volume.sort_priority = 200; }));
  EXPECT_TRUE(with([](auto& volume) -> void {
    volume.sort_priority = fog::kSortPriorityRange.max.value_or(0);
  }));
  EXPECT_TRUE(
    with([](auto& volume) -> void { volume.height_fog_offset = -50.0F; }));
}

} // namespace
