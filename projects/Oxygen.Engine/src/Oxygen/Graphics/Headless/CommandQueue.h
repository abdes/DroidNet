//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <span>
#include <string_view>

#include <Oxygen/Graphics/Common/CommandQueue.h>

namespace oxygen::graphics::headless {

namespace internal {
  class SerialExecutor;
} // namespace internal

class CommandQueue final : public graphics::CommandQueue {
public:
  CommandQueue(std::string_view name, QueueRole role)
    : graphics::CommandQueue(name)
    , queue_role_(role)
  {
  }
  ~CommandQueue() override;

  // CommandQueue interface
  auto Signal(uint64_t value) const -> void override;
  [[nodiscard]] auto Signal() const -> uint64_t override;
  auto Wait(uint64_t value, std::chrono::milliseconds timeout) const
    -> void override;
  auto Wait(uint64_t value) const -> void override;

  auto QueueSignalCommand(uint64_t value) -> void override;
  auto QueueWaitCommand(uint64_t value) const -> void override;

  [[nodiscard]] auto GetCompletedValue() const -> uint64_t override;
  [[nodiscard]] auto GetCurrentValue() const -> uint64_t override;

  auto Submit(graphics::CommandList& command_list) -> void override;
  auto Submit(std::span<graphics::CommandList*> command_lists) -> void override;

  // Override the base Flush to provide headless-specific flush behavior.
  auto Flush() const -> void override;

  [[nodiscard]] auto GetQueueRole() const -> QueueRole override
  {
    return queue_role_;
  }

private:
  mutable std::mutex mutex_;
  mutable std::condition_variable cv_;
  mutable uint64_t current_value_ { 0 };
  mutable uint64_t completed_value_ { 0 };
  // Pending submissions are held until the queue is explicitly signaled.
  // Each pending submission is represented as a simple counter of how many
  // submissions are waiting to be completed. When Signal()/Signal(value)
  // advances the completed value we will clear pending submissions up to
  // the advanced value.
  mutable uint64_t pending_submissions_ { 0 };
  // Per-queue serial executor to ensure recorded submissions execute in
  // submission order and without creating orphaned threads. The executor
  // runs a single worker and provides futures for deterministic test sync.
  // Owned via unique_ptr for RAII; the destructor is defined out-of-line
  // in the .cpp where the executor type is complete.
  internal::SerialExecutor* executor_ { nullptr };

private:
  QueueRole queue_role_ { QueueRole::kGraphics };
};

} // namespace oxygen::graphics::headless
