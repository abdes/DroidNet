//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <functional>
#include <utility>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Headless/Command.h>
#include <Oxygen/Graphics/Headless/CommandQueue.h>
#include <Oxygen/Graphics/Headless/Internal/CommandExecutor.h>
#include <Oxygen/Graphics/Headless/Internal/SerialExecutor.h>

namespace oxygen::graphics::headless::internal {

CommandExecutor::CommandExecutor() = default;

CommandExecutor::~CommandExecutor() = default;

auto CommandExecutor::Prepare(CommandQueue* queue,
  std::vector<SubmissionChunk> chunks, std::function<void()> before,
  std::function<void()> completed, std::function<void()> failed) -> Task
{
  return SerialExecutor::Prepare(
    [queue, chunks = std::move(chunks), before = std::move(before),
      completed = std::move(completed), failed = std::move(failed)]() mutable {
      try {
        before();
        CommandContext context;
        context.queue = observer_ptr<CommandQueue>(queue);
        for (auto& chunk : chunks) {
          for (const auto& action : chunk.submit_actions) {
            if (action.kind
              == graphics::CommandList::SubmitQueueActionKind::kWait) {
              queue->QueueWaitImmediate(action.value);
            }
          }
          for (auto& command : chunk.commands) {
            if (command) {
              command->Execute(context);
            }
          }
          for (const auto& action : chunk.submit_actions) {
            if (action.kind
              == graphics::CommandList::SubmitQueueActionKind::kSignal) {
              queue->SignalImmediate(action.value);
            }
          }
        }
        completed();
      } catch (...) {
        failed();
      }
      queue->CompleteSubmission();
    });
}

} // namespace oxygen::graphics::headless::internal
