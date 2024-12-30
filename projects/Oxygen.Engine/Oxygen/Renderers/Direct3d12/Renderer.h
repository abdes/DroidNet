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

namespace oxygen {
  class Engine;
}

namespace oxygen::renderer {
  class WindowSurface;
}

namespace oxygen::renderer::d3d12 {

  namespace detail {
    class RendererImpl;
    class DescriptorHeap;
    class PerFrameResourceManager;
  }  // namespace detail

  class Renderer final
    : public oxygen::Renderer
    , public std::enable_shared_from_this<Renderer>
  {
  public:
    OXYGEN_D3D12_API Renderer();
    OXYGEN_D3D12_API ~Renderer() override = default;

    OXYGEN_MAKE_NON_COPYABLE(Renderer);
    OXYGEN_MAKE_NON_MOVEABLE(Renderer);

    auto GetCommandRecorder() const->CommandRecorderPtr override;

    OXYGEN_D3D12_API [[nodiscard]] auto RtvHeap() const->detail::DescriptorHeap&;
    OXYGEN_D3D12_API [[nodiscard]] auto DsvHeap() const->detail::DescriptorHeap&;
    OXYGEN_D3D12_API [[nodiscard]] auto SrvHeap() const->detail::DescriptorHeap&;
    OXYGEN_D3D12_API [[nodiscard]] auto UavHeap() const->detail::DescriptorHeap&;

    OXYGEN_D3D12_API [[nodiscard]] auto CreateWindowSurface(platform::WindowPtr window) const->SurfacePtr override;
    OXYGEN_D3D12_API [[nodiscard]] auto CreateImGuiModule(EngineWeakPtr engine, platform::WindowIdType window_id) const->std::unique_ptr<imgui::ImguiModule> override;


    //OXYGEN_D3D12_API void CreateSwapChain(const resources::SurfaceId& surface_id) const override;
    //, DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) const;
    // TODO: Add backend independent support for format

    D3D12MA::Allocator* GetAllocator() const;

    // TODO: Temporary until we separate the rendering surfaces from the app module
    auto GetCurrentRenderTarget() const -> const RenderTarget& { return *current_render_target_; }

  protected:
    void OnInitialize(PlatformPtr platform, const RendererProperties& props) override;
    void OnShutdown() override;

    auto BeginFrame(const resources::SurfaceId& surface_id) -> const renderer::RenderTarget & override;
    void EndFrame(CommandLists& command_lists, const resources::SurfaceId& surface_id) const override;

  private:
    std::shared_ptr<detail::RendererImpl> pimpl_{};

    // TODO: Temporary until we separate the rendering surfaces from the app module
    const RenderTarget* current_render_target_{ nullptr };
  };

}  // namespace oxygen::renderer::d3d12
