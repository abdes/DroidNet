//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Headless/CommandQueue.h>

namespace oxygen::graphics::headless {

auto CommandQueue::Signal(uint64_t value) const -> void
{
  std::lock_guard lk(mutex_);
  current_value_ = value;
  if (current_value_ > completed_value_) {
    completed_value_ = current_value_;
    cv_.notify_all();
  }
}

[[nodiscard]] auto CommandQueue::Signal() const -> uint64_t
{
  std::lock_guard lk(mutex_);
  ++current_value_;
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

auto CommandQueue::Submit(CommandList& /*command_list*/) -> void
{
  // For headless, just advance the completed value and log.
  const uint64_t v = Signal();
  LOG_F(INFO, "Headless Submit advanced fence to {}", v);
}

auto CommandQueue::Submit(std::span<CommandList*> /*command_lists*/) -> void
{
  const uint64_t v = Signal();
  LOG_F(INFO, "Headless Submit(span) advanced fence to {}", v);
}

} // namespace oxygen::graphics::headless
