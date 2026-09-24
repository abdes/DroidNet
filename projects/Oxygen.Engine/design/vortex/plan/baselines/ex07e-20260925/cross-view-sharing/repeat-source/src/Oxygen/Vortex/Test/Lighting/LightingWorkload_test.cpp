//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <numbers>
#include <stdexcept>
#include <unordered_set>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Test/Support/LightingWorkload.h>

namespace oxygen::vortex::testing {
namespace {
  NOLINT_TEST(LightingWorkloadTest, PrimaryIsFrozenAndHasVisibleMixedPopulation)
  {
    auto input = std::ifstream(OXYGEN_LIGHTING_WORKLOAD_MANIFEST);
    ASSERT_TRUE(input.good());
    const auto manifest = nlohmann::json::parse(input);
    ASSERT_EQ(manifest.at("revision"), 2U);
    const auto primary = BuildLightingWorkload({});
    ASSERT_EQ(primary.lights.size(), manifest.at("primary").at("light_count"));
    ASSERT_EQ(primary.lights.size(), 1024U);
    ASSERT_EQ(primary.views.size(), 1U);
    EXPECT_EQ(primary.views.front().width, 1920U);
    EXPECT_EQ(primary.views.front().height, 1080U);
    EXPECT_EQ(primary.floor_half_extent_m, 24.0F);
    EXPECT_EQ(std::ranges::count_if(primary.lights,
                [](const auto& light) -> bool {
                  return light.kind == WorkloadLightKind::kPoint;
                }),
      512);
    EXPECT_EQ(
      CountGuaranteedVisibleContributors(primary, primary.views.front()), 576U);
    EXPECT_EQ(primary.lights.front().position_ws,
      (std::array { -23.25F, -23.25F, 1.5F }));
    EXPECT_EQ(
      primary.lights.back().position_ws, (std::array { 23.25F, 23.25F, 1.5F }));
    auto minimum_visible = 1024U;
    for (unsigned frame = 0U; frame < 240U; ++frame) {
      const auto moving
        = BuildLightingWorkload({ .moving = true, .motion_frame = frame });
      minimum_visible = std::min(minimum_visible,
        CountGuaranteedVisibleContributors(moving, moving.views.front()));
    }
    EXPECT_GE(minimum_visible, 256U);
    RecordProperty("primary_lights", primary.lights.size());
    RecordProperty("minimum_visible_over_motion_cycle", minimum_visible);
  }

  NOLINT_TEST(LightingWorkloadTest, CountAndDistributionMatrixPreservesIdentity)
  {
    for (const auto count :
      { 0U, 1U, 31U, 32U, 33U, 64U, 256U, 1024U, 4096U }) {
      for (const auto distribution : {
             WorkloadDistribution::kSparse,
             WorkloadDistribution::kDense,
             WorkloadDistribution::kMostlyIrrelevant,
           }) {
        const auto workload = BuildLightingWorkload(
          { .light_count = count, .distribution = distribution });
        ASSERT_EQ(workload.lights.size(), count);
        auto ids = std::unordered_set<std::uint32_t> {};
        for (const auto& light : workload.lights) {
          EXPECT_TRUE(ids.insert(light.id.get()).second);
        }
        const auto reversed = BuildLightingWorkload({
          .light_count = count,
          .distribution = distribution,
          .reverse_lights = true,
        });
        for (std::size_t index = 0U; index < count; ++index) {
          EXPECT_EQ(workload.lights.at(index).id,
            reversed.lights.at(count - 1U - index).id);
          EXPECT_EQ(workload.lights.at(index).position_ws,
            reversed.lights.at(count - 1U - index).position_ws);
        }
        const auto visible = CountGuaranteedVisibleContributors(
          workload, workload.views.front());
        if (distribution == WorkloadDistribution::kDense) {
          EXPECT_EQ(visible, count);
        }
        if (distribution == WorkloadDistribution::kMostlyIrrelevant) {
          EXPECT_EQ(visible, std::min(count, 64U));
        }
      }
    }
  }

