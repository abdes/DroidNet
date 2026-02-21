//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string>

#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Physics/api_export.h>

namespace oxygen::physics {

using CollisionLayer = NamedType<uint32_t, struct CollisionLayerTag,
  // clang-format off
  Comparable,
  Printable,
  Hashable>; // clang-format on

using CollisionMask = NamedType<uint32_t, struct CollisionMaskTag,
  // clang-format off
  Comparable,
  Printable,
  Hashable>; // clang-format on

constexpr CollisionLayer kCollisionLayerDefault { 0 };
constexpr CollisionMask kCollisionMaskAll { ~0u };

OXGN_PHYS_NDAPI auto to_string(CollisionLayer value) -> std::string;
OXGN_PHYS_NDAPI auto to_string(CollisionMask value) -> std::string;

} // namespace oxygen::physics
