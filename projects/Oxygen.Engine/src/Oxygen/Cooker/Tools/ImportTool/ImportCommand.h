//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <expected>
#include <system_error>

#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Tools/Common/ToolCommand.h>

namespace oxygen::content::import::tool {

//! The process exit status of a failed import command.
/*!
 An invalid argument, input or configuration is `ExitCode::kUsage`; anything
 else is `ExitCode::kFailed`.
*/
[[nodiscard]] auto ExitStatusFromError(const std::error_code& error) -> int;

//! A command of the import tool.
/*!
 Commands report their progress and failures through the run's message writer,
 so a failed `Execute` carries only the error whose category picks the exit
 status.
*/
class ImportCommand : public toolkit::ToolCommand {
public:
  //! Runs the command.
  [[nodiscard]] virtual auto Execute()
    -> std::expected<void, std::error_code> = 0;

  //! Whether this command schedules work through the asynchronous importer.
  [[nodiscard]] virtual auto RequiresImportService() const noexcept -> bool
  {
    return true;
  }

  //! Prepare import service configuration for this command.
  [[nodiscard]] virtual auto PrepareImportServiceConfig()
    -> std::expected<AsyncImportService::Config, std::error_code>
  {
    AsyncImportService::Config config {};
    config.max_in_flight_jobs = 1U;
    return config;
  }

  [[nodiscard]] auto Run(toolkit::ToolContext& context)
    -> toolkit::ToolResult final;
};

} // namespace oxygen::content::import::tool
