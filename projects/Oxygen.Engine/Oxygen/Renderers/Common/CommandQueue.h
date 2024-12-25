//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <chrono>

#include "Oxygen/api_export.h"
#include "Oxygen/Renderers/Common/Disposable.h"
#include "Oxygen/Renderers/Common/ISynchronizationCounter.h"
#include "Oxygen/Renderers/Common/Types.h"

namespace oxygen::renderer {

  class CommandQueue : public Disposable
  {
  public:
    explicit CommandQueue(const CommandListType type) : type_{ type } {}
    ~CommandQueue() override = default;

    OXYGEN_MAKE_NON_COPYABLE(CommandQueue);
    OXYGEN_MAKE_NON_MOVEABLE(CommandQueue);

    virtual void Submit(const CommandListPtr& command_list) = 0;
    virtual void Flush() = 0;

    OXYGEN_API void Initialize();

    [[nodiscard]] virtual auto GetQueueType() const -> CommandListType { return type_; }

    virtual void Signal(const uint64_t value)
    {
      fence_->Signal(value);
    }
    [[nodiscard]] uint64_t Signal() const
    {
      return fence_->Signal();
    }
    void Wait(const uint64_t value, const std::chrono::milliseconds timeout) const
    {
      fence_->Wait(value, timeout);
    }
    void Wait(const uint64_t value) const
    {
      fence_->Wait(value);
    }
    void QueueWaitCommand(const uint64_t value) const
    {
      fence_->QueueWaitCommand(value);
    }
    void QueueSignalCommand(const uint64_t value) const
    {
      fence_->QueueSignalCommand(value);
    }

  protected:
    virtual void OnInitialize() = 0;
    virtual auto CreateSynchronizationCounter() -> std::unique_ptr<ISynchronizationCounter> = 0;

    std::unique_ptr<ISynchronizationCounter> fence_{};

  private:
    CommandListType type_{ CommandListType::kNone };
  };

} // namespace oxygen::renderer
