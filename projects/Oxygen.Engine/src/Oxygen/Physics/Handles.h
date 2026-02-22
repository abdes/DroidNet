//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <limits>
#include <string>

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
constexpr CharacterId kInvalidCharacterId {
  std::numeric_limits<uint32_t>::max()
};

using ShapeId = NamedType<uint32_t, struct ShapeIdTag,
  // clang-format off
  Comparable,
  Printable,
  Hashable>; // clang-format on
constexpr ShapeId kInvalidShapeId { std::numeric_limits<uint32_t>::max() };

using ShapeInstanceId = NamedType<uint32_t, struct ShapeInstanceIdTag,
  // clang-format off
  Comparable,
  Printable,
  Hashable>; // clang-format on
constexpr ShapeInstanceId kInvalidShapeInstanceId {
  std::numeric_limits<uint32_t>::max()
};

using AreaId = NamedType<uint32_t, struct AreaIdTag,
  // clang-format off
  Comparable,
  Printable,
  Hashable>; // clang-format on
constexpr AreaId kInvalidAreaId { std::numeric_limits<uint32_t>::max() };

using JointId = NamedType<uint32_t, struct JointIdTag,
  // clang-format off
  Comparable,
  Printable,
  Hashable>; // clang-format on
constexpr JointId kInvalidJointId { std::numeric_limits<uint32_t>::max() };

using AggregateId = NamedType<uint32_t, struct AggregateIdTag,
  // clang-format off
  Comparable,
  Printable,
  Hashable>; // clang-format on
constexpr AggregateId kInvalidAggregateId {
  std::numeric_limits<uint32_t>::max()
};

OXGN_PHYS_NDAPI auto to_string(WorldId value) -> std::string;
OXGN_PHYS_NDAPI auto to_string(BodyId value) -> std::string;
OXGN_PHYS_NDAPI auto to_string(CharacterId value) -> std::string;
OXGN_PHYS_NDAPI auto to_string(ShapeId value) -> std::string;
OXGN_PHYS_NDAPI auto to_string(ShapeInstanceId value) -> std::string;
OXGN_PHYS_NDAPI auto to_string(AreaId value) -> std::string;
OXGN_PHYS_NDAPI auto to_string(JointId value) -> std::string;
OXGN_PHYS_NDAPI auto to_string(AggregateId value) -> std::string;

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

[[nodiscard]] inline auto IsValid(ShapeId value) noexcept -> bool
{
  return value != kInvalidShapeId;
}

[[nodiscard]] inline auto IsValid(ShapeInstanceId value) noexcept -> bool
{
  return value != kInvalidShapeInstanceId;
}

[[nodiscard]] inline auto IsValid(AreaId value) noexcept -> bool
{
  return value != kInvalidAreaId;
}

[[nodiscard]] inline auto IsValid(JointId value) noexcept -> bool
{
  return value != kInvalidJointId;
}

[[nodiscard]] inline auto IsValid(AggregateId value) noexcept -> bool
{
  return value != kInvalidAggregateId;
}

} // namespace oxygen::physics
