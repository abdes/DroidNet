//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <span>
#include <string_view>

#include <Oxygen/Graphics/Common/CommandQueue.h>

namespace oxygen::graphics::headless {

class CommandQueue final : public graphics::CommandQueue {
public:
  explicit CommandQueue(std::string_view name)
    : graphics::CommandQueue(name)
  {
  }
  ~CommandQueue() override = default;

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

  auto Submit(CommandList& command_list) -> void override;
  auto Submit(std::span<CommandList*> command_lists) -> void override;

  [[nodiscard]] auto GetQueueRole() const -> QueueRole override
  {
    return QueueRole::kGraphics;
  }

private:
  mutable std::mutex mutex_;
  mutable std::condition_variable cv_;
  mutable uint64_t current_value_ { 0 };
  mutable uint64_t completed_value_ { 0 };
};

} // namespace oxygen::graphics::headless
