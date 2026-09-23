//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>

#include <glm/ext/vector_float3.hpp>

#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Lighting/Internal/LightEvaluationRecords.h>
#include <Oxygen/Vortex/Lighting/Types/LightingPreparationFailure.h>
#include <Oxygen/Vortex/Types/FrameLightSelection.h>
#include <Oxygen/Vortex/Types/LightingIndices.h>

namespace oxygen::vortex::lighting::internal {
namespace {

  NOLINT_TEST(
    LightEvaluationRecordsTest, ResolvesUnitsModifiersAndStableSelectionIndices)
  {
    auto input = FrameLightSelection {};
    input.directional_lights = {
      FrameDirectionalLightSelection {
        .source_node = {},
        .direction = { 0.0F, -4.0F, 0.0F },
        .color = { 0.25F, 0.5F, 1.0F },
        .illuminance_lux = 32.0F,
        .exposure_compensation_ev = 1.0F,
      },
    };
    input.local_lights = {
      FrameLocalLightSelection { .range = 16.0F, .luminous_flux_lm = 1000.0F },
      FrameLocalLightSelection {
        .kind = LocalLightKind::kSpot,
        .position = { 1.0F, 2.0F, 3.0F },
        .range = 8.0F,
        .color = { 0.5F, 1.0F, 0.0F },
        .luminous_flux_lm = 2000.0F,
        .exposure_compensation_ev = -1.0F,
        .direction = { 0.0F, 0.0F, -4.0F },
        .inner_cone_half_angle_radians = 0.0F,
        .outer_cone_half_angle_radians = std::numbers::pi_v<float> / 2.0F,
        .source_radius = 0.125F,
        .flags = kLocalLightFlagCastsShadows | kLightFlagContactShadows,
      },
    };
    const auto records = ResolveLightEvaluationRecords(input);
    ASSERT_TRUE(records.has_value());
    ASSERT_EQ(records->directional.size(), 1U);
    ASSERT_EQ(records->local.size(), 2U);
    const auto& directional = records->directional.front();
    EXPECT_EQ(directional.direction_to_source_ws, (glm::vec3 { 0, -1, 0 }));
    EXPECT_EQ(directional.illuminance_rgb_lux, (glm::vec3 { 16, 32, 64 }));
    EXPECT_EQ(directional.selection_index, LightSelectionIndex { 0U });
    EXPECT_EQ(directional.reserved, (std::array<std::uint32_t, 3> {}));
    const auto& point = records->local.front();
    EXPECT_NEAR(
      point.intensity_rgb_cd.r, 1000.0 / (4.0 * std::numbers::pi), 1.0e-5);
    EXPECT_EQ(point.inverse_range_m, 0.0625F);
    const auto& spot = records->local.back();
    EXPECT_EQ(spot.selection_index, LightSelectionIndex { 1U });
    EXPECT_EQ(spot.emitted_direction_ws, (glm::vec3 { 0, 0, -1 }));
    EXPECT_NEAR(
      spot.intensity_rgb_cd.g, 1000.0 / (2.0 * std::numbers::pi / 3.0), 1.0e-4);
    EXPECT_EQ(spot.intensity_rgb_cd.r, spot.intensity_rgb_cd.g * 0.5F);
    EXPECT_EQ(spot.intensity_rgb_cd.b, 0.0F);
    EXPECT_EQ(spot.inverse_cone_cosine_width, 1.0F);
    EXPECT_EQ(spot.source_radius_m, 0.125F);
    EXPECT_EQ(spot.flags, 3U);
    EXPECT_EQ(spot.reserved, (std::array<std::uint32_t, 3> {}));
  }

  NOLINT_TEST(
    LightEvaluationRecordsTest, UnrepresentableFp32ConeRejectsTheWholePublication)
  {
    auto input = FrameLightSelection {};
    constexpr float kInner = 4.0e-6F;
    constexpr float kOuter = 1.0e-5F;
    input.local_lights = {
      FrameLocalLightSelection {
        .source_node = {},
        .kind = LocalLightKind::kSpot,
        .range = 10.0F,
        .luminous_flux_lm = 100.0F,
        .direction = { 0.0F, 0.0F, -1.0F },
        .inner_cone_half_angle_radians = kInner,
        .outer_cone_half_angle_radians = kOuter,
      },
    };
    const auto records = ResolveLightEvaluationRecords(input);
    ASSERT_FALSE(records.has_value());
    EXPECT_EQ(records.error().error, LightingPreparationError::kUnrepresentable);
  }

  NOLINT_TEST(
    LightEvaluationRecordsTest, InvalidLaterLightRejectsTheWholeCandidate)
  {
    auto input = FrameLightSelection {};
    input.local_lights.resize(2);
    input.local_lights.back().luminous_flux_lm = -1.0F;
    const auto records = ResolveLightEvaluationRecords(input);
    ASSERT_FALSE(records.has_value());
    EXPECT_EQ(records.error().error, LightingPreparationError::kInvalidInput);
    EXPECT_EQ(records.error().family, LightingSelectionFamily::kLocal);
    EXPECT_EQ(records.error().selection_index, LightSelectionIndex { 1U });
  }

