//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <optional>
#include <span>
#include <thread>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Tools/Common/ToolCommand.h>
#include <Oxygen/Cooker/Tools/ImportTool/GlobalOptions.h>
#include <Oxygen/Cooker/Tools/ImportTool/MessageWriter.h>
#include <Oxygen/Cooker/Tools/ImportTool/ServiceConfig.h>

namespace oxygen::content::import::tool {

class BatchCommand;
class ImportCommand;

//! What one import tool run shares between its commands.
/*!
 Before the selected command runs it completes the global options, creates the
 message writer, applies the service overrides, and, for a command that
 schedules work, starts its import service and a watcher that cancels it on
 SIGINT or SIGTERM. Afterwards it shuts all of that down, in reverse.

 The global options hold non-owning pointers to the writer and the service for
 the duration of the run; commands must not keep them.
*/
class ImportSession final : public toolkit::ToolLifecycle {
public:
  //! `commands` are the tool's import commands; the others it runs, such as
  //! `versions`, do not import. They must outlive the session.
  ImportSession(GlobalOptions& global_options, BatchCommand& batch_command,
    AsyncImportService::Config service_config,
    std::span<ImportCommand* const> commands);
  ~ImportSession() override;
  OXYGEN_MAKE_NON_COPYABLE(ImportSession)
  OXYGEN_MAKE_NON_MOVABLE(ImportSession)

  [[nodiscard]] auto Begin(const clap::CommandLineContext& parsed,
    toolkit::ToolCommand& command, toolkit::ToolContext& context)
    -> toolkit::ToolResult override;
  auto End() -> void override;

private:
  //! The import command `command` is, or null for a built-in one. Compares
  //! identity: Oxygen builds without RTTI.
  [[nodiscard]] auto FindImportCommand(
    const toolkit::ToolCommand& command) const -> ImportCommand*;
  auto Release() -> void;

  observer_ptr<GlobalOptions> global_options_;
  observer_ptr<BatchCommand> batch_command_;
  std::vector<ImportCommand*> commands_;
  AsyncImportService::Config service_config_;
  ServiceOverrides overrides_ {};
  std::unique_ptr<IMessageWriter> writer_;
  std::unique_ptr<AsyncImportService> service_;
  std::optional<std::jthread> stop_watcher_;
};

} // namespace oxygen::content::import::tool
