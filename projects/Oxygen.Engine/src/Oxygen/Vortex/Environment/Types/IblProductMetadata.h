//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace oxygen::vortex::environment {

inline constexpr std::uint32_t kIblProductFinite = 1U << 0U;
inline constexpr std::uint32_t kIblProductComplete = 1U << 1U;

//! GPU-produced range and readiness for one immutable product generation.
struct IblProductMetadata {
  float source_radiance_scale { 1.0F };
  float average_brightness { 0.0F };
  std::uint32_t processing_flags { 0U };
  std::uint32_t product_revision { 0U };
};

static_assert(std::is_standard_layout_v<IblProductMetadata>);
static_assert(sizeof(IblProductMetadata) == 16);
static_assert(offsetof(IblProductMetadata, source_radiance_scale) == 0);
static_assert(offsetof(IblProductMetadata, average_brightness) == 4);
static_assert(offsetof(IblProductMetadata, processing_flags) == 8);
static_assert(offsetof(IblProductMetadata, product_revision) == 12);

} // namespace oxygen::vortex::environment
