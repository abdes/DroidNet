//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <string_view>

namespace oxygen::data {

namespace detail {

  [[nodiscard]] constexpr auto IsMountNameStart(const char c) noexcept -> bool
  {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
  }

  [[nodiscard]] constexpr auto IsMountNameChar(const char c) noexcept -> bool
  {
    return IsMountNameStart(c) || (c >= '0' && c <= '9') || c == '-';
  }

} // namespace detail

//! Whether `mount` is spelled as a mount name: an identifier (an ASCII letter
//! or `_`, then letters, digits, `_` or `-`), after an optional leading `.`
//! that marks a mount of derived content.
/*!
 This is the spelling rule of the first segment of every virtual path, so a
 pak catalog names only mounts a virtual path can name. Length bounds belong
 to each owner: a virtual path segment's, a catalog field's.
*/
[[nodiscard]] constexpr auto IsMountNameSpelling(
  const std::string_view mount) noexcept -> bool
{
  const auto name = mount.starts_with('.') ? mount.substr(1) : mount;
  return !name.empty() && detail::IsMountNameStart(name.front())
    && std::ranges::all_of(name.substr(1), detail::IsMountNameChar);
}

} // namespace oxygen::data
