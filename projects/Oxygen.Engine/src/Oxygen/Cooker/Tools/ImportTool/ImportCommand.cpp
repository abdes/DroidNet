//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <expected>
#include <system_error>

#include <Oxygen/Cooker/Tools/Common/ToolCommand.h>
#include <Oxygen/Cooker/Tools/ImportTool/ImportCommand.h>

namespace oxygen::content::import::tool {

auto ExitStatusFromError(const std::error_code& error) -> int
{
  if (error == std::errc::invalid_argument) {
    return static_cast<int>(toolkit::ExitCode::kUsage);
  }
  return static_cast<int>(toolkit::ExitCode::kFailed);
}

auto ImportCommand::Run(toolkit::ToolContext& /*context*/)
  -> toolkit::ToolResult
{
  if (const auto result = Execute(); !result) {
    return std::unexpected(
      toolkit::ToolError::WithCode(ExitStatusFromError(result.error())));
  }
  return {};
}

} // namespace oxygen::content::import::tool
