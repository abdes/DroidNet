//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "detail/resources.h"
#include "oxygen/renderer/renderer.h"
#include "oxygen/renderer-d3d12/api_export.h"

namespace oxygen::renderer::direct3d12 {

  namespace detail {
    class RendererImpl;
  }  // namespace detail

  class Renderer final : public oxygen::Renderer, public std::enable_shared_from_this<Renderer>
  {
  public:
    OXYGEN_D3D12_API Renderer();
    OXYGEN_D3D12_API ~Renderer() override;

    OXYGEN_MAKE_NON_COPYABLE(Renderer);
    OXYGEN_MAKE_NON_MOVEABLE(Renderer);

    [[nodiscard]] auto Name() const->std::string override { return "DX12 Renderer"; }

    OXYGEN_D3D12_API void Init(PlatformPtr platform, const RendererProperties& props) override;
    OXYGEN_D3D12_API void Render(const SurfaceId& surface_id) override;

    OXYGEN_D3D12_API [[nodiscard]] auto CurrentFrameIndex() const->size_t override;

    OXYGEN_D3D12_API [[nodiscard]] auto RtvHeap() const->detail::DescriptorHeap&;
    OXYGEN_D3D12_API [[nodiscard]] auto DsvHeap() const->detail::DescriptorHeap&;
    OXYGEN_D3D12_API [[nodiscard]] auto SrvHeap() const->detail::DescriptorHeap&;
    OXYGEN_D3D12_API [[nodiscard]] auto UavHeap() const->detail::DescriptorHeap&;

    OXYGEN_D3D12_API void CreateSwapChain(const SurfaceId& surface_id) const;
    //, DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) const;
    // TODO: Add backend independent support for format

  protected:
    void DoShutdown() override;

  private:
    std::shared_ptr<detail::RendererImpl> pimpl_;
  };

}  // namespace oxygen::renderer::direct3d12