  NOLINT_TEST(
    LightingWorkloadTest, VariantsKeepRequestedShadowsAndStableSurvivors)
  {
    const auto workload = BuildLightingWorkload({
      .projection = WorkloadProjection::kOrthographic,
      .secondary_view = true,
      .reverse_views = true,
      .point_shadow_requests = 5U,
      .spot_shadow_requests = 9U,
      .source_radius_m = 0.25F,
      .spot_outer_half_angle_radians = std::numbers::pi_v<float> / 2.0F,
      .width = 3840U,
      .height = 2160U,
      .content_origin_px = { 17.0F, 9.0F },
    });
    ASSERT_EQ(workload.views.size(), 2U);
    EXPECT_EQ(workload.views.front().id.get(), 2U);
    EXPECT_EQ(workload.views.front().width, 1920U);
    EXPECT_EQ(workload.views.front().height, 1080U);
    EXPECT_EQ(std::ranges::count_if(workload.lights,
                [](const auto& light) -> bool {
                  return light.casts_shadows
                    && light.kind == WorkloadLightKind::kPoint;
                }),
      5);
    EXPECT_EQ(std::ranges::count_if(workload.lights,
                [](const auto& light) -> bool {
                  return light.casts_shadows
                    && light.kind == WorkloadLightKind::kSpot;
                }),
      9);
    const auto disabled = BuildLightingWorkload(
      { .mutation = WorkloadMutation::kDisableQuarter });
    const auto deleted
      = BuildLightingWorkload({ .mutation = WorkloadMutation::kDeleteQuarter });
    EXPECT_EQ(disabled.lights.size(), 1024U);
    EXPECT_EQ(deleted.lights.size(), 768U);
    EXPECT_EQ(std::ranges::count_if(disabled.lights,
                [](const auto& light) -> bool { return light.enabled; }),
      768);
    for (const auto& light : deleted.lights) {
      EXPECT_TRUE(disabled.lights.at(light.id.get()).enabled);
      EXPECT_EQ(
        light.position_ws, disabled.lights.at(light.id.get()).position_ws);
    }
    const auto next_cycle
      = BuildLightingWorkload({ .moving = true, .motion_frame = 240U });
    const auto first_cycle
      = BuildLightingWorkload({ .moving = true, .motion_frame = 0U });
    for (std::size_t index = 0U; index < next_cycle.lights.size(); ++index) {
      EXPECT_EQ(next_cycle.lights.at(index).position_ws,
        first_cycle.lights.at(index).position_ws);
    }
  }

  NOLINT_TEST(LightingWorkloadTest, ShadowOwnersStayVisibleThroughoutMotion)
  {
    auto owners = std::unordered_set<std::uint32_t> {};
    for (unsigned phase = 0U; phase < 240U; ++phase) {
      const auto workload = BuildLightingWorkload({ .moving = true,
        .motion_frame = phase,
        .point_shadow_requests = 5U,
        .spot_shadow_requests = 9U });
      auto current = std::unordered_set<std::uint32_t> {};
      for (const auto& light : workload.lights) {
        if (light.casts_shadows) {
          // The old first-row selection was outside the visible receiver.
          EXPECT_LT(std::abs(light.position_ws.at(0)), 6.0F);
          EXPECT_LT(std::abs(light.position_ws.at(1)), 6.0F);
          current.insert(light.id.get());
        }
      }
      ASSERT_EQ(current.size(), 14U);
      if (phase == 0U) {
        owners = current;
      }
      EXPECT_EQ(current, owners);
    }
  }

  NOLINT_TEST(LightingWorkloadTest, RejectsImpossibleRecipeInputs)
  {
    EXPECT_THROW(
      static_cast<void>(BuildLightingWorkload({ .light_count = 4097U })),
      std::invalid_argument);
    EXPECT_THROW(static_cast<void>(BuildLightingWorkload(
                   { .light_count = 1U, .spot_shadow_requests = 1U })),
      std::invalid_argument);
    EXPECT_THROW(
      static_cast<void>(BuildLightingWorkload({ .source_radius_m = 2.0F })),
      std::invalid_argument);
    EXPECT_THROW(static_cast<void>(BuildLightingWorkload({ .width = 0U })),
      std::invalid_argument);
  }
} // namespace
NOLINT_TEST(LightingWorkloadTest,
  ExplicitSecondaryLayoutsPreserveLegacyDefaultAndSupportSharing)
{
  LightingWorkloadOptions options;
  options.secondary_view = true;
  const auto legacy = BuildLightingWorkload(options);
  ASSERT_EQ(legacy.views.size(), 2U);
  EXPECT_EQ(legacy.views[1].width, options.width / 2U);
  EXPECT_EQ(legacy.views[1].eye_ws[0], 16.0F);
  options.secondary_layout = WorkloadSecondaryLayout::kMatched;
  const auto matched = BuildLightingWorkload(options);
  EXPECT_EQ(matched.views[0].width, matched.views[1].width);
  EXPECT_EQ(matched.views[0].eye_ws, matched.views[1].eye_ws);
  EXPECT_NE(matched.views[0].id, matched.views[1].id);
  options.secondary_layout = WorkloadSecondaryLayout::kPartialOverlap;
  const auto partial = BuildLightingWorkload(options);
  EXPECT_EQ(partial.views[1].width, options.width);
  EXPECT_EQ(partial.views[1].eye_ws[0], 26.0F);
}
} // namespace oxygen::vortex::testing
