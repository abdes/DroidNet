//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Vortex/Types/Generated.DrawContracts.h>

namespace oxygen::vortex {

enum class DrawPrimitiveFlagBits : uint32_t {
  kNone = 0U,
  kStaticShadowCaster = 1U << 0U,
  kMainViewVisible = 1U << 1U,
  kDisableShadowReception = 1U << 2U,
};

[[nodiscard]] constexpr auto HasAnyDrawPrimitiveFlag(
  const uint32_t flags, const DrawPrimitiveFlagBits bits) noexcept -> bool
{
  return (flags & static_cast<uint32_t>(bits)) != 0U;
}

} // namespace oxygen::vortex
