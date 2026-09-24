//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Core/Types/View.h>

namespace oxygen::vortex::testing {

using WorkloadLightId
  = NamedType<std::uint32_t, struct WorkloadLightIdTag, Comparable>;
enum class WorkloadLightKind : std::uint8_t { kPoint, kSpot };
enum class WorkloadDistribution : std::uint8_t {
  kSparse,
  kDense,
  kMostlyIrrelevant,
};
enum class WorkloadMutation : std::uint8_t {
  kNone,
  kDisableQuarter,
  kDeleteQuarter,
};
enum class WorkloadProjection : std::uint8_t { kPerspective, kOrthographic };

enum class WorkloadSecondaryLayout : std::uint8_t {
  kOffsetHalf,
  kMatched,
  kOffsetFull,
  kPartialOverlap
};
struct LightingWorkloadOptions {
  std::uint32_t light_count { 1024U };
  WorkloadDistribution distribution { WorkloadDistribution::kSparse };
  WorkloadMutation mutation { WorkloadMutation::kNone };
  WorkloadProjection projection { WorkloadProjection::kPerspective };
  bool moving { false };
  std::uint32_t motion_frame { 0U };
  bool secondary_view { false };
  WorkloadSecondaryLayout secondary_layout {
    WorkloadSecondaryLayout::kOffsetHalf
  };
  bool reverse_lights { false };
  bool reverse_views { false };
  std::uint32_t point_shadow_requests { 0U };
  std::uint32_t spot_shadow_requests { 0U };
  float source_radius_m { 0.0F };
  float spot_outer_half_angle_radians { 0.6F };
  float camera_height_m { 24.0F };
  std::uint32_t width { 1920U };
  std::uint32_t height { 1080U };
  std::array<float, 2> content_origin_px { 0.0F, 0.0F };
};

struct WorkloadLight {
  WorkloadLightId id { 0U };
  WorkloadLightKind kind { WorkloadLightKind::kPoint };
  std::array<float, 3> position_ws {};
  std::array<float, 3> color_rgb { 1.0F, 1.0F, 1.0F };
  float flux_lm { 100.0F };
  float range_m { 3.0F };
  float source_radius_m { 0.0F };
  float inner_half_angle_radians { 0.2F };
  float outer_half_angle_radians { 0.6F };
  bool enabled { true };
  bool casts_shadows { false };
};

struct WorkloadView {
  ViewId id { 1U };
  std::array<float, 3> eye_ws { 0.0F, 0.0F, 24.0F };
  std::uint32_t width { 1920U };
  std::uint32_t height { 1080U };
  WorkloadProjection projection { WorkloadProjection::kPerspective };
  std::array<float, 2> content_origin_px {};
};

//! Recipe v1: XY floor at z=0, normal +Z, camera looks -Z with up +Y.
//! Spots emit -Z. FOV is 60 degrees, near/far 0.1/100 m; orthographic views
//! use the same floor footprint. Material: linear gray 0.5, roughness 0.5,
//! metallic 0, specular 0.5, opaque. Manual EV0, no sky/fog/bloom.
struct LightingWorkload {
  float floor_half_extent_m { 24.0F };
  std::vector<WorkloadLight> lights;
  std::vector<WorkloadView> views;
};

[[nodiscard]] auto BuildLightingWorkload(const LightingWorkloadOptions& options)
  -> LightingWorkload;
//! Conservative lower bound: unshadowed on-axis samples directly below sources,
//! inside the visible floor. It does not count every contributing light.
[[nodiscard]] auto CountGuaranteedVisibleContributors(
  const LightingWorkload& workload, const WorkloadView& view) -> std::uint32_t;

} // namespace oxygen::vortex::testing
