//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <Oxygen/Vortex/Types/LightingIndices.h>

namespace oxygen::vortex {

inline constexpr std::uint32_t kShadowProjectionNone = 0U;
inline constexpr std::uint32_t kShadowProjectionCascaded2D = 1U;
inline constexpr std::uint32_t kShadowProjectionLocalCube = 2U;
inline constexpr std::uint32_t kShadowProjectionLocalProjected2D = 3U;

inline constexpr std::uint32_t kShadowCoverageNoRequest = 0U;
inline constexpr std::uint32_t kShadowCoverageNoInfluence = 1U;
inline constexpr std::uint32_t kShadowCoverageComplete = 2U;
inline constexpr std::uint32_t kShadowCoverageOutsideAuthored = 3U;

//! Per-view association from an immutable light selection to its shadow record.
struct alignas(16) LightShadowReference {
  std::uint32_t projection_kind { kShadowProjectionNone };
  ShadowRecordIndex record_index { kInvalidShadowRecordIndex };
  LightSelectionIndex selection_index { kInvalidLightSelectionIndex };
  std::uint32_t coverage_state { kShadowCoverageNoRequest };
};

// NOLINTBEGIN(*-magic-numbers)
static_assert(std::is_standard_layout_v<LightShadowReference>);
static_assert(std::is_trivially_copyable_v<LightShadowReference>);
static_assert(alignof(LightShadowReference) == 16U);
static_assert(sizeof(LightShadowReference) == 16U);
static_assert(offsetof(LightShadowReference, projection_kind) == 0U);
static_assert(offsetof(LightShadowReference, record_index) == 4U);
static_assert(offsetof(LightShadowReference, selection_index) == 8U);
static_assert(offsetof(LightShadowReference, coverage_state) == 12U);
// NOLINTEND(*-magic-numbers)

} // namespace oxygen::vortex
