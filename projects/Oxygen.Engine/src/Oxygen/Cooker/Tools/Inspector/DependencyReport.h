//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string>

#include <Oxygen/Cooker/Tools/Common/ToolCommand.h>

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
[[nodiscard]] auto RunDependencyReport(const DependencyReportOptions& options)
  -> toolkit::ToolResult;

} // namespace oxygen::content::inspection
