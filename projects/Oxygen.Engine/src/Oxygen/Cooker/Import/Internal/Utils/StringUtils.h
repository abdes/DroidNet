//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <span>
#include <string>
#include <string_view>

namespace oxygen::content::import::util {

//! Truncates UTF-8 at a character boundary and null-terminates the buffer.
inline auto TruncateAndNullTerminate(
  char* dst, const size_t dst_size, std::string_view s) -> void
{
  if (dst == nullptr || dst_size == 0) {
    return;
  }

  std::fill_n(dst, dst_size, '\0');
  auto copy_len = (std::min)(dst_size - 1, s.size());
  constexpr auto kUtf8PrefixMask = 0xc0U;
  constexpr auto kUtf8Continuation = 0x80U;
  while (copy_len > 0 && copy_len < s.size()
    && (static_cast<unsigned char>(s.at(copy_len)) & kUtf8PrefixMask)
      == kUtf8Continuation) {
    --copy_len;
  }
  std::copy_n(s.data(), copy_len, dst);
}

//! Truncates UTF-8 into the fixed-size field `dst` and null-terminates it.
inline auto TruncateAndNullTerminate(
  const std::span<char> dst, const std::string_view s) -> void
{
  TruncateAndNullTerminate(dst.data(), dst.size(), s);
}

} // namespace oxygen::content::import::util
