//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <vector>

namespace oxygen::scene {
//! Invariant: LOD 0 is the finest quality. Index i denotes the boundary
//! between LOD i and LOD i+1. Increasing the LOD index moves to coarser
//! representations.
struct FixedPolicy {
  static constexpr std::size_t kFinest = 0;
  std::size_t index { kFinest };
  // Clamp to existing LOD count
  std::size_t Clamp(std::size_t lod_count) const noexcept;
};

struct DistancePolicy {
  std::vector<float> thresholds; // boundaries between i and i+1
  float hysteresis_ratio { 0.1f }; // symmetric band around boundary
  // Ensure thresholds are non-decreasing and clamp hysteresis into [0, 0.99]
  void NormalizeThresholds() noexcept;
  // Base selection without hysteresis
  std::size_t SelectBase(
    float normalized_distance, std::size_t lod_count) const noexcept;
  // Apply symmetric hysteresis around the boundary between last and base
  std::size_t ApplyHysteresis(std::optional<std::size_t> current,
    std::size_t base, float normalized_distance,
    std::size_t lod_count) const noexcept;
};

struct ScreenSpaceErrorPolicy {
  //! SSE threshold to enter a finer LOD (index decreases) when SSE increases.
  std::vector<float> enter_finer_sse;
  //! SSE threshold to enter a coarser LOD (index increases) when SSE
  //! decreases.
  std::vector<float> exit_coarser_sse;
  // Ensure arrays are non-decreasing
  void NormalizeMonotonic() noexcept;
  // Validate sizes: if provided, expect at least lod_count-1 boundaries
  [[nodiscard]] bool ValidateSizes(std::size_t lod_count) const noexcept;
  // Base selection without hysteresis
  std::size_t SelectBase(float sse, std::size_t lod_count) const noexcept;
  // Apply directional hysteresis using enter/exit arrays
  std::size_t ApplyHysteresis(std::optional<std::size_t> current,
    std::size_t base, float sse, std::size_t lod_count) const noexcept;
};

} // namespace oxygen::scene
