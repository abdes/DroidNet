//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <deque>
#include <functional>
#include <memory>
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
  current_value_ = value;
  if (current_value_ > completed_value_) {
    // Complete pending submissions up to the signaled value.
    const uint64_t advance = current_value_ - completed_value_;
    if (pending_submissions_ <= advance) {
      pending_submissions_ = 0;
    } else {
      pending_submissions_ -= advance;
    }
    completed_value_ = current_value_;
    cv_.notify_all();
  }
}

[[nodiscard]] auto CommandQueue::Signal() const -> uint64_t
{
  std::lock_guard lk(mutex_);
  ++current_value_;
  // One submission is considered completed by this signal.
  if (pending_submissions_ > 0) {
    --pending_submissions_;
  }
  completed_value_ = current_value_;
  cv_.notify_all();
  return current_value_;
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

auto CommandQueue::QueueSignalCommand(uint64_t value) -> void { Signal(value); }

auto CommandQueue::QueueWaitCommand(uint64_t value) const -> void
{
  Wait(value);
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
  // Support submission of one or more command lists. For each headless
  // command list we will steal its recorded commands and enqueue a submission
  // task that executes them serially on the per-queue executor. Each call to
  // Submit increments the pending submissions counter by one and the task is
  // responsible for signaling the queue when finished.
  {
    std::lock_guard lk(mutex_);
    // Lazy-initialize executor to avoid ordering issues during construction in
    // tests.
    if (!executor_) {
      executor_ = new internal::CommandExecutor();
    }
    ++pending_submissions_;
  }

  // Capture copies of the pointers to avoid lifetime issues with the caller's
  // storage. Immediately steal each headless command list's commands on the
  // submitter thread so that when Submit() returns the lists are empty.
  std::vector<std::deque<std::shared_ptr<Command>>> stolen_per_list;
  stolen_per_list.reserve(command_lists.size());
  for (const auto& base_ptr : command_lists) {
    if (!base_ptr) {
      stolen_per_list.emplace_back();
      continue;
    }
    // Avoid dynamic_cast (RTTI may be disabled). Use queue role to verify
    // the command list belongs to this queue's role and then static_cast.
    if (base_ptr->GetQueueRole() != queue_role_) {
      LOG_F(WARNING, "Submit: command list role mismatch, ignoring");
      stolen_per_list.emplace_back();
      continue;
    }

    auto* hdls = static_cast<CommandList*>(base_ptr.get());
    try {
      stolen_per_list.emplace_back(hdls->StealCommands());
    } catch (const std::exception& e) {
      LOG_F(ERROR, "Submit: failed to steal commands: {}", e.what());
      stolen_per_list.emplace_back();
    }
  }

  // Enqueue a task that executes the stolen command deques in submission
  // order. The executor_ has been initialized above while holding mutex_
  // so we can call it directly.
  // Flatten the stolen per-list deques into a single submission deque (preserve
  // list order) and hand it off to the executor which will create a
  // CommandContext, execute the commands, and signal the queue when done.
  std::deque<std::shared_ptr<Command>> submission;
  for (auto& lst : stolen_per_list) {
    for (auto& c : lst) {
      submission.push_back(std::move(c));
    }
  }

  const auto returned_id = executor_->ExecuteAsync(this, std::move(submission));

  LOG_F(INFO, "Headless Submit(span) enqueued pending submission (role={})",
    nostd::to_string(queue_role_));
}

auto CommandQueue::Flush() const -> void
{
  // Wait for all pending submissions to be consumed. We loop because
  // Signal() may be called concurrently and we want to wait until
  // pending_submissions_ reaches zero and completed_value_ reflects
  // the latest signaled value.
  std::unique_lock lk(mutex_);
  cv_.wait(lk, [this] { return pending_submissions_ == 0; });
  // At this point, completed_value_ should be up-to-date. No further
  // backend-specific action required for the headless model.
}

} // namespace oxygen::graphics::headless
