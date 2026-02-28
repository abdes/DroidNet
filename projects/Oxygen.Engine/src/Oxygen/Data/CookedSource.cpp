//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Data/CookedSource.h>

namespace oxygen::data {

auto to_string(const CookedSourceKind value) noexcept -> std::string_view
{
  switch (value) {
  case CookedSourceKind::kLooseCooked:
    return "LooseCooked";
  case CookedSourceKind::kPak:
    return "Pak";
  }

  return "__NotSupported__";
}

} // namespace oxygen::data