  NOLINT_TEST(
    LightEvaluationRecordsTest, ZeroRangeIsPreservedWithoutAnInfluenceFloor)
  {
    auto input = FrameLightSelection {};
    input.local_lights.push_back(FrameLocalLightSelection {
      .range = 0.0F,
      .luminous_flux_lm = 1000.0F,
      .source_radius = 0.5F,
    });
    const auto records = ResolveLightEvaluationRecords(input);
    ASSERT_TRUE(records.has_value());
    EXPECT_EQ(records->local.front().range_m, 0.0F);
    EXPECT_EQ(records->local.front().inverse_range_m, 0.0F);
    EXPECT_EQ(records->local.front().source_radius_m, 0.5F);
  }

  NOLINT_TEST(LightEvaluationRecordsTest, UnknownKindAndFlagsAreRejected)
  {
    auto input = FrameLightSelection {};
    input.local_lights.resize(1);
    input.local_lights.front().kind = static_cast<LocalLightKind>(255);
    EXPECT_FALSE(ResolveLightEvaluationRecords(input).has_value());
    input.local_lights.front().kind = LocalLightKind::kPoint;
    input.local_lights.front().flags = 0x80000000U;
    EXPECT_FALSE(ResolveLightEvaluationRecords(input).has_value());
  }

  NOLINT_TEST(
    LightEvaluationRecordsTest, InvalidDirectionAndReciprocalAreRejected)
  {
    auto input = FrameLightSelection {};
    input.local_lights.push_back(FrameLocalLightSelection {
      .kind = LocalLightKind::kSpot,
      .direction = glm::vec3 { 0.0F },
    });
    EXPECT_FALSE(ResolveLightEvaluationRecords(input).has_value());
    input.local_lights.front().kind = LocalLightKind::kPoint;
    input.local_lights.front().range = std::numeric_limits<float>::denorm_min();
    const auto records = ResolveLightEvaluationRecords(input);
    ASSERT_FALSE(records.has_value());
    EXPECT_EQ(
      records.error().error, LightingPreparationError::kUnrepresentable);
  }

  NOLINT_TEST(LightEvaluationRecordsTest,
    DirectionalArrayPreservesOrderAndAtmosphereSlots)
  {
    auto input = FrameLightSelection {};
    input.directional_lights = {
      FrameDirectionalLightSelection {
        .source_node = {}, .illuminance_lux = 10.0F },
      FrameDirectionalLightSelection {
        .source_node = {},
        .illuminance_lux = 20.0F,
        .exposure_compensation_ev = 1.0F,
        .atmosphere_light_slot = 1U,
      },
      FrameDirectionalLightSelection {
        .source_node = {},
        .illuminance_lux = 30.0F,
        .atmosphere_light_slot = 0U,
      },
    };
    const auto records = ResolveLightEvaluationRecords(input);
    ASSERT_TRUE(records.has_value());
    ASSERT_EQ(records->directional.size(), 3U);
    EXPECT_EQ(
      records->directional.at(0).selection_index, LightSelectionIndex { 0U });
    EXPECT_EQ(
      records->directional.at(1).selection_index, LightSelectionIndex { 1U });
    EXPECT_EQ(
      records->directional.at(2).selection_index, LightSelectionIndex { 2U });
    EXPECT_EQ(records->directional.at(0).atmosphere_light_slot,
      kInvalidAtmosphereLightIndex);
    EXPECT_EQ(records->directional.at(1).atmosphere_light_slot,
      AtmosphereLightIndex { 1U });
    EXPECT_EQ(records->directional.at(2).atmosphere_light_slot,
      AtmosphereLightIndex { 0U });
    EXPECT_EQ(records->directional.at(1).illuminance_rgb_lux, glm::vec3(40.0F));
  }

  NOLINT_TEST(LightEvaluationRecordsTest,
    DuplicateAtmosphereAssignmentRejectsTheWholeCandidate)
  {
    auto input = FrameLightSelection {};
    input.directional_lights = {
      FrameDirectionalLightSelection {
        .source_node = {}, .atmosphere_light_slot = 1U },
      FrameDirectionalLightSelection {
        .source_node = {}, .atmosphere_light_slot = 1U },
    };
    const auto records = ResolveLightEvaluationRecords(input);
    ASSERT_FALSE(records.has_value());
    EXPECT_EQ(records.error().family, LightingSelectionFamily::kDirectional);
    EXPECT_EQ(records.error().selection_index, LightSelectionIndex { 1U });
  }

} // namespace
} // namespace oxygen::vortex::lighting::internal
