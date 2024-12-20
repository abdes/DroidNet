//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "oxygen/renderer/renderer.h"
#include "oxygen/renderer/renderer_module.h"
#include "oxygen/renderer-d3d12//api_export.h"

namespace oxygen::renderer::direct3d12 {

  namespace detail {
    class RendererImpl;
  }  // namespace detail

  class Renderer : public oxygen::Renderer
  {
  public:
    OXYGEN_RENDERER_D3D12_API Renderer() = default;
    OXYGEN_RENDERER_D3D12_API ~Renderer() override = default;

    OXYGEN_MAKE_NON_COPYABLE(Renderer);
    OXYGEN_MAKE_NON_MOVEABLE(Renderer);

    [[nodiscard]] auto Name() const->std::string override
    {
      return nostd::to_string(graphics::GraphicsBackendType::kDirect3D12);
    }

    OXYGEN_RENDERER_D3D12_API void Init(PlatformPtr platform, const RendererProperties& props) override;

  protected:
    void DoShutdown() override;


  private:
    std::unique_ptr<detail::RendererImpl> pimpl_;
  };

}  // namespace oxygen::renderer::direct3d12
