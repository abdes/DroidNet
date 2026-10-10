//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include <Oxygen/Data/GeometryIndices.h>

namespace oxygen::scene {
//! Invariant: LOD 0 is the finest quality. Index i denotes the boundary
//! between LOD i and LOD i+1. Increasing the LOD index moves to coarser
//! representations.
struct FixedPolicy {
  static constexpr data::LodIndex kFinest {};
  data::LodIndex index { kFinest };
  // Clamp to existing LOD count
  [[nodiscard]] auto Clamp(std::size_t lod_count) const noexcept
    -> data::LodIndex;
};

struct DistancePolicy {
  std::vector<float> thresholds; // boundaries between i and i+1
  float hysteresis_ratio { 0.1f }; // symmetric band around boundary
  // Ensure thresholds are non-decreasing and clamp hysteresis into [0, 0.99]
  void NormalizeThresholds() noexcept;
  // Base selection without hysteresis
  [[nodiscard]] auto SelectBase(float normalized_distance,
    std::size_t lod_count) const noexcept -> data::LodIndex;
  // Apply symmetric hysteresis around the boundary between last and base
  [[nodiscard]] auto ApplyHysteresis(std::optional<data::LodIndex> current,
    data::LodIndex base, float normalized_distance,
    std::size_t lod_count) const noexcept -> data::LodIndex;
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
  [[nodiscard]] auto SelectBase(float sse, std::size_t lod_count) const noexcept
    -> data::LodIndex;
  // Apply directional hysteresis using enter/exit arrays
  [[nodiscard]] auto ApplyHysteresis(std::optional<data::LodIndex> current,
    data::LodIndex base, float sse, std::size_t lod_count) const noexcept
    -> data::LodIndex;
};

} // namespace oxygen::scene
