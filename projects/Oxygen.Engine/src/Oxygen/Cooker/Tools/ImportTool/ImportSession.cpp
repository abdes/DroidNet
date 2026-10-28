//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <expected>
#include <span>
#include <utility>

#include "GlobalOptions.h"
#include "ServiceConfig.h"

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Clap/CommandLineContext.h>
#include <Oxygen/Cooker/Tools/Common/ToolCommand.h>
#include <Oxygen/Cooker/Tools/ImportTool/BatchCommand.h>
#include <Oxygen/Cooker/Tools/ImportTool/ConsoleMessageWriter.h>
#include <Oxygen/Cooker/Tools/ImportTool/ImportCommand.h>
#include <Oxygen/Cooker/Tools/ImportTool/ImportSession.h>
#include <Oxygen/Cooker/Tools/ImportTool/StopSignal.h>

namespace oxygen::content::import::tool {

namespace {

  auto ApplyLoggingOptions(const GlobalOptions& options) -> void
  {
    // Control colored output to stderr according to --no-color
    loguru::g_colorlogtostderr = !options.no_color;

    // When running with an interactive TUI we want to avoid interleaving
    // log messages on stderr with the curses-style UI. Disable stderr logging
    // completely when the TUI is enabled (i.e., --no-tui is not set).
    if (!options.no_tui) {
      loguru::g_log_to_stderr = false;
    }
  }

} // namespace

ImportSession::ImportSession(GlobalOptions& global_options,
  BatchCommand& batch_command, AsyncImportService::Config service_config,
  const std::span<ImportCommand* const> commands)
  : global_options_(&global_options)
  , batch_command_(&batch_command)
  , commands_(commands.begin(), commands.end())
  , service_config_(std::move(service_config))
{
}

auto ImportSession::FindImportCommand(const toolkit::ToolCommand& command) const
  -> ImportCommand*
{
  const auto found = std::ranges::find_if(commands_, [&](auto* candidate) {
    return static_cast<const toolkit::ToolCommand*>(candidate) == &command;
  });
  return found != commands_.end() ? *found : nullptr;
}

ImportSession::~ImportSession() = default;

auto ImportSession::Begin(const clap::CommandLineContext& parsed,
  toolkit::ToolCommand& command, toolkit::ToolContext& context)
  -> toolkit::ToolResult
{
  auto& options = *global_options_;
  options.command_line = context.CommandLine();
  options.parsed_options = observer_ptr { &parsed.ovm };
  ApplyLoggingOptions(options);

  writer_ = CreateMessageWriter(options, context.Streams());
  options.writer = observer_ptr<IMessageWriter> { writer_.get() };

  if (auto applied
    = ApplyServiceConfigOverrides(parsed, options, service_config_, overrides_);
    !applied) {
    Release();
    return applied;
  }
  batch_command_->SetServiceConfigOverrides(
    &service_config_, overrides_.concurrency);

  // The built-in commands, such as `versions`, do not import.
  auto* import_command = FindImportCommand(command);
  if (import_command == nullptr || !import_command->RequiresImportService()) {
    return {};
  }

  auto created
    = CreateImportService(*import_command, service_config_, overrides_);
  if (!created) {
    Release();
    return std::unexpected(created.error());
  }
  service_ = std::move(*created);
  options.import_service = observer_ptr<AsyncImportService> { service_.get() };
  ResetStopState();
  stop_watcher_.emplace(StartStopWatcher(service_.get(), options.writer));
  return {};
}

auto ImportSession::End() -> void
{
  if (service_ != nullptr) {
    service_->RequestShutdown();
    service_->Stop();
  }
  Release();
}

auto ImportSession::Release() -> void
{
  stop_watcher_.reset();
  service_.reset();
  global_options_->import_service = {};
  global_options_->writer = {};
  writer_.reset();
}

} // namespace oxygen::content::import::tool
