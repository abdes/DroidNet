//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "oxygen/base/macros.h"
#include "oxygen/base/types.h"
#include "oxygen/platform/types.h"
#include "oxygen/renderer/types.h"

namespace oxygen {

  class Renderer
  {
  public:
    virtual ~Renderer()
    {
      if (!IsShutdown()) Shutdown();
    }

    OXYGEN_MAKE_NON_COPYABLE(Renderer);
    OXYGEN_MAKE_NON_MOVEABLE(Renderer);

    [[nodiscard]] virtual auto Name() const->std::string = 0;
    [[nodiscard]] virtual auto CurrentFrameIndex() const->size_t = 0;

    virtual void Init(PlatformPtr platform, const RendererProperties& props) = 0;
    virtual void Render(const SurfaceId& surface_id) = 0;
    void Shutdown()
    {
      if (is_shutdown_) return;
      DoShutdown();
      is_shutdown_ = true;
    }

    [[nodiscard]] auto IsShutdown() const -> bool { return is_shutdown_; }

    virtual void CreateSwapChain(const SurfaceId& surface_id) const = 0;

  protected:
    Renderer() = default;
    virtual void DoShutdown() = 0;

  private:
    bool is_shutdown_{ false };
  };

} // namespace oxygen
