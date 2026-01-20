//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Data/AssetKey.h>

namespace oxygen::content::import::util {

//! Truncates a string and null-terminates it into a fixed-size buffer.
inline auto TruncateAndNullTerminate(
  char* dst, const size_t dst_size, std::string_view s) -> void
{
  if (dst == nullptr || dst_size == 0) {
    return;
  }

  std::fill_n(dst, dst_size, '\0');
  const auto copy_len = (std::min)(dst_size - 1, s.size());
  std::copy_n(s.data(), copy_len, dst);
}

} // namespace oxygen::content::import::util
