//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>

#include "Oxygen/Base/Macros.h"
#include "Oxygen/Base/Mixin.h"
#include "Oxygen/Base/MixinInitialize.h"
#include "Oxygen/Base/MixinNamed.h"
#include "Oxygen/Base/MixinShutdown.h"
#include "Oxygen/Base/Types.h"
#include "Oxygen/Core/Types.h"
#include "Oxygen/Platform/Common/InputEvent.h"

namespace oxygen {

class Renderer;

namespace core {

  class Module
    : public Mixin<Module, Curry<MixinNamed, const char*>::mixin, MixinInitialize, MixinShutdown>
  {
   public:
    //! Constructor to forward the arguments to the mixins in the chain.
    template <typename... Args>
    constexpr explicit Module(const char* name, EngineWeakPtr engine, Args&&... args)
      : Mixin(name, std::forward<Args>(args)...)
      , engine_(std::move(engine))
    {
    }

    ~Module() override = default;

    OXYGEN_MAKE_NON_COPYABLE(Module);
    OXYGEN_MAKE_NON_MOVEABLE(Module);

    [[nodiscard]] auto Name() const -> const std::string& { return ObjectName(); }

    virtual auto ProcessInput(const platform::InputEvent& event) -> void = 0;
    virtual auto Update(Duration delta_time) -> void = 0;
    virtual auto FixedUpdate() -> void = 0;
    virtual auto Render(const Renderer*) -> void = 0;

   protected:
    virtual void OnInitialize(const Renderer* renderer) = 0;
    template <typename Base, typename... CtorArgs>
    friend class MixinInitialize; //< Allow access to OnInitialize.

    virtual void OnShutdown() = 0;
    template <typename Base>
    friend class MixinShutdown; //< Allow access to OnShutdown.

    [[nodiscard]] auto GetEngine() const -> const Engine& { return *(engine_.lock()); }

   private:
    EngineWeakPtr engine_;
  };

}
} // namespace core
