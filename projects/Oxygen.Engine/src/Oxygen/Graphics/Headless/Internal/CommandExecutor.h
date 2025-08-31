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

#include <Oxygen/Graphics/Headless/CommandContext.h>
#include <Oxygen/Graphics/Headless/Internal/SerialExecutor.h>
#include <Oxygen/Graphics/Headless/api_export.h>

// Forward-declare Command to avoid including Command.h here.
namespace oxygen::graphics::headless {
class Command;
}

namespace oxygen::graphics::headless::internal {

class CommandExecutor {
public:
  CommandExecutor();
  ~CommandExecutor();

  // Enqueue a full submission described by the queue and the stolen command
  // lists. The executor will schedule execution, populate a CommandContext,
  // and call Signal() on the queue when finished. Returns the assigned
  // submission id (caller may provide one).
  // Enqueue a full submission described by the queue and the stolen command
  // lists. The executor will schedule execution, populate a CommandContext,
  // and call Signal() on the queue when finished. The executor will assign
  // and return a submission id.
  // Enqueue a full submission described by the queue and the stolen command
  // deque. The executor will schedule execution, populate a CommandContext,
  // and call Signal() on the queue when finished. The executor will assign
  // and return a submission id.
  OXGN_HDLS_NDAPI auto ExecuteAsync(CommandQueue* queue,
    std::deque<std::shared_ptr<Command>> stolen_commands) -> uint64_t;

private:
  SerialExecutor executor_;

  // Track outstanding task futures so the executor can wait for them when
  // shutting down. Access protected by futures_mutex_.
  std::mutex futures_mutex_;
  std::vector<std::shared_future<void>> outstanding_futures_;
  // Monotonic submission id generator local to this executor. Initialized on
  // first submission using the queue's current value so ids map to future
  // fence values and remain unique across concurrent submits.
  std::atomic<uint64_t> next_submission_id_ { 0 };
};

} // namespace oxygen::graphics::headless::internal
