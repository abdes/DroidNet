//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace oxygen::vortex {

//! GPU readiness values; failures remain sticky through finalization.
inline constexpr std::uint32_t kLightGridBuildPending = 0U;
inline constexpr std::uint32_t kLightGridBuildValid = 1U;
inline constexpr std::uint32_t kLightGridBuildFailed = 2U;

inline constexpr std::uint32_t kLightGridReasonNone = 0U;
inline constexpr std::uint32_t kLightGridReasonInvalidBounds = 1U;
inline constexpr std::uint32_t kLightGridReasonInvalidIndex = 2U;
inline constexpr std::uint32_t kLightGridReasonCapacity = 3U;
inline constexpr std::uint32_t kLightGridReasonGenerationMismatch = 4U;

//! GPU completion product. Each two-word quantity stores low word first.
struct alignas(16) LightGridBuildStatus {
  std::uint32_t state { kLightGridBuildPending };
  std::uint32_t reason { kLightGridReasonNone };
  std::uint32_t written_index_count { 0U };
  std::uint32_t fallback_cell_count { 0U };
  std::array<std::uint32_t, 2> required_index_count {};
  std::array<std::uint32_t, 2> selection_revision {};
};

// NOLINTBEGIN(*-magic-numbers)
static_assert(std::is_standard_layout_v<LightGridBuildStatus>);
static_assert(std::is_trivially_copyable_v<LightGridBuildStatus>);
static_assert(alignof(LightGridBuildStatus) == 16U);
static_assert(sizeof(LightGridBuildStatus) == 32U);
static_assert(offsetof(LightGridBuildStatus, state) == 0U);
static_assert(offsetof(LightGridBuildStatus, reason) == 4U);
static_assert(offsetof(LightGridBuildStatus, written_index_count) == 8U);
static_assert(offsetof(LightGridBuildStatus, fallback_cell_count) == 12U);
static_assert(offsetof(LightGridBuildStatus, required_index_count) == 16U);
static_assert(offsetof(LightGridBuildStatus, selection_revision) == 24U);
// NOLINTEND(*-magic-numbers)

} // namespace oxygen::vortex
