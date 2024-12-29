//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "glm/vec4.hpp"

#include "Oxygen/Base/Logging.h"
#include "Oxygen/Base/Macros.h"
#include "Oxygen/Base/Mixin.h"
#include "Oxygen/Base/MixinDisposable.h"
#include "Oxygen/Base/MixinInitialize.h"
#include "Oxygen/Base/MixinNamed.h"
#include "Oxygen/Renderers/Common/Types.h"

namespace oxygen::renderer {

  enum ClearFlags
  {
    kClearFlagsColor = (1 << 0),
    kClearFlagsDepth = (1 << 1),
    kClearFlagsStencil = (1 << 2),
  };

  class CommandRecorder
    : public Mixin<CommandRecorder
    , Curry<MixinNamed, const char*>::mixin
    , MixinDisposable
    , MixinInitialize // last to consume remaining args
    >
  {
  public:
    //! Constructor to forward the arguments to the mixins in the chain.
    template <typename... Args>
    constexpr explicit CommandRecorder(const CommandListType type, Args &&...args)
      : Mixin(std::forward<Args>(args)...), type_{ type }
    {
    }

    //! Minimal constructor, sets the object name.
    explicit CommandRecorder(const CommandListType type)
      : Mixin("Command Recorder"), type_{ type }
    {
    }

    ~CommandRecorder() override = default;

    OXYGEN_MAKE_NON_COPYABLE(CommandRecorder);
    OXYGEN_MAKE_NON_MOVEABLE(CommandRecorder);

    [[nodiscard]] virtual auto GetQueueType() const -> CommandListType { return type_; }

    virtual void Begin() = 0;
    virtual auto End() -> CommandListPtr = 0;

    // Graphics commands
    virtual void Clear(uint32_t flags, uint32_t num_targets, const uint32_t* slots, const glm::vec4* colors, float depthValue, uint8_t stencilValue) = 0;

  protected:
    virtual void InitializeCommandRecorder() = 0;
    virtual void ReleaseCommandRecorder() noexcept = 0;

  private:
    void OnInitialize()
    {
      if (this->self().ShouldRelease())
      {
        const auto msg = fmt::format("{} OnInitialize() called twice without calling Release()", this->self().ObjectName());
        LOG_F(ERROR, "{}", msg);
        throw std::runtime_error(msg);
      }
      try {
        InitializeCommandRecorder();
        this->self().ShouldRelease(true);
      }
      catch (const std::exception& e) {
        LOG_F(ERROR, "Failed to initialize {}: {}", this->self().ObjectName(), e.what());
        ReleaseCommandRecorder();
        throw;
      }
    }
    template <typename Base, typename... CtorArgs>
    friend class MixinInitialize; //< Allow access to OnInitialize.

    void OnRelease() noexcept
    {
      ReleaseCommandRecorder();
      this->self().IsInitialized(false);
    }
    template <typename Base>
    friend class MixinDisposable; //< Allow access to OnRelease.

    CommandListType type_{ CommandListType::kNone };
  };

} // namespace oxygen::renderer
