//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "ConsoleModule.h"
#include <Oxygen/OxCo/ThreadPool.h>

namespace oxygen::examples::asyncsim {

ConsoleModule::ConsoleModule()
  : EngineModuleBase("Console",
      ModulePhases::Input | ModulePhases::AsyncWork
        | ModulePhases::DetachedWork,
      ModulePriorities::Normal)
{
}

auto ConsoleModule::Initialize(AsyncEngineSimulator& engine) -> co::Co<>
{
  // Store engine reference for later use
  engine_ = observer_ptr { &engine };

  LOG_F(INFO, "[Console] Initializing console system");

  // Register built-in commands
  RegisterCommand("help", [this](const std::vector<std::string>&) {
    LOG_F(INFO, "[Console] Available commands: help, quit, debug, stats");
  });

  RegisterCommand("quit", [this](const std::vector<std::string>&) {
    LOG_F(INFO, "[Console] Quit command executed");
    quit_requested_ = true;
  });

  RegisterCommand("debug", [this](const std::vector<std::string>& args) {
    bool enable = args.empty() || (args[0] == "on");
    LOG_F(INFO, "[Console] Debug overlay {}", enable ? "enabled" : "disabled");
  });

  RegisterCommand("stats", [this](const std::vector<std::string>&) {
    LOG_F(INFO, "[Console] Commands executed: {}, Pending: {}",
      commands_executed_, pending_commands_.size());
  });

  LOG_F(
    INFO, "[Console] Console initialized with {} commands", commands_.size());
  co_return;
}

auto ConsoleModule::OnInput(FrameContext& context) -> co::Co<>
{
  LOG_F(3, "[Console] Processing console input for frame {}",
    context.GetFrameIndex());

  // Simulate checking for console input
  if (context.GetFrameIndex() % 120 == 0) { // Simulate command every 2 seconds
    std::string simulated_command = "stats";
    QueueCommand(simulated_command);
    LOG_F(2, "[Console] Queued simulated command: {}", simulated_command);
  }

  co_return;
}

auto ConsoleModule::OnAsyncWork(FrameContext& context) -> co::Co<>
{
  LOG_F(3, "[Console] Processing async console work for frame {}",
    context.GetFrameIndex());

  // Process pending commands asynchronously
  if (!pending_commands_.empty()) {
    auto command = pending_commands_.front();
    pending_commands_.pop_front();

    co_await context.GetThreadPool()->Run(
      [this, command](auto /*cancel_token*/) {
        std::this_thread::sleep_for(100us); // Simulate command processing
        ExecuteCommand(command);
      });
  }

  co_return;
}

auto ConsoleModule::OnDetachedWork(FrameContext& context) -> co::Co<>
{
  // Background console services (log file management, command history, etc.)
  co_await context.GetThreadPool()->Run([this](auto /*cancel_token*/) {
    std::this_thread::sleep_for(20us); // Minimal background work

    // Simulate background console maintenance
    background_maintenance_cycles_++;

    // Cleanup old command history entries
    if (command_history_.size() > 100) {
      command_history_.erase(
        command_history_.begin(), command_history_.begin() + 50);
    }
  });

  co_return;
}

auto ConsoleModule::Shutdown() -> co::Co<>
{
  LOG_F(INFO, "[Console] Shutting down console system");

  // Process any remaining commands before shutdown
  while (!pending_commands_.empty()) {
    auto command = pending_commands_.front();
    pending_commands_.pop_front();
    ExecuteCommand(command);
  }

  LOG_F(INFO,
    "[Console] Console shutdown (executed {} commands, {} history entries)",
    commands_executed_, command_history_.size());
  co_return;
}

void ConsoleModule::QueueCommand(const std::string& command)
{
  pending_commands_.push_back(command);
}

void ConsoleModule::RegisterCommand(
  const std::string& name, CommandHandler handler)
{
  commands_[name] = std::move(handler);
}

void ConsoleModule::ExecuteCommand(const std::string& command_line)
{
  command_history_.push_back(command_line);

  // Parse command (simple space separation)
  std::vector<std::string> tokens;
  std::istringstream iss(command_line);
  std::string token;
  while (iss >> token) {
    tokens.push_back(token);
  }

  if (tokens.empty())
    return;

  const auto& command_name = tokens[0];
  std::vector<std::string> args(tokens.begin() + 1, tokens.end());

  auto it = commands_.find(command_name);
  if (it != commands_.end()) {
    it->second(args);
    commands_executed_++;
    LOG_F(2, "[Console] Executed command: {}", command_line);
  } else {
    LOG_F(WARNING, "[Console] Unknown command: {}", command_name);
  }
}

} // namespace oxygen::examples::asyncsim
