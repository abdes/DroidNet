//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

namespace oxygen::renderer {

enum ConventionalShadowReceiverAnalysisFlagBits : std::uint32_t {
  kConventionalShadowReceiverAnalysisFlagValid = 1U << 0U,
  kConventionalShadowReceiverAnalysisFlagEmpty = 1U << 1U,
  kConventionalShadowReceiverAnalysisFlagFallbackToLegacyRect = 1U << 2U,
};

//! Per-raster-job receiver-analysis input derived from the conventional
//! backend.
/*!
 The job is authored in rotated light space so `CSM-2` can compare actual
 visible receiver samples against both the full cascade-fit reference and the
 legacy receiver-sphere fit without mixing translated light eye spaces.
*/
struct alignas(16) ConventionalShadowReceiverAnalysisJob {
  glm::mat4 light_rotation_matrix { 1.0F };
  glm::vec4 full_rect_center_half_extent { 0.0F };
  glm::vec4 legacy_rect_center_half_extent { 0.0F };
  glm::vec4 split_and_full_depth_range { 0.0F };
  glm::vec4 shading_margins { 0.0F };
  std::uint32_t target_array_slice { 0U };
  std::uint32_t flags { 0U };
  std::uint32_t _pad0 { 0U };
  std::uint32_t _pad1 { 0U };

  auto operator==(const ConventionalShadowReceiverAnalysisJob&) const -> bool
    = default;
};

static_assert(std::is_standard_layout_v<ConventionalShadowReceiverAnalysisJob>);
static_assert(sizeof(ConventionalShadowReceiverAnalysisJob) == 144U);
static_assert(
  sizeof(ConventionalShadowReceiverAnalysisJob) % alignof(glm::vec4) == 0U);

//! Final per-raster-job receiver-analysis record produced by `CSM-2`.
/*!
 `raw_xy_min_max` and `raw_depth_and_dilation.xy` describe the tight visible
 receiver footprint gathered from actual main-view samples. The remaining fields
 keep the full cascade-fit and legacy receiver-sphere references needed for
 baseline comparison and later mask construction phases.
*/
struct alignas(16) ConventionalShadowReceiverAnalysis {
  glm::vec4 raw_xy_min_max { 0.0F };
  glm::vec4 raw_depth_and_dilation { 0.0F };
  glm::vec4 full_rect_center_half_extent { 0.0F };
  glm::vec4 legacy_rect_center_half_extent { 0.0F };
  glm::vec4 full_depth_and_area_ratios { 0.0F };
  float full_depth_ratio { 0.0F };
  std::uint32_t sample_count { 0U };
  std::uint32_t target_array_slice { 0U };
  std::uint32_t flags { 0U };

  auto operator==(const ConventionalShadowReceiverAnalysis&) const -> bool
    = default;
};

static_assert(std::is_standard_layout_v<ConventionalShadowReceiverAnalysis>);
static_assert(sizeof(ConventionalShadowReceiverAnalysis) == 96U);
static_assert(
  sizeof(ConventionalShadowReceiverAnalysis) % alignof(glm::vec4) == 0U);

//! Per-view CPU publication for the authoritative conventional receiver jobs.
struct ConventionalShadowReceiverAnalysisPlan {
  std::span<const ConventionalShadowReceiverAnalysisJob> jobs {};
};

} // namespace oxygen::renderer
