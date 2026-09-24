//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <deque>
#include <future>
#include <memory>
#include <vector>

#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Headless/CommandContext.h>
#include <Oxygen/Graphics/Headless/Internal/SerialExecutor.h>
#include <Oxygen/Graphics/Headless/api_export.h>

// Forward-declare Command to avoid including Command.h here.
namespace oxygen::graphics::headless {
class Command;
}

namespace oxygen::graphics::headless::internal {

struct SubmissionChunk {
  std::vector<graphics::CommandList::SubmitQueueAction> submit_actions;
  std::deque<std::shared_ptr<Command>> commands;
};

class CommandExecutor {
public:
  CommandExecutor();
  ~CommandExecutor();

  using Task = SerialExecutor::Task;
  auto Prepare(CommandQueue* queue, std::vector<SubmissionChunk> chunks,
    std::function<void()> before, std::function<void()> completed,
    std::function<void()> failed) -> Task;
  auto Stop() -> void { executor_.Stop(); }
  auto Commit(Task& task) noexcept -> bool { return executor_.Commit(task); }

private:
  SerialExecutor executor_;
};

} // namespace oxygen::graphics::headless::internal
