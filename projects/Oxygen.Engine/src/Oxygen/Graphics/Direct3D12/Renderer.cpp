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

#include <Oxygen/Graphics/Common/Renderer.h>
#include <Oxygen/Graphics/Direct3D12/CommandRecorder.h>
#include <Oxygen/Graphics/Direct3D12/Detail/WindowSurface.h>
#include <Oxygen/Graphics/Direct3D12/Graphics.h>
#include <Oxygen/Graphics/Direct3D12/Renderer.h>

using oxygen::graphics::d3d12::Renderer;

Renderer::Renderer(
    const std::string_view name,
    std::weak_ptr<oxygen::Graphics> gfx_weak,
    std::weak_ptr<graphics::Surface> surface_weak,
    const uint32_t frames_in_flight)
    : graphics::Renderer(name, std::move(gfx_weak), std::move(surface_weak), frames_in_flight)
{
}

auto Renderer::CreateCommandRecorder(graphics::CommandList* command_list, graphics::CommandQueue* target_queue)
    -> std::unique_ptr<graphics::CommandRecorder>
{
    return std::make_unique<CommandRecorder>(command_list, target_queue);
}

#if 0
auto Renderer::CreateVertexBuffer(const void* data, size_t size, uint32_t stride) const -> BufferPtr
{
    DCHECK_NOTNULL_F(data);
    DCHECK_GT_F(size, 0u);
    DCHECK_GT_F(stride, 0u);

    // Create the vertex buffer resource
    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Alignment = 0;
    resourceDesc.Width = size;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.SampleDesc.Quality = 0;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12MA::ALLOCATION_DESC allocDesc = {};
    allocDesc.HeapType = D3D12_HEAP_TYPE_UPLOAD;

    BufferInitInfo initInfo = {
        .alloc_desc = allocDesc,
        .resource_desc = resourceDesc,
        .initial_state = D3D12_RESOURCE_STATE_GENERIC_READ,
        .size_in_bytes = size
    };

    auto buffer = std::make_shared<Buffer>(initInfo);

    // Copy the vertex data to the buffer
    void* mappedData = buffer->Map();
    memcpy(mappedData, data, size);
    buffer->Unmap();

    return buffer;
}

void Renderer::OnInitialize(/*PlatformPtr platform, const RendererProperties& props*/)
{
    graphics::Renderer::OnInitialize();
    pimpl_ = std::make_shared<detail::RendererImpl>();
    try {
        pimpl_->Init(GetInitProperties());
    } catch (const std::runtime_error&) {
        // Request a shutdown to cleanup resources
        throw;
    }
}
#endif
