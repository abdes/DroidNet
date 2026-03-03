//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string_view>
#include <type_traits>

namespace oxygen::core::meta::physics {

enum class PhysicsBackend : uint8_t {
  kNone = 0,
  kJolt,
  kPhysX,
};

static_assert(sizeof(std::underlying_type_t<PhysicsBackend>) <= sizeof(uint8_t),
  "PhysicsBackend enum fit in `uint8_t`");

[[nodiscard]] constexpr auto to_string(const PhysicsBackend backend) noexcept
  -> std::string_view
{
  switch (backend) {
  case PhysicsBackend::kNone:
    return "none";
  case PhysicsBackend::kJolt:
    return "jolt";
  case PhysicsBackend::kPhysX:
    return "physx";
  }
  return "__Unknown__";
}

} // namespace oxygen::core::meta::physics
