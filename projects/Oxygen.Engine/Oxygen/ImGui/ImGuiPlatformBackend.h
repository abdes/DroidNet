//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#include "Oxygen/Base/Mixin.h"
#include "Oxygen/Base/MixinInitialize.h"
#include "Oxygen/Base/MixinNamed.h"
#include "Oxygen/Base/MixinShutdown.h"

struct ImGuiContext;

namespace oxygen::imgui {

  class ImGuiPlatformBackend
    : public Mixin
    < ImGuiPlatformBackend
    , Curry<MixinNamed, const char*>::mixin
    , MixinInitialize
    , MixinShutdown
    >
  {
  public:
    template <typename... Args>
    explicit ImGuiPlatformBackend(const char* name, Args&&... args)
      : Mixin(name, std::forward<Args>(args)...)
    {
    }

    ~ImGuiPlatformBackend() override = default;

    OXYGEN_MAKE_NON_COPYABLE(ImGuiPlatformBackend);
    OXYGEN_MAKE_NON_MOVEABLE(ImGuiPlatformBackend);

    virtual void NewFrame() = 0;

  protected:
    virtual void OnInitialize(ImGuiContext* imgui_context) = 0;
    template <typename Base, typename... CtorArgs>
    friend class MixinInitialize; //< Allow access to OnInitialize.

    virtual void OnShutdown() = 0;
    template <typename Base>
    friend class MixinShutdown; //< Allow access to OnShutdown.

  };

}  // namespace oxygen
