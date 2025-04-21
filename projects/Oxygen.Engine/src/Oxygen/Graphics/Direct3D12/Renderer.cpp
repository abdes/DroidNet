//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Direct3D12/Renderer.h>

#include <cstdint>
#include <cstring>
#include <d3d12.h>
#include <dxgiformat.h>
#include <exception>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

#include <wrl/client.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ResourceTable.h>
#include <Oxygen/Base/Windows/ComError.h> // needed
#include <Oxygen/Graphics/Common/RenderTarget.h>
#include <Oxygen/Graphics/Common/Renderer.h>
#include <Oxygen/Graphics/Common/ShaderCompiler.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Direct3D12/Allocator/D3D12MemAlloc.h>
#include <Oxygen/Graphics/Direct3D12/CommandQueue.h>
#include <Oxygen/Graphics/Direct3D12/CommandRecorder.h>
#include <Oxygen/Graphics/Direct3D12/Detail/WindowSurface.h>
#include <Oxygen/Graphics/Direct3D12/Forward.h>
#include <Oxygen/Graphics/Direct3D12/Graphics.h>
#include <Oxygen/Graphics/Direct3D12/RenderTarget.h>
#include <Oxygen/Graphics/Direct3D12/Resources/Buffer.h>
#include <Oxygen/Graphics/Direct3D12/Resources/DescriptorHeap.h>
#include <Oxygen/ImGui/ImGuiPlatformBackend.h> // needed
#include <Oxygen/Platform/Types.h>

using Microsoft::WRL::ComPtr;
using oxygen::graphics::ShaderType;
using oxygen::graphics::d3d12::DeviceType;
using oxygen::graphics::d3d12::FactoryType;
using oxygen::graphics::d3d12::detail::GetMainDevice;
using oxygen::windows::ThrowOnFailed;

namespace {
using oxygen::graphics::resources::kSurface;
oxygen::ResourceTable<oxygen::graphics::d3d12::detail::WindowSurface> surfaces(kSurface, 256);
} // namespace

// Implementation details of the Renderer class
namespace oxygen::graphics::d3d12::detail {
// Anonymous namespace for command frame management
namespace {

    struct CommandFrame {
        uint64_t fence_value { 0 };
    };

} // namespace

class RendererImpl final {
public:
    RendererImpl() = default;
    ~RendererImpl() = default;

    OXYGEN_MAKE_NON_COPYABLE(RendererImpl);
    OXYGEN_MAKE_NON_MOVABLE(RendererImpl);

    auto CurrentFrameIndex() const -> size_t { return current_frame_index_; }

    void Init(const GraphicsConfig& props);
    void ShutdownRenderer();

    auto BeginFrame(const resources::SurfaceId& surface_id) const
        -> const graphics::RenderTarget&;
    void EndFrame(const resources::SurfaceId& surface_id) const;
    auto CreateWindowSurfaceImpl(platform::WindowPtr window) const -> resources::SurfaceId;

    [[nodiscard]] auto RtvHeap() const -> DescriptorHeap& { return rtv_heap_; }
    [[nodiscard]] auto DsvHeap() const -> DescriptorHeap& { return dsv_heap_; }
    [[nodiscard]] auto SrvHeap() const -> DescriptorHeap& { return srv_heap_; }
    [[nodiscard]] auto UavHeap() const -> DescriptorHeap& { return uav_heap_; }

    auto GetCommandRecorder() -> CommandRecorderPtr { return command_recorder_; }

private:
    std::unique_ptr<CommandQueue> command_queue_ {};
    std::shared_ptr<CommandRecorder> command_recorder_ {};
    mutable size_t current_frame_index_ { 0 };
    mutable CommandFrame frames_[kFrameBufferCount] {};

    // DeferredReleaseControllerPtr GetWeakPtr() { return shared_from_this(); }

