//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <chrono>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Headless/Command.h>
#include <Oxygen/Graphics/Headless/CommandQueue.h>
#include <Oxygen/Graphics/Headless/Internal/CommandExecutor.h>

namespace oxygen::graphics::headless::internal {

CommandExecutor::CommandExecutor() = default;

CommandExecutor::~CommandExecutor()
{
  // Wait for any outstanding tasks to complete. We move the vector out under
  // lock to avoid holding the mutex while waiting.
  std::vector<std::shared_future<void>> to_wait;
  {
    std::lock_guard lk(futures_mutex_);
    to_wait = std::move(outstanding_futures_);
    outstanding_futures_.clear();
  }

  for (auto& f : to_wait) {
    try {
      f.wait();
    } catch (...) {
      // Swallow exceptions on shutdown.
    }
  }
}

auto CommandExecutor::ExecuteAsync(CommandQueue* queue,
  std::vector<SubmissionChunk> submission_chunks) -> uint64_t
{
  // Assign a submission id. Initialize next_submission_id_ on first use so
  // that ids correspond to queue fence values but are unique across
  // concurrent Submit() calls.
  if (next_submission_id_.load(std::memory_order_acquire) == 0) {
    const uint64_t start_id = queue ? (queue->GetCurrentValue() + 1) : 1;
    next_submission_id_.store(start_id, std::memory_order_release);
  }
  const uint64_t submission_id = next_submission_id_.fetch_add(1);

  auto wrapper
    = [chunks = std::move(submission_chunks), queue, submission_id]() mutable {
        try {
          LOG_F(INFO, "Executing submission id={} on executor", submission_id);
          CommandContext ctx;
          // observer_ptr has an explicit constructor from raw pointer, use
          // that form to initialize.
          ctx.queue = observer_ptr<CommandQueue>(queue);
          ctx.submission_id = submission_id;

          for (auto& chunk : chunks) {
            for (const auto& action : chunk.submit_actions) {
              if (action.kind
                == graphics::CommandList::SubmitQueueActionKind::kWait) {
                DCHECK_NOTNULL_F(queue);
                queue->QueueWaitImmediate(action.value);
              }
            }

            for (auto& cmd : chunk.commands) {
              if (cmd) {
                std::ostringstream ss;
                cmd->Serialize(ss);
                LOG_F(INFO, "submission={} executing command: {}",
                  submission_id, ss.str());
                cmd->Execute(ctx);
              }
            }

            if (queue != nullptr && !chunk.known_states.empty()) {
              queue->AdoptKnownResourceStates(chunk.known_states);
            }

            for (const auto& action : chunk.submit_actions) {
              if (action.kind
                == graphics::CommandList::SubmitQueueActionKind::kSignal) {
                DCHECK_NOTNULL_F(queue);
                queue->SignalImmediate(action.value);
              }
            }
          }
          DCHECK_NOTNULL_F(queue);
          queue->CompleteSubmission();
          LOG_F(INFO, "Completed submission id={}", submission_id);
        } catch (const std::exception& e) {
          LOG_F(ERROR, "Error executing submission: {}", e.what());
          if (queue != nullptr) {
            queue->CompleteSubmission();
          }
        }
      };
  auto fut = executor_.Enqueue(std::move(wrapper));

  // Keep a shared_future copy so we can wait on outstanding tasks during
  // destruction. Prune any already-ready futures to keep the vector bounded.
  try {
    std::shared_future<void> sf = fut.share();
    std::lock_guard lk(futures_mutex_);
    // Remove ready futures
    std::erase_if(outstanding_futures_, [](const std::shared_future<void>& f) {
      using namespace std::chrono_literals;
      return f.wait_for(0ms) == std::future_status::ready;
    });

    outstanding_futures_.push_back(sf);
  } catch (...) {
    // In case futures are not shareable for some reason, ignore and continue.
  }

  LOG_F(INFO, "Enqueued submission id={}", submission_id);
  return submission_id;
}

} // namespace oxygen::graphics::headless::internal
