//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <span>

#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex::environment::internal {

inline constexpr std::uint32_t kIblBrdfWidth = 128U;
inline constexpr std::uint32_t kIblBrdfHeight = 32U;
inline constexpr std::uint32_t kIblBrdfSamples = 128U;
using IblBrdfTexel = std::array<std::uint16_t, 2>;

//! Immutable RG16Unorm split-sum data, generated once and uploaded per device.
[[nodiscard]] OXGN_VRTX_API auto GetIblBrdfLookup()
  -> std::span<const IblBrdfTexel, kIblBrdfWidth * kIblBrdfHeight>;

} // namespace oxygen::vortex::environment::internal
