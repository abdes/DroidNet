//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <utility>

#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/Renderer.h>
#include <Oxygen/Graphics/Direct3D12/Detail/PipelineStateCache.h>
#include <Oxygen/Graphics/Direct3D12/api_export.h>

// ReSharper disable CppRedundantQualifier

namespace oxygen::graphics::d3d12 {

class Graphics;

namespace detail {
    class DescriptorHeap;
    class PerFrameResourceManager;
} // namespace detail

class Renderer final : public graphics::Renderer {
    using Base = graphics::Renderer;

public:
    Renderer(
        const std::weak_ptr<oxygen::Graphics>& gfx_weak,
        std::weak_ptr<oxygen::graphics::Surface> surface,
        const uint32_t frames_in_flight = kFrameBufferCount - 1)
        : Renderer("D3D12 Renderer", gfx_weak, std::move(surface), frames_in_flight)
    {
    }

    //! Default constructor, sets the object name.
    OXYGEN_D3D12_API Renderer(
        std::string_view name,
        const std::weak_ptr<oxygen::Graphics>& gfx_weak,
        std::weak_ptr<oxygen::graphics::Surface> surface_weak,
        uint32_t frames_in_flight = kFrameBufferCount - 1);

    OXYGEN_D3D12_API ~Renderer() override = default;

    OXYGEN_MAKE_NON_COPYABLE(Renderer)
    OXYGEN_MAKE_NON_MOVABLE(Renderer)

    // Hides base GetGraphics(), returns d3d12::Graphics&
    // ReSharper disable once CppHidingFunction
    [[nodiscard]] auto GetGraphics() -> d3d12::Graphics&;

    // Hides base GetGraphics(), returns d3d12::Graphics&
    // ReSharper disable once CppHidingFunction
    [[nodiscard]] auto GetGraphics() const -> const d3d12::Graphics&;

    [[nodiscard]] auto CreateTexture(TextureDesc desc) const
        -> std::shared_ptr<graphics::Texture> override;

    [[nodiscard]] auto CreateTextureFromNativeObject(
        TextureDesc desc, NativeObject native) const
        -> std::shared_ptr<graphics::Texture> override;

    [[nodiscard]] auto CreateFramebuffer(FramebufferDesc desc)
        -> std::shared_ptr<graphics::Framebuffer> override;

    [[nodiscard]] auto CreateBuffer(const BufferDesc& desc) const
        -> std::shared_ptr<graphics::Buffer> override;

    [[nodiscard]] auto GetOrCreateGraphicsPipeline(GraphicsPipelineDesc desc, size_t hash) const
        -> detail::PipelineStateCache::Entry;
    [[nodiscard]] auto GetOrCreateComputePipeline(ComputePipelineDesc desc, size_t hash) const
        -> detail::PipelineStateCache::Entry;

    [[nodiscard]] auto CreateDepthPrePass(const DepthPrePassConfig& config)
        -> std::shared_ptr<RenderPass> override;

protected:
    [[nodiscard]] OXYGEN_D3D12_API auto CreateCommandRecorder(
        graphics::CommandList* command_list,
        graphics::CommandQueue* target_queue)
        -> std::unique_ptr<graphics::CommandRecorder> override;

    OXYGEN_D3D12_API void PrepareRecorderForRender(graphics::CommandRecorder& recorder) override;
};

} // namespace oxygen::graphics::d3d12
