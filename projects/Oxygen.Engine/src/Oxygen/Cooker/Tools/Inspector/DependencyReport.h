//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string>

namespace oxygen::clap {
class Command;
}

namespace oxygen::content::inspection {

struct DependencyReportOptions {
  std::string cooked_root;
  std::string output;
};

auto BuildDependencyCommand(DependencyReportOptions& options)
  -> std::shared_ptr<clap::Command>;
auto RunDependencyReport(const DependencyReportOptions& options) -> int;

} // namespace oxygen::content::inspection
