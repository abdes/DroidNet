//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Console/Constants.h>
#include <Oxygen/Console/api_export.h>

namespace oxygen::console {

struct PrefixMatch {
  std::string_view text;
  std::string_view prefix;
};

class History final {
public:
  OXGN_CONS_API explicit History(size_t max_entries = kDefaultHistoryCapacity);
  ~History() = default;

  OXYGEN_DEFAULT_COPYABLE(History)
  OXYGEN_DEFAULT_MOVABLE(History)

  OXGN_CONS_API void Push(std::string entry);
  OXGN_CONS_API void Clear();

  OXGN_CONS_NDAPI auto Entries() const -> const std::vector<std::string>&;
  OXGN_CONS_NDAPI auto MaxEntries() const noexcept -> size_t;
  OXGN_CONS_NDAPI auto Size() const noexcept -> size_t;

private:
  size_t max_entries_ { 0 };
  std::vector<std::string> entries_;
};

OXGN_CONS_NDAPI auto StartsWithCaseInsensitive(PrefixMatch match) -> bool;

} // namespace oxygen::console
