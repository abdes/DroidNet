//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <utility>

namespace oxygen::examples::ui {

//! The text of a filter field, matched ignoring ASCII case. An empty filter
//! matches every text. The content panel's filter fields match with it.
class TextFilter final {
public:
  explicit TextFilter(std::string needle)
    : needle_(std::move(needle))
  {
  }

  [[nodiscard]] auto IsEmpty() const noexcept -> bool
  {
    return needle_.empty();
  }

  //! True when `text` contains the filter.
  [[nodiscard]] auto Matches(const std::string_view text) const -> bool
  {
    if (needle_.empty()) {
      return true;
    }
    const auto lower = [](const char ch) -> int {
      return std::tolower(static_cast<unsigned char>(ch));
    };
    return !std::ranges::search(text, needle_, {}, lower, lower).empty();
  }

private:
  std::string needle_;
};

} // namespace oxygen::examples::ui
