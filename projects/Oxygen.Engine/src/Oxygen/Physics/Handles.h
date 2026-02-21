//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string>
#include <limits>

#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Physics/api_export.h>

namespace oxygen::physics {

using WorldId = NamedType<uint32_t, struct WorldIdTag,
  // clang-format off
  Comparable,
  Printable,
  Hashable>; // clang-format on
constexpr WorldId kInvalidWorldId { std::numeric_limits<uint32_t>::max() };

using BodyId = NamedType<uint32_t, struct BodyIdTag,
  // clang-format off
  Comparable,
  Printable,
  Hashable>; // clang-format on
constexpr BodyId kInvalidBodyId { std::numeric_limits<uint32_t>::max() };

using CharacterId = NamedType<uint32_t, struct CharacterIdTag,
  // clang-format off
  Comparable,
  Printable,
  Hashable>; // clang-format on
constexpr CharacterId kInvalidCharacterId { std::numeric_limits<uint32_t>::max() };

OXGN_PHYS_NDAPI auto to_string(WorldId value) -> std::string;
OXGN_PHYS_NDAPI auto to_string(BodyId value) -> std::string;
OXGN_PHYS_NDAPI auto to_string(CharacterId value) -> std::string;

[[nodiscard]] inline auto IsValid(WorldId value) noexcept -> bool
{
  return value != kInvalidWorldId;
}

[[nodiscard]] inline auto IsValid(BodyId value) noexcept -> bool
{
  return value != kInvalidBodyId;
}

[[nodiscard]] inline auto IsValid(CharacterId value) noexcept -> bool
{
  return value != kInvalidCharacterId;
}

} // namespace oxygen::physics
