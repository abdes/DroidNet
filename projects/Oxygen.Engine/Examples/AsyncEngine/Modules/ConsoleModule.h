//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <deque>
#include <functional>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Logging.h>

#include "../IEngineModule.h"
#include <Oxygen/Engine/FrameContext.h>

using namespace std::chrono_literals;

namespace oxygen::engine::asyncsim {

//! Engine console module for command processing and development tools
/*!
 This module provides a command-line interface for engine development:
 - Asynchronous command processing with background execution
 - Built-in commands for debugging and engine control
 - Command history and auto-completion support
 - Background maintenance of console services
 - Integration with engine input and async work phases
 */
class ConsoleModule final : public EngineModuleBase {
public:
  ConsoleModule();
  ~ConsoleModule() override = default;

  OXYGEN_MAKE_NON_COPYABLE(ConsoleModule)
  OXYGEN_MAKE_NON_MOVABLE(ConsoleModule)

  // === LIFECYCLE MANAGEMENT ===

  auto Initialize(AsyncEngineSimulator& engine) -> co::Co<> override;
  auto Shutdown() -> co::Co<> override;

  // === FRAME PHASE IMPLEMENTATIONS ===

  //! Input phase - process console input commands
  auto OnInput(FrameContext& context) -> co::Co<> override;

  //! Async work phase - execute commands asynchronously
  auto OnAsyncWork(FrameContext& context) -> co::Co<> override;

  //! Detached work phase - background console maintenance
  auto OnDetachedWork(FrameContext& context) -> co::Co<> override;

  // === PUBLIC API ===

  //! Queue a command for asynchronous execution
  void QueueCommand(const std::string& command);

  //! Check if quit was requested via console command
  [[nodiscard]] bool IsQuitRequested() const noexcept
  {
    return quit_requested_;
  }

  //! Console statistics for monitoring
  struct ConsoleStats {
    uint32_t commands_executed { 0 };
    uint32_t pending_commands { 0 };
    uint32_t history_entries { 0 };
    uint32_t maintenance_cycles { 0 };
  };

  //! Get current console statistics
  [[nodiscard]] auto GetConsoleStats() const -> ConsoleStats
  {
    return { commands_executed_,
      static_cast<uint32_t>(pending_commands_.size()),
      static_cast<uint32_t>(command_history_.size()),
      background_maintenance_cycles_ };
  }

private:
  using CommandHandler = std::function<void(const std::vector<std::string>&)>;

  //! Register a command with its handler
  void RegisterCommand(const std::string& name, CommandHandler handler);

  //! Execute a parsed command line
  void ExecuteCommand(const std::string& command_line);

  std::unordered_map<std::string, CommandHandler> commands_;
  std::deque<std::string> pending_commands_;
  std::vector<std::string> command_history_;

  uint32_t commands_executed_ { 0 };
  uint32_t background_maintenance_cycles_ { 0 };
  bool quit_requested_ { false };
};

} // namespace oxygen::engine::asyncsim
