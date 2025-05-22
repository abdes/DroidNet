//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <memory>
#include <string_view>
#include <type_traits>
#include <utility>

#include <Oxygen/Graphics/Common/Detail/Bindless.h>
#include <Oxygen/Graphics/Direct3D12/Allocator/D3D12MemAlloc.h>
#include <Oxygen/Graphics/Direct3D12/Bindless/D3D12HeapAllocationStrategy.h>
#include <Oxygen/Graphics/Direct3D12/Bindless/DescriptorAllocator.h>
#include <Oxygen/Graphics/Direct3D12/Buffer.h>
#include <Oxygen/Graphics/Direct3D12/CommandRecorder.h>
#include <Oxygen/Graphics/Direct3D12/Detail/WindowSurface.h>
#include <Oxygen/Graphics/Direct3D12/Framebuffer.h>
#include <Oxygen/Graphics/Direct3D12/Graphics.h>
#include <Oxygen/Graphics/Direct3D12/Renderer.h>

using oxygen::graphics::TextureDesc;
using oxygen::graphics::d3d12::D3D12HeapAllocationStrategy;
using oxygen::graphics::d3d12::DescriptorAllocator;
using oxygen::graphics::d3d12::Renderer;
using oxygen::graphics::detail::Bindless;

Renderer::Renderer(
    const std::string_view name,
    std::weak_ptr<oxygen::Graphics> gfx_weak,
    std::weak_ptr<Surface> surface_weak,
    const uint32_t frames_in_flight)
    : graphics::Renderer(name, std::move(gfx_weak), std::move(surface_weak), frames_in_flight)
{
    auto allocator = std::make_unique<DescriptorAllocator>(
        std::make_shared<D3D12HeapAllocationStrategy>(),
        GetGraphics().GetCurrentDevice());
    AddComponent<Bindless>(std::move(allocator)); // TODO: make strategy configurable
}

auto Renderer::GetGraphics() -> d3d12::Graphics&
{
    // Hides base GetGraphics(), returns d3d12::Graphics&
    return static_cast<d3d12::Graphics&>(Base::GetGraphics());
}

auto Renderer::GetGraphics() const -> const d3d12::Graphics&
{
    // Hides base GetGraphics(), returns d3d12::Graphics&
    return static_cast<const d3d12::Graphics&>(Base::GetGraphics());
}

auto Renderer::CreateCommandRecorder(graphics::CommandList* command_list, graphics::CommandQueue* target_queue)
    -> std::unique_ptr<graphics::CommandRecorder>
{
    return std::make_unique<CommandRecorder>(command_list, target_queue);
}

auto Renderer::CreateTexture(TextureDesc desc) const
    -> std::shared_ptr<graphics::Texture>
{
    return std::make_shared<Texture>(desc, GetPerFrameResourceManager());
}

auto Renderer::CreateTextureFromNativeObject(TextureDesc desc, NativeObject native) const
    -> std::shared_ptr<graphics::Texture>
{
    return std::make_shared<Texture>(desc, native, GetPerFrameResourceManager());
}

auto Renderer::CreateFramebuffer(FramebufferDesc desc)
    -> std::shared_ptr<graphics::Framebuffer>
{
    return std::make_shared<Framebuffer>(shared_from_this(), desc);
}

auto Renderer::CreateBuffer(const BufferDesc& desc) const -> std::shared_ptr<graphics::Buffer>
{
    return std::make_shared<Buffer>(GetPerFrameResourceManager(), desc);
}
