//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <type_traits>
#include <utility>

#include <fmt/format.h>

#include <Oxygen/Base/NoStd.h>

namespace oxygen::content::toolkit {

//! An enum value as a content tool shows it: its name, then its numeric
//! value, such as `RigidBody(1)`, so a value without a name still shows what
//! it holds.
template <typename E>
  requires std::is_enum_v<E>
[[nodiscard]] auto EnumString(const E value) -> std::string
{
  return fmt::format(
    "{}({})", nostd::to_string(value), std::to_underlying(value));
}

} // namespace oxygen::content::toolkit
