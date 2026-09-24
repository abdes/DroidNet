//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <stdexcept>
#include <tuple>

#include <Oxygen/Vortex/Test/Support/LightingWorkload.h>

namespace oxygen::vortex::testing {
namespace {
  constexpr std::uint32_t kMaximumLights = 4096U;
  constexpr std::uint32_t kRelevantSubset = 64U;
  constexpr std::uint32_t kMotionPeriod = 240U;
  constexpr float kSparseSpacingM = 1.5F;
  constexpr float kDenseSpacingM = 0.05F;
  constexpr float kSourceHeightM = 1.5F;
  constexpr float kRemoteOffsetM = 1000.0F;
  constexpr float kMotionAmplitudeM = 0.25F;
  constexpr float kSecondaryOffsetM = 16.0F;
  constexpr float kNearPlaneM = 0.1F;
  constexpr float kFarPlaneM = 100.0F;
  constexpr float kMinimumOuterHalfAngle = 0.2F;
} // namespace

auto BuildLightingWorkload(const LightingWorkloadOptions& options)
  -> LightingWorkload
{
  if (options.light_count > kMaximumLights || options.width == 0U
    || options.height == 0U || !std::isfinite(options.camera_height_m)
    || options.camera_height_m <= kNearPlaneM
    || options.camera_height_m >= kFarPlaneM
    || options.distribution > WorkloadDistribution::kMostlyIrrelevant
    || options.mutation > WorkloadMutation::kDeleteQuarter
    || options.projection > WorkloadProjection::kOrthographic
    || !std::isfinite(options.source_radius_m) || options.source_radius_m < 0.0F
    || options.source_radius_m >= kSourceHeightM
    || !std::isfinite(options.spot_outer_half_angle_radians)
    || options.spot_outer_half_angle_radians < kMinimumOuterHalfAngle
    || options.spot_outer_half_angle_radians > std::numbers::pi_v<float> / 2.0F
    || options.point_shadow_requests > (options.light_count + 1U) / 2U
    || options.spot_shadow_requests > options.light_count / 2U
    || !std::ranges::all_of(options.content_origin_px, [](float value) -> bool {
         return std::isfinite(value) && value >= 0.0F;
       })) {
    throw std::invalid_argument("Invalid lighting workload options");
  }
  auto result = LightingWorkload {};
  const auto population
    = options.distribution == WorkloadDistribution::kMostlyIrrelevant
    ? std::min(options.light_count, kRelevantSubset)
    : options.light_count;
  const auto columns = std::max(
    1U, static_cast<std::uint32_t>(std::ceil(std::sqrt(population))));
  const auto spacing = options.distribution == WorkloadDistribution::kDense
    ? kDenseSpacingM
    : kSparseSpacingM;
  const auto center = static_cast<float>(columns - 1U) * 0.5F;
  result.floor_half_extent_m = std::max(
    result.floor_half_extent_m, static_cast<float>(columns) * spacing * 0.5F);
  result.lights.reserve(options.light_count);
  for (std::uint32_t index = 0U; index < options.light_count; ++index) {
    const bool affected = (index / 2U) % 4U == 0U;
    if (affected && options.mutation == WorkloadMutation::kDeleteQuarter) {
      continue;
    }
    const auto row = index / columns;
    auto light = WorkloadLight {
      .id = WorkloadLightId { index },
      .kind
      = index % 2U == 0U ? WorkloadLightKind::kPoint : WorkloadLightKind::kSpot,
      .position_ws = { (static_cast<float>(index % columns) - center) * spacing,
        (static_cast<float>(row) - center) * spacing, kSourceHeightM, },
      .source_radius_m = options.source_radius_m,
      .outer_half_angle_radians = options.spot_outer_half_angle_radians,
      .enabled
      = !affected || options.mutation != WorkloadMutation::kDisableQuarter,
    };
    if (options.distribution == WorkloadDistribution::kMostlyIrrelevant
      && index >= kRelevantSubset) {
      light.position_ws.at(0) += kRemoteOffsetM;
    }
    if (options.moving) {
      const auto phase = 2.0 * std::numbers::pi_v<double>
        * static_cast<double>(
          ((options.motion_frame % kMotionPeriod) + (index % kMotionPeriod))
          % kMotionPeriod)
        / kMotionPeriod;
      light.position_ws.at(0)
        += kMotionAmplitudeM * static_cast<float>(std::sin(phase));
      light.position_ws.at(1)
        += kMotionAmplitudeM * static_cast<float>(std::cos(phase));
    }
    result.lights.push_back(light);
  }
  // Select by the static recipe position so requested shadows affect visible
  // receivers and keep the same owners throughout a motion cycle.
  auto shadow_order = std::vector<std::size_t>(result.lights.size());
  for (std::size_t index = 0U; index < shadow_order.size(); ++index) {
    shadow_order.at(index) = index;
  }
  std::ranges::sort(shadow_order, {}, [&](const std::size_t index) {
    const auto id = result.lights.at(index).id.get();
    const auto x = (static_cast<float>(id % columns) - center) * spacing;
    const auto y = (static_cast<float>(id / columns) - center) * spacing;
    return std::tuple { x * x + y * y, id };
  });
  auto remaining_point = options.point_shadow_requests;
  auto remaining_spot = options.spot_shadow_requests;
  for (const auto index : shadow_order) {
    auto& light = result.lights.at(index);
    auto& remaining = light.kind == WorkloadLightKind::kPoint ? remaining_point
                                                              : remaining_spot;
    if (remaining != 0U) {
      light.casts_shadows = true;
      --remaining;
    }
  }
  result.views.push_back({
    .id = ViewId { 1U },
    .eye_ws = { 0.0F, 0.0F, options.camera_height_m },
    .width = options.width,
    .height = options.height,
    .projection = options.projection,
    .content_origin_px = options.content_origin_px,
  });
  if (options.secondary_view) {
    result.views.push_back({
      .id = ViewId { 2U },
      .eye_ws = { kSecondaryOffsetM, 0.0F, options.camera_height_m },
      .width = std::max(1U, options.width / 2U),
      .height = std::max(1U, options.height / 2U),
      .projection = options.projection,
      .content_origin_px = options.content_origin_px,
    });
  }
  if (options.reverse_lights) {
    std::ranges::reverse(result.lights);
  }
  if (options.reverse_views) {
    std::ranges::reverse(result.views);
  }
  return result;
}

auto CountGuaranteedVisibleContributors(
  const LightingWorkload& workload, const WorkloadView& view) -> std::uint32_t
{
  const auto half_height
    = static_cast<double>(view.eye_ws.at(2)) / std::numbers::sqrt3;
  const auto half_width = half_height * view.width / view.height;
  return static_cast<std::uint32_t>(
    std::ranges::count_if(workload.lights, [&](const auto& light) -> bool {
      return light.enabled && !light.casts_shadows && light.flux_lm > 0.0F
        && light.position_ws.at(2) > light.source_radius_m
        && light.position_ws.at(2) < light.range_m
        && std::abs(light.position_ws.at(0)) < workload.floor_half_extent_m
        && std::abs(light.position_ws.at(1)) < workload.floor_half_extent_m
        && std::abs(light.position_ws.at(0) - view.eye_ws.at(0)) < half_width
        && std::abs(light.position_ws.at(1) - view.eye_ws.at(1)) < half_height;
    }));
}

} // namespace oxygen::vortex::testing
