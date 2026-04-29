//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex {

enum class OcclusionFallbackReason : std::uint8_t {
  kNone,
  kStageDisabled,
  kNoPreparedFrame,
  kNoDraws,
  kNoCurrentFurthestHzb,
  kNoPreviousResults,
  kReadbackUnavailable,
  kCapacityOverflow,
};

struct OcclusionFrameResults {
  std::span<const std::uint8_t> visible_by_draw;
  std::uint32_t draw_count { 0U };
  bool valid { false };
  OcclusionFallbackReason fallback_reason {
    OcclusionFallbackReason::kStageDisabled
  };

  [[nodiscard]] constexpr auto IsDrawVisible(
    const std::uint32_t draw_index) const noexcept -> bool
  {
    return !valid || draw_index >= visible_by_draw.size()
      || visible_by_draw[draw_index] != 0U;
  }
};

struct OcclusionStats {
  std::uint32_t draw_count { 0U };
  std::uint32_t candidate_count { 0U };
  std::uint32_t submitted_count { 0U };
  std::uint32_t visible_count { 0U };
  std::uint32_t occluded_count { 0U };
  std::uint32_t overflow_visible_count { 0U };
  OcclusionFallbackReason fallback_reason {
    OcclusionFallbackReason::kStageDisabled
  };
  bool current_furthest_hzb_available { false };
  bool previous_results_valid { false };
  bool results_valid { false };
};

[[nodiscard]] constexpr auto to_string(
  const OcclusionFallbackReason reason) noexcept -> std::string_view
{
  switch (reason) {
  case OcclusionFallbackReason::kNone:
    return "None";
  case OcclusionFallbackReason::kStageDisabled:
    return "StageDisabled";
  case OcclusionFallbackReason::kNoPreparedFrame:
    return "NoPreparedFrame";
  case OcclusionFallbackReason::kNoDraws:
    return "NoDraws";
  case OcclusionFallbackReason::kNoCurrentFurthestHzb:
    return "NoCurrentFurthestHzb";
  case OcclusionFallbackReason::kNoPreviousResults:
    return "NoPreviousResults";
  case OcclusionFallbackReason::kReadbackUnavailable:
    return "ReadbackUnavailable";
  case OcclusionFallbackReason::kCapacityOverflow:
    return "CapacityOverflow";
  }
  return "__Unknown__";
}

[[nodiscard]] OXGN_VRTX_API auto MakeInvalidOcclusionFrameResults(
  OcclusionFallbackReason reason) noexcept -> OcclusionFrameResults;

} // namespace oxygen::vortex
