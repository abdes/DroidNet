//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Headless/CommandList.h>

using oxygen::graphics::headless::CommandList;

CommandList::CommandList(const std::string_view name, const QueueRole role)
  : ::oxygen::graphics::CommandList(name, role)
{
  LOG_F(INFO, "Headless CommandList created: {} (role={})", name,
    nostd::to_string(role));
}

CommandList::~CommandList() = default;

auto CommandList::QueueCommand(std::shared_ptr<Command> cmd) -> void
{
  if (!IsRecording()) {
    throw std::runtime_error("QueueCommand called while not Recording");
  }
  if (!cmd) {
    LOG_F(WARNING, "QueueCommand: null command ignored");
    return;
  }
  commands_.push_back(std::move(cmd));
}

auto CommandList::DequeueCommand() -> std::optional<std::shared_ptr<Command>>
{
  if (!IsSubmitted()) {
    throw std::runtime_error(
      "DequeueCommand called while not Submitted/Executing");
  }
  if (commands_.empty()) {
    return std::nullopt;
  }
  auto cmd = commands_.front();
  commands_.pop_front();
  return cmd;
}

auto CommandList::StealCommands() -> std::deque<std::shared_ptr<Command>>
{
  // Immediately move out the recorded commands. The caller is responsible
  // for performing any required state transitions; stealing does not assert
  // on the command list state so that Submit() can steal prior to
  // OnSubmitted() being called by higher-level code.
  const auto before_count = commands_.size();
  LOG_F(INFO, "StealCommands: stealing {} commands from list '{}'",
    before_count, GetName());
  auto stolen = std::move(commands_);
  commands_.clear();
  return stolen;
}

auto CommandList::PeekNext() const -> std::optional<std::shared_ptr<Command>>
{
  if (commands_.empty()) {
    return std::nullopt;
  }
  return commands_.front();
}

auto CommandList::Clear() -> void
{
  // Allow clear while recording or closed; disallow while executing/submitted.
  if (IsSubmitted()) {
    throw std::runtime_error("Clear called while Submitted/Executing");
  }
  commands_.clear();
}

auto CommandList::OnBeginRecording() -> void
{
  ::oxygen::graphics::CommandList::OnBeginRecording();
  // No headless-specific action required here.
}

auto CommandList::OnEndRecording() -> void
{
  ::oxygen::graphics::CommandList::OnEndRecording();
  // No headless-specific action required here.
}

auto CommandList::OnSubmitted() -> void
{
  ::oxygen::graphics::CommandList::OnSubmitted();
  // Keep commands_ until StealCommands() is called by recorder.
}

auto CommandList::OnExecuted() -> void
{
  ::oxygen::graphics::CommandList::OnExecuted();
  // After execution the command list should be free; clear any remaining
  // commands defensively.
  commands_.clear();
}
