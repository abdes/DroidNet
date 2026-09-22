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

//! A directional light's contiguous range in the view's cascade record array.
struct alignas(16) DirectionalShadowRecord {
  LightSelectionIndex selection_index { kInvalidLightSelectionIndex };
  ShadowCascadeIndex first_cascade { kInvalidShadowCascadeIndex };
  std::uint32_t cascade_count { 0U };
  std::uint32_t reserved { 0U };
};

// NOLINTBEGIN(*-magic-numbers)
static_assert(std::is_standard_layout_v<DirectionalShadowRecord>);
static_assert(std::is_trivially_copyable_v<DirectionalShadowRecord>);
static_assert(alignof(DirectionalShadowRecord) == 16U);
static_assert(sizeof(DirectionalShadowRecord) == 16U);
static_assert(offsetof(DirectionalShadowRecord, selection_index) == 0U);
static_assert(offsetof(DirectionalShadowRecord, first_cascade) == 4U);
static_assert(offsetof(DirectionalShadowRecord, cascade_count) == 8U);
static_assert(offsetof(DirectionalShadowRecord, reserved) == 12U);
// NOLINTEND(*-magic-numbers)

} // namespace oxygen::vortex
