//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <chrono>

#include "Oxygen/Base/Logging.h"
#include "Oxygen/Base/Macros.h"
#include "Oxygen/Base/Mixin.h"
#include "Oxygen/Base/MixinDisposable.h"
#include "Oxygen/Base/MixinInitialize.h"
#include "Oxygen/Base/MixinNamed.h"
#include "Oxygen/Graphics/Common/ObjectRelease.h"
#include "Oxygen/Graphics/Common/SynchronizationCounter.h"
#include "Oxygen/Graphics/Common/Types.h"

namespace oxygen::renderer {

class CommandQueue
  : public Mixin<CommandQueue, Curry<MixinNamed, const char*>::mixin, MixinDisposable, MixinInitialize // last to consume remaining args
      >
{
 public:
  //! Constructor to forward the arguments to the mixins in the chain.
  template <typename... Args>
  constexpr explicit CommandQueue(const CommandListType type, Args&&... args)
    : Mixin(std::forward<Args>(args)...)
    , type_ { type }
  {
  }

  //! Minimal constructor, sets the object name.
  explicit CommandQueue(const CommandListType type)
    : Mixin("Command Queue")
    , type_ { type }
  {
  }

  ~CommandQueue() override = default;

  OXYGEN_MAKE_NON_COPYABLE(CommandQueue);
  OXYGEN_MAKE_NON_MOVEABLE(CommandQueue);

  virtual void Submit(const CommandListPtr& command_list) = 0;
  virtual void Flush() { Wait(fence_->GetCurrentValue()); }

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
  virtual void InitializeCommandQueue() = 0;
  virtual void ReleaseCommandQueue() noexcept = 0;

  virtual auto CreateSynchronizationCounter() -> std::unique_ptr<SynchronizationCounter> = 0;

 private:
  void OnInitialize()
  {
    if (this->self().ShouldRelease()) {
      const auto msg = fmt::format("{} OnInitialize() called twice without calling Release()", this->self().ObjectName());
      LOG_F(ERROR, "{}", msg);
      throw std::runtime_error(msg);
    }
    try {
      InitializeCommandQueue();
      fence_ = CreateSynchronizationCounter();
      CHECK_NOTNULL_F(fence_);
      fence_->Initialize(0);
    } catch (const std::exception& e) {
      LOG_F(ERROR, "Failed to initialize {}: {}", this->self().ObjectName(), e.what());
      ObjectRelease(fence_);
      throw;
    }
    this->self().ShouldRelease(true);
  }
  template <typename Base, typename... CtorArgs>
  friend class MixinInitialize; //< Allow access to OnInitialize.

  void OnRelease() noexcept
  {
    ReleaseCommandQueue();
    ObjectRelease(fence_);
    this->self().IsInitialized(false);
  }
  template <typename Base>
  friend class MixinDisposable; //< Allow access to OnRelease.

  CommandListType type_ { CommandListType::kNone };
  std::shared_ptr<SynchronizationCounter> fence_ {};
};

} // namespace oxygen::renderer
