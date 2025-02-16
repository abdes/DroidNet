//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <fmt/format.h>

#include "Oxygen/Base/Logging.h"
#include "Oxygen/Base/Macros.h"
#include "Oxygen/Base/Mixin.h"
#include "Oxygen/Base/MixinDisposable.h"
#include "Oxygen/Base/MixinInitialize.h"
#include "Oxygen/Base/MixinNamed.h"
#include "Oxygen/Graphics/Common/Types/CommandListType.h"

namespace oxygen::graphics {

class CommandList
    : public Mixin<CommandList,
          Curry<MixinNamed, const char*>::mixin,
          MixinDisposable,
          MixinInitialize // last to consume remaining args
          > {
public:
    //! Constructor to forward the arguments to the mixins in the chain.
    template <typename... Args>
    constexpr explicit CommandList(Args&&... args)
        : Mixin(std::forward<Args>(args)...)
    {
    }

    OXYGEN_GFX_API ~CommandList() override = default;

    OXYGEN_MAKE_NON_COPYABLE(CommandList);
    OXYGEN_MAKE_NON_MOVABLE(CommandList);

    [[nodiscard]] virtual auto GetQueueType() const -> CommandListType { return type_; }

protected:
    virtual void InitializeCommandList(CommandListType type) = 0;
    virtual void ReleaseCommandList() noexcept = 0;

private:
    void OnInitialize(const CommandListType type)
    {
        if (this->self().ShouldRelease()) {
            const auto msg = fmt::format("{} OnInitialize() called twice without calling Release()", this->self().ObjectName());
            LOG_F(ERROR, "{}", msg);
            throw std::runtime_error(msg);
        }
        try {
            InitializeCommandList(type);
            this->self().ShouldRelease(true);
        } catch (const std::exception& e) {
            LOG_F(ERROR, "Failed to initialize {}: {}", this->self().ObjectName(), e.what());
            throw;
        }
    }
    template <typename Base, typename... CtorArgs>
    friend class MixinInitialize; //< Allow access to OnInitialize.

    void OnRelease() noexcept
    {
        ReleaseCommandList();
        this->self().IsInitialized(false);
    }
    template <typename Base>
    friend class MixinDisposable; //< Allow access to OnRelease.

    CommandListType type_ { CommandListType::kNone };
};

} // namespace oxygen::graphics
