//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <concepts>

namespace oxygen::scene {

//! Concept defining requirements for enum types usable with SceneFlags.
/*!
 This concept ensures that enum types used with SceneFlags template have:
 - A kCount sentinel value indicating the number of enum values
 - kCount value convertible to std::size_t
 - Maximum of 12 flags (constraint based on 5 bits per flag in 64-bit storage)

 \tparam T The enum type to validate
*/
template <typename T>
concept SceneFlagEnum = std::is_enum_v<T> && requires {
  T::kCount; // Must have a Count sentinel value
  { static_cast<std::size_t>(T::kCount) } -> std::convertible_to<std::size_t>;
} && static_cast<std::size_t>(T::kCount) <= 12; // Maximum 12 flags with 5 bits
                                                // each

} // namespace oxygen::scene