    mutable DescriptorHeap rtv_heap_ {
        DescriptorHeap::InitInfo {
            .type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
            .capacity = 512,
            .is_shader_visible = false,
            .device = GetMainDevice(),
            .name = "RTV Descriptor Heap",
        }
    };
    mutable DescriptorHeap dsv_heap_ {
        DescriptorHeap::InitInfo {
            .type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
            .capacity = 512,
            .is_shader_visible = false,
            .device = GetMainDevice(),
            .name = "DSV Descriptor Heap",
        }
    };
    mutable DescriptorHeap srv_heap_ {
        DescriptorHeap::InitInfo {
            .type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
            .capacity = 4096,
            .is_shader_visible = true,
            .device = GetMainDevice(),
            .name = "SRV Descriptor Heap",
        }
    };
    mutable DescriptorHeap uav_heap_ {
        DescriptorHeap::InitInfo {
            .type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
            .capacity = 512,
            .is_shader_visible = false,
            .device = GetMainDevice(),
            .name = "RTV Descriptor Heap",
        }
    };
};

void RendererImpl::Init(const GraphicsConfig& props)
{
    // Initialize the command recorder
    command_queue_.reset(new CommandQueue(QueueRole::kGraphics));
    command_recorder_.reset(new CommandRecorder(QueueRole::kGraphics));

    // Initialize heaps
}

void RendererImpl::ShutdownRenderer()
{
    LOG_SCOPE_FUNCTION(INFO);

    // Flush any pending commands and release any deferred resources for all
    // our frame indices
    command_queue_->Flush();

    command_queue_.reset();
    command_recorder_.reset();

    // TODO: SafeRelease for objects that need to be released after a full flush
    // otherwise, we should use ObjectRelease() or DeferredObjectRelease()
    LOG_F(INFO, "D3D12MA Memory Allocator released");
}

auto RendererImpl::BeginFrame(const resources::SurfaceId& surface_id) const
    -> const graphics::RenderTarget&
{
    DCHECK_NOTNULL_F(command_recorder_);

    // Wait for the GPU to finish executing the previous frame, reset the
    // allocator once the GPU is done with it to free the memory we allocated to
    // store the commands.
    const auto& fence_value = frames_[CurrentFrameIndex()].fence_value;
    command_queue_->Wait(fence_value);

    DCHECK_F(surface_id.IsValid());

    auto& surface = surfaces.ItemAt(surface_id);
    if (surface.ShouldResize()) {
        command_queue_->Flush();
        surface.Resize();
    }
    return static_cast<RenderTarget&>(surface);
}

void RendererImpl::EndFrame(const resources::SurfaceId& surface_id) const
{
    try {
        const auto& surface = surfaces.ItemAt(surface_id);
        // Presenting
        surface.Present();
    } catch (const std::exception& e) {
        LOG_F(WARNING, "No surface for id=`{}`; frame discarded: {}", surface_id.ToString(), e.what());
    }

    // Signal and increment the fence value for the next frame.
    frames_[CurrentFrameIndex()].fence_value = command_queue_->Signal();
    current_frame_index_ = (current_frame_index_ + 1) % kFrameBufferCount;
}

auto RendererImpl::CreateWindowSurfaceImpl(platform::WindowPtr window) const -> resources::SurfaceId
{
    DCHECK_NOTNULL_F(window.lock());
    DCHECK_F(window.lock()->Valid());

    const auto surface_id = surfaces.Emplace(std::move(window), command_queue_->GetCommandQueue());
    if (!surface_id.IsValid()) {
        return {};
    }
    LOG_F(INFO, "Window Surface created: {}", surface_id.ToString());
    return surface_id;
}

} // namespace oxygen::graphics::d3d12::detail

using oxygen::graphics::d3d12::Renderer;

Renderer::Renderer()
    : graphics::Renderer("D3D12 Renderer")
{
}

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

void Renderer::OnShutdown()
{
    pimpl_->ShutdownRenderer();
    graphics::Renderer::OnShutdown();
}

auto Renderer::BeginFrame(const resources::SurfaceId& surface_id)
    -> const graphics::RenderTarget&
{
    current_render_target_ = static_cast<const RenderTarget*>(&pimpl_->BeginFrame(surface_id));
    return *current_render_target_;
}

void Renderer::EndFrame(const resources::SurfaceId& surface_id) const
{
    pimpl_->EndFrame(surface_id);
}

auto Renderer::GetCommandRecorder() const -> CommandRecorderPtr
{
    return pimpl_->GetCommandRecorder();
}

auto Renderer::RtvHeap() const -> detail::DescriptorHeap&
{
    return pimpl_->RtvHeap();
}

auto Renderer::DsvHeap() const -> detail::DescriptorHeap&
{
    return pimpl_->DsvHeap();
}

auto Renderer::SrvHeap() const -> detail::DescriptorHeap&
{
    return pimpl_->SrvHeap();
}

auto Renderer::UavHeap() const -> detail::DescriptorHeap&
{
    return pimpl_->UavHeap();
}

auto Renderer::CreateWindowSurface(platform::WindowPtr window) const -> resources::SurfaceId
{
    DCHECK_NOTNULL_F(window.lock());
    DCHECK_F(window.lock()->Valid());

    return pimpl_->CreateWindowSurfaceImpl(window);
}
