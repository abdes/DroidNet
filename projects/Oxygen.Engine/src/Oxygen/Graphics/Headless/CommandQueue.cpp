//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <deque>
#include <functional>
#include <memory>
#include <stdexcept>
#include <vector>

#include <Oxygen/Graphics/Headless/Command.h>
#include <Oxygen/Graphics/Headless/CommandList.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Headless/CommandQueue.h>
#include <Oxygen/Graphics/Headless/Internal/CommandExecutor.h>

namespace oxygen::graphics::headless {

CommandQueue::~CommandQueue()
{
  if (executor_) {
    delete executor_;
  }
}

auto CommandQueue::Signal(uint64_t value) const -> void
{
  std::lock_guard lk(mutex_);
  if (value <= current_value_) {
    throw std::invalid_argument(
      "New value must be greater than the current value");
  }
  current_value_ = value;
}

[[nodiscard]] auto CommandQueue::Signal() const -> uint64_t
{
  std::lock_guard lk(mutex_);
  ++current_value_;
  return current_value_;
}

auto CommandQueue::SignalImmediate(const uint64_t value) const -> void
{
  std::lock_guard lk(mutex_);
  if (value > current_value_) {
    current_value_ = value;
  }
  if (value <= completed_value_) {
    throw std::invalid_argument(
      "Immediate signal value must be greater than the completed value");
  }
  completed_value_ = value;
  cv_.notify_all();
}

auto CommandQueue::QueueWaitImmediate(const uint64_t value) const -> void
{
  Wait(value);
}

auto CommandQueue::Wait(uint64_t value, std::chrono::milliseconds timeout) const
  -> void
{
  std::unique_lock lk(mutex_);
  cv_.wait_for(
    lk, timeout, [this, value] { return completed_value_ >= value; });
}

auto CommandQueue::Wait(uint64_t value) const -> void
{
  std::unique_lock lk(mutex_);
  cv_.wait(lk, [this, value] { return completed_value_ >= value; });
}

[[nodiscard]] auto CommandQueue::GetCompletedValue() const -> uint64_t
{
  std::lock_guard lk(mutex_);
  return completed_value_;
}

[[nodiscard]] auto CommandQueue::GetCurrentValue() const -> uint64_t
{
  std::lock_guard lk(mutex_);
  return current_value_;
}

auto CommandQueue::Submit(std::shared_ptr<graphics::CommandList> command_list)
  -> void
{
  // Forward to the span overload for a single command list using a tiny
  // array so std::span has a valid array reference.
  std::array arr = { command_list };
  Submit(std::span<std::shared_ptr<graphics::CommandList>> { arr });
}

auto CommandQueue::Submit(
  std::span<std::shared_ptr<graphics::CommandList>> command_lists) -> void
{
  {
    std::lock_guard lk(mutex_);
    if (!executor_) {
      executor_ = new internal::CommandExecutor();
    }
    ++pending_submissions_;
  }

  std::vector<internal::SubmissionChunk> submission_chunks;
  submission_chunks.reserve(command_lists.size());
  for (const auto& base_ptr : command_lists) {
    if (!base_ptr) {
      submission_chunks.emplace_back();
      continue;
    }
    if (base_ptr->GetQueueRole() != queue_role_) {
      LOG_F(WARNING, "Submit: command list role mismatch, ignoring");
      submission_chunks.emplace_back();
      continue;
    }

    auto* hdls = static_cast<CommandList*>(base_ptr.get());
    try {
      submission_chunks.push_back(internal::SubmissionChunk {
        .submit_actions = base_ptr->TakeSubmitQueueActions(),
        .commands = hdls->StealCommands(),
      });
    } catch (const std::exception& e) {
      LOG_F(ERROR, "Submit: failed to steal commands: {}", e.what());
      submission_chunks.emplace_back();
    }
  }

  const auto returned_id
    = executor_->ExecuteAsync(this, std::move(submission_chunks));
  LOG_F(INFO,
    "Headless Submit(span) enqueued pending submission (role={}) -> id={}",
    nostd::to_string(queue_role_), returned_id);
}

auto CommandQueue::CompleteSubmission() const -> void
{
  std::lock_guard lk(mutex_);
  if (pending_submissions_ > 0) {
    --pending_submissions_;
  }
  cv_.notify_all();
}

auto CommandQueue::Flush() const -> void
{
  // Wait for all pending submissions to be consumed. We loop because
  // Signal() may be called concurrently, and we want to wait until
  // pending_submissions_ reaches zero and completed_value_ reflects
  // the latest signaled value.
  std::unique_lock lk(mutex_);
  cv_.wait(lk, [this] { return pending_submissions_ == 0; });
  // At this point, completed_value_ should be up-to-date. No further
  // backend-specific action required for the headless model.
}

} // namespace oxygen::graphics::headless
