//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <chrono>
#include <string>
#include <vector>

namespace oxygen::content::import::tool {

struct JobViewModel {
  std::string status;
  std::string phase;
  std::string item_kind;
  std::string item_name;
  float progress = 0.0f;
  std::vector<std::string> recent_logs;
  std::chrono::seconds elapsed { 0 };
  bool completed = false;
  bool success = false;
};

} // namespace oxygen::content::import::tool
