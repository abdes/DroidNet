//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <Oxygen/Core/Bindless/Types.h>

namespace oxygen::vortex::environment {

inline constexpr std::uint32_t kIblProductFinite = 1U << 0U;
inline constexpr std::uint32_t kIblProductComplete = 1U << 1U;
inline constexpr std::uint32_t kIblHalfCertificateComplete = 1U << 0U;

//! GPU-produced range and readiness for one immutable product generation.
struct IblProductMetadata {
  float source_radiance_scale { 1.0F };
  float average_brightness { 0.0F };
  std::uint32_t processing_flags { 0U };
  std::uint32_t product_revision { 0U };
  float maximum_half_gain { 0.0F };
  std::uint32_t precision_flags { 0U };
  std::uint32_t processed_half_srv { kInvalidBindlessIndex };
  std::uint32_t specular_half_srv { kInvalidBindlessIndex };
};

static_assert(std::is_standard_layout_v<IblProductMetadata>);
static_assert(sizeof(IblProductMetadata) == 32);
static_assert(offsetof(IblProductMetadata, source_radiance_scale) == 0);
static_assert(offsetof(IblProductMetadata, average_brightness) == 4);
static_assert(offsetof(IblProductMetadata, processing_flags) == 8);
static_assert(offsetof(IblProductMetadata, product_revision) == 12);
static_assert(offsetof(IblProductMetadata, maximum_half_gain) == 16);
static_assert(offsetof(IblProductMetadata, precision_flags) == 20);
static_assert(offsetof(IblProductMetadata, processed_half_srv) == 24);
static_assert(offsetof(IblProductMetadata, specular_half_srv) == 28);

} // namespace oxygen::vortex::environment
