//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Vortex/Types/Generated.DrawContracts.h>

namespace oxygen::vortex {

//! What occlusion culling needs to know about one draw.
enum class DrawCullFlagBits : std::uint8_t {
  kOpaque = 1U << 0U,
  kMasked = 1U << 1U,
  kTransparent = 1U << 2U,
  kShadowCaster = 1U << 3U,
  kMainViewVisible = 1U << 4U,
  //! The box is a world-space AABB, not local to the draw's transform.
  kWorldSpaceBox = 1U << 5U,
  //! The draw is never culled (its bounds are not finite).
  kAlwaysVisible = 1U << 6U,
};

[[nodiscard]] constexpr auto ToUnderlying(const DrawCullFlagBits bit) noexcept
  -> std::uint32_t
{
  return static_cast<std::uint32_t>(bit);
}

} // namespace oxygen::vortex
