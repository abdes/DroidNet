//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cctype>

#include <Oxygen/Console/History.h>

namespace oxygen::console {

History::History(const size_t max_entries)
  : max_entries_(std::max<size_t>(kMinHistoryCapacity, max_entries))
{
  entries_.reserve(max_entries_);
}

auto History::Entries() const -> const std::vector<std::string>&
{
  return entries_;
}

auto History::MaxEntries() const noexcept -> size_t { return max_entries_; }

auto History::Size() const noexcept -> size_t { return entries_.size(); }

void History::Push(std::string entry)
{
  if (entry.empty()) {
    return;
  }
  if (!entries_.empty() && entries_.back() == entry) {
    return;
  }

  if (entries_.size() >= max_entries_) {
    entries_.erase(entries_.begin());
  }
  entries_.push_back(std::move(entry));
}

void History::Clear() { entries_.clear(); }

auto StartsWithCaseInsensitive(const PrefixMatch match) -> bool
{
  if (match.prefix.size() > match.text.size()) {
    return false;
  }

  for (size_t i = 0; i < match.prefix.size(); ++i) {
    const auto lhs = static_cast<char>(
      std::tolower(static_cast<unsigned char>(match.text[i])));
    const auto rhs = static_cast<char>(
      std::tolower(static_cast<unsigned char>(match.prefix[i])));
    if (lhs != rhs) {
      return false;
    }
  }
  return true;
}

} // namespace oxygen::console
