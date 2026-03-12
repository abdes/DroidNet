//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

namespace oxygen::renderer {

enum class VirtualShadowPageFlag : std::uint32_t {
  kAllocated = (1U << 0U),
  kDynamicUncached = (1U << 1U),
  kStaticUncached = (1U << 2U),
  kDetailGeometry = (1U << 3U),
  kUsedThisFrame = (1U << 4U),
};

[[nodiscard]] constexpr auto ToMask(const VirtualShadowPageFlag flag)
  -> std::uint32_t
{
  return static_cast<std::uint32_t>(flag);
}

[[nodiscard]] constexpr auto MakeVirtualShadowPageFlags(
  const bool allocated = false, const bool dynamic_uncached = false,
  const bool static_uncached = false, const bool detail_geometry = false,
  const bool used_this_frame = false) -> std::uint32_t
{
  return (allocated ? ToMask(VirtualShadowPageFlag::kAllocated) : 0U)
    | (dynamic_uncached ? ToMask(VirtualShadowPageFlag::kDynamicUncached) : 0U)
    | (static_uncached ? ToMask(VirtualShadowPageFlag::kStaticUncached) : 0U)
    | (detail_geometry ? ToMask(VirtualShadowPageFlag::kDetailGeometry) : 0U)
    | (used_this_frame ? ToMask(VirtualShadowPageFlag::kUsedThisFrame) : 0U);
}

[[nodiscard]] constexpr auto HasVirtualShadowPageFlag(
  const std::uint32_t page_flags, const VirtualShadowPageFlag flag) -> bool
{
  return (page_flags & ToMask(flag)) != 0U;
}

} // namespace oxygen::renderer
