//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Headless/Command.h>

namespace oxygen::graphics::headless {

class QueueSignalCommand : public Command {
public:
  explicit QueueSignalCommand(
    observer_ptr<graphics::CommandQueue> queue, uint64_t value)
    : queue_(queue)
    , value_(value)
  {
  }

  auto Serialize(std::ostream& /*os*/) const -> void override { }

  [[nodiscard]] auto GetName() const noexcept -> const char* override
  {
    return "QueueSignalCommand";
  }

protected:
  auto DoExecute(CommandContext& /*ctx*/) -> void override
  {
    if (!queue_) [[unlikely]] {
      DLOG_F(WARNING, "QueueSignalCommand: queue is null!");
      return;
    }
    DLOG_F(3, "value      : {}", value_);
    queue_->QueueSignalCommand(value_);
  }

private:
  observer_ptr<graphics::CommandQueue> queue_ { nullptr };
  uint64_t value_ { 0 };
};

} // namespace oxygen::graphics::headless
