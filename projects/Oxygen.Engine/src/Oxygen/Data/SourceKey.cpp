//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Data/SourceKey.h>

#include <iomanip>
#include <sstream>

namespace oxygen::data {

auto to_string(const SourceKey& key) -> std::string
{
  std::ostringstream oss;
  oss << std::hex << std::setfill('0');
  for (const auto& b : key.get()) {
    oss << std::setw(2) << static_cast<int>(b);
  }
  return oss.str();
}

} // namespace oxygen::data
