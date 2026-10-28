//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <expected>
#include <memory>

#include <Oxygen/Clap/CommandLineContext.h>
#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Tools/Common/ToolCommand.h>
#include <Oxygen/Cooker/Tools/ImportTool/GlobalOptions.h>

namespace oxygen::content::import::tool {

class ImportCommand;

//! Service settings given on the command line, which override a command's.
struct ServiceOverrides {
  bool thread_pool_size = false;
  bool concurrency = false;
};

//! Reads `--thread-pool-size` and `--concurrency` into `service_config`.
/*!
 An invalid `--concurrency` specification is reported on the global writer and
 fails with `ExitCode::kUsage`.
*/
[[nodiscard]] auto ApplyServiceConfigOverrides(
  const clap::CommandLineContext& context, const GlobalOptions& global_options,
  AsyncImportService::Config& service_config, ServiceOverrides& overrides)
  -> toolkit::ToolResult;

//! Creates the command's import service, or the error of its configuration.
[[nodiscard]] auto CreateImportService(ImportCommand& active_command,
  const AsyncImportService::Config& service_config,
  const ServiceOverrides& overrides)
  -> std::expected<std::unique_ptr<AsyncImportService>, toolkit::ToolError>;

} // namespace oxygen::content::import::tool
