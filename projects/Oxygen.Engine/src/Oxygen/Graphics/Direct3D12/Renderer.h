//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Graphics/Common/Forward.h>
#include <Oxygen/Graphics/Common/Renderer.h>
#include <Oxygen/Graphics/Direct3D12/api_export.h>
#include <Oxygen/Platform/Types.h>

namespace oxygen::graphics::d3d12 {

namespace detail {
    class RendererImpl;
    class DescriptorHeap;
    class PerFrameResourceManager;
} // namespace detail

class Renderer final
    : public graphics::Renderer,
      public std::enable_shared_from_this<Renderer> {
public:
    OXYGEN_D3D12_API Renderer();
    OXYGEN_D3D12_API ~Renderer() override = default;

    OXYGEN_MAKE_NON_COPYABLE(Renderer);
    OXYGEN_MAKE_NON_MOVABLE(Renderer);

    auto GetCommandRecorder() const -> CommandRecorderPtr override;
    auto GetShaderCompiler() const -> ShaderCompilerPtr override;
    auto GetEngineShader(std::string_view unique_id) const -> std::shared_ptr<IShaderByteCode> override;

    [[nodiscard]] OXYGEN_D3D12_API auto RtvHeap() const -> detail::DescriptorHeap&;
    [[nodiscard]] OXYGEN_D3D12_API auto DsvHeap() const -> detail::DescriptorHeap&;
    [[nodiscard]] OXYGEN_D3D12_API auto SrvHeap() const -> detail::DescriptorHeap&;
    [[nodiscard]] OXYGEN_D3D12_API auto UavHeap() const -> detail::DescriptorHeap&;

    [[nodiscard]] OXYGEN_D3D12_API auto CreateWindowSurface(platform::WindowPtr window) const -> resources::SurfaceId override;

    // OXYGEN_D3D12_API void CreateSwapChain(const resources::SurfaceId& surface_id) const override;
    //, DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) const;
    //  TODO: Add backend independent support for format

    // TODO: Temporary until we separate the rendering surfaces from the app module
    auto GetCurrentRenderTarget() const -> const RenderTarget& { return *current_render_target_; }

    [[nodiscard]] auto CreateVertexBuffer(const void* data, size_t size, uint32_t stride) const -> BufferPtr override;

protected:
    void OnInitialize(PlatformPtr platform, const RendererProperties& props) override;
    void OnShutdown() override;

    auto BeginFrame(const resources::SurfaceId& surface_id) -> const graphics::RenderTarget& override;
    void EndFrame(CommandLists& command_lists, const resources::SurfaceId& surface_id) const override;

private:
    std::shared_ptr<detail::RendererImpl> pimpl_ {};

    // TODO: Temporary until we separate the rendering surfaces from the app module
    const RenderTarget* current_render_target_ { nullptr };
};

} // namespace oxygen::graphics::d3d12
