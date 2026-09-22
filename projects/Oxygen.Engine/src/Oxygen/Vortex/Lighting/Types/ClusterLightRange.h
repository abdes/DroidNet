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

//! GPU cluster range; an empty cluster has both members zero.
struct ClusterLightRange {
  LightListOffset offset { 0U };
  std::uint32_t count { 0U };
};

// NOLINTBEGIN(*-magic-numbers)
static_assert(std::is_standard_layout_v<ClusterLightRange>);
static_assert(std::is_trivially_copyable_v<ClusterLightRange>);
static_assert(alignof(ClusterLightRange) == 4U);
static_assert(sizeof(ClusterLightRange) == 8U);
static_assert(offsetof(ClusterLightRange, offset) == 0U);
static_assert(offsetof(ClusterLightRange, count) == 4U);
// NOLINTEND(*-magic-numbers)

} // namespace oxygen::vortex
