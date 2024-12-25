//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "oxygen/platform/Types.h"
#include "Oxygen/Renderers/Common/Renderer.h"
#include "Oxygen/Renderers/Direct3d12/api_export.h"
#include <Oxygen/Renderers/Common/Types.h>

#include "D3D12MemAlloc.h"

namespace oxygen::renderer {
  class WindowSurface;
}

namespace oxygen::renderer::d3d12 {

  namespace detail {
    class RendererImpl;
    class DescriptorHeap;
  }  // namespace detail

  class Renderer final
    : public oxygen::Renderer
    , public std::enable_shared_from_this<Renderer>
  {
  public:
    OXYGEN_D3D12_API Renderer() = default;
    OXYGEN_D3D12_API ~Renderer() override = default;

    OXYGEN_MAKE_NON_COPYABLE(Renderer);
    OXYGEN_MAKE_NON_MOVEABLE(Renderer);

    [[nodiscard]] auto Name() const->std::string override { return "DX12 Renderer"; }

    OXYGEN_D3D12_API void Render(const resources::SurfaceId& surface_id) override;

    OXYGEN_D3D12_API [[nodiscard]] auto CurrentFrameIndex() const->size_t override;

    OXYGEN_D3D12_API [[nodiscard]] auto RtvHeap() const->detail::DescriptorHeap&;
    OXYGEN_D3D12_API [[nodiscard]] auto DsvHeap() const->detail::DescriptorHeap&;
    OXYGEN_D3D12_API [[nodiscard]] auto SrvHeap() const->detail::DescriptorHeap&;
    OXYGEN_D3D12_API [[nodiscard]] auto UavHeap() const->detail::DescriptorHeap&;

    OXYGEN_D3D12_API auto CreateWindowSurface(oxygen::platform::WindowPtr window) const->SurfacePtr override;


    //OXYGEN_D3D12_API void CreateSwapChain(const resources::SurfaceId& surface_id) const override;
    //, DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) const;
    // TODO: Add backend independent support for format

    D3D12MA::Allocator* GetAllocator() const;

  protected:
    void OnInitialize() override;
    void OnShutdown() override;


  private:
    std::shared_ptr<detail::RendererImpl> pimpl_;
  };

  namespace detail {
    /**
     * Get a reference to the single instance of the Direct3D12 Renderer for
     * internal use within the renderer implementation module.
     *
     * @note This function is not part of the public API and should not be used.
     * Instead, use the GetRenderer() function from the renderer loader API.
     *
     * @note This function will __abort__ when called while the renderer
     * instance is not yet initialized or has been destroyed.
     *
     * @return The Direct3D12 Renderer instance.
     */
    [[nodiscard]] Renderer& GetRenderer();
  }  // namespace detail

}  // namespace oxygen::renderer::d3d12
