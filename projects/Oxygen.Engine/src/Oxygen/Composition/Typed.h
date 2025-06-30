//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <concepts>
#include <cstdint>

namespace oxygen {

using TypeId = uint64_t;
auto constexpr kInvalidTypeId = static_cast<TypeId>(-1);

//! Concept: T must have static ClassTypeId() returning oxygen::TypeId
template <typename T>
concept IsTyped = requires {
  { T::ClassTypeId() } -> std::same_as<oxygen::TypeId>;
};

} // namespace oxygen
