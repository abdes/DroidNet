//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>

#include <nlohmann/json.hpp>
#include <wrl/client.h>

#include "Graphics.h"
#include <Oxygen/Config/GraphicsConfig.h>
#include <Oxygen/Graphics/Common/BackendModule.h>
#include <Oxygen/Graphics/Common/Forward.h>
#include <Oxygen/Graphics/Common/ObjectRelease.h>
#include <Oxygen/Graphics/Direct3D12/CommandList.h>
#include <Oxygen/Graphics/Direct3D12/CommandQueue.h>
#include <Oxygen/Graphics/Direct3D12/Detail/Converters.h>
#include <Oxygen/Graphics/Direct3D12/Detail/FormatUtils.h>
#include <Oxygen/Graphics/Direct3D12/Detail/WindowSurface.h>
#include <Oxygen/Graphics/Direct3D12/Devices/DeviceManager.h>
#include <Oxygen/Graphics/Direct3D12/ImGui/ImGuiModule.h>
#include <Oxygen/Graphics/Direct3D12/Renderer.h>
#include <Oxygen/Graphics/Direct3D12/Resources/DescriptorHeaps.h>
#include <Oxygen/Graphics/Direct3D12/Shaders/EngineShaders.h>
#include <Oxygen/Graphics/Direct3D12/Texture.h>

//===----------------------------------------------------------------------===//
// Internal implementation of the graphics backend module API.

namespace {

auto GetBackendInternal() -> std::shared_ptr<oxygen::graphics::d3d12::Graphics>&
{
    static std::shared_ptr<oxygen::graphics::d3d12::Graphics> graphics;
    return graphics;
}

auto CreateBackend(const oxygen::SerializedBackendConfig& config) -> void*
{
    auto& backend = GetBackendInternal();
    if (!backend) {
        backend = std::make_shared<oxygen::graphics::d3d12::Graphics>(config);
    }
    return backend.get();
}

void DestroyBackend()
{
    LOG_SCOPE_F(INFO, "DestroyBackend");
    auto& renderer = GetBackendInternal();
    renderer.reset();
}

} // namespace

//===----------------------------------------------------------------------===//
// Implementation of the helper function for internal access to the graphics
// backend instance.

auto oxygen::graphics::d3d12::detail::GetGraphics() -> oxygen::graphics::d3d12::Graphics&
{
    auto& gfx = GetBackendInternal();
    CHECK_NOTNULL_F(gfx,
        "illegal access to the graphics backend before it is initialized or after it has been destroyed");
    return *gfx;
}

//===----------------------------------------------------------------------===//
// Public implementation of the graphics backend API.

extern "C" __declspec(dllexport) auto GetGraphicsModuleApi() -> void*
{
    static oxygen::graphics::GraphicsModuleApi render_module;
    render_module.CreateBackend = CreateBackend;
    render_module.DestroyBackend = DestroyBackend;
    return &render_module;
}

//===----------------------------------------------------------------------===//
// The Graphics class methods

using oxygen::graphics::d3d12::Graphics;
using oxygen::graphics::d3d12::detail::DescriptorHeaps;

auto Graphics::GetFactory() const -> dx::IFactory*
{
    auto* factory = GetComponent<DeviceManager>().Factory();
    CHECK_NOTNULL_F(factory, "graphics backend not properly initialized");
    return factory;
}

auto Graphics::GetCurrentDevice() const -> dx::IDevice*
{
    auto* device = GetComponent<DeviceManager>().Device();
    CHECK_NOTNULL_F(device, "graphics backend not properly initialized");
    return device;
}

auto oxygen::graphics::d3d12::Graphics::GetAllocator() const -> D3D12MA::Allocator*
{
    auto* allocator = GetComponent<DeviceManager>().Allocator();
    CHECK_NOTNULL_F(allocator, "graphics backend not properly initialized");
    return allocator;
}

auto Graphics::GetShader(const std::string_view unique_id) const -> std::shared_ptr<graphics::IShaderByteCode>
{
    return GetComponent<EngineShaders>().GetShader(unique_id);
}

Graphics::Graphics(const SerializedBackendConfig& config)
    : Base("D3D12 Backend")
{
    LOG_SCOPE_FUNCTION(INFO);

    // Parse JSON configuration
    nlohmann::json jsonConfig = nlohmann::json::parse(config.json_data, config.json_data + config.size);

    DeviceManagerDesc desc {};
    if (auto& enable_debug = jsonConfig["enable_debug"]) {
        desc.enable_debug = enable_debug.get<bool>();
    }

    AddComponent<DeviceManager>(desc);
    AddComponent<EngineShaders>();
    AddComponent<DescriptorHeaps>();
}

auto Graphics::Descriptors() const -> const detail::DescriptorHeaps&
{
    return GetComponent<DescriptorHeaps>();
}

auto Graphics::CreateCommandQueue(
    std::string_view name,
    graphics::QueueRole role,
    [[maybe_unused]] graphics::QueueAllocationPreference allocation_preference)
    -> std::shared_ptr<graphics::CommandQueue>
{
    return std::make_shared<CommandQueue>(name, role);
}

auto Graphics::CreateRendererImpl(
    const std::string_view name,
    std::weak_ptr<graphics::Surface> surface,
    uint32_t frames_in_flight)
    -> std::unique_ptr<oxygen::graphics::Renderer>
{
    return std::make_unique<graphics::d3d12::Renderer>(
        name, weak_from_this(), std::move(surface), frames_in_flight);
}

auto Graphics::CreateCommandListImpl(graphics::QueueRole role, std::string_view command_list_name)
    -> std::unique_ptr<oxygen::graphics::CommandList>
{
    return std::make_unique<CommandList>(role, command_list_name);
}

auto Graphics::GetFormatPlaneCount(DXGI_FORMAT format) const -> uint8_t
{
    uint8_t& plane_count = dxgi_format_plane_count_cache_[format];
    if (plane_count == 0) {
        D3D12_FEATURE_DATA_FORMAT_INFO format_info = { format, 1 };
        if (FAILED(GetCurrentDevice()->CheckFeatureSupport(
            D3D12_FEATURE_FORMAT_INFO, &format_info, sizeof(format_info)))) {
            // Format not supported - store a special value in the cache to avoid querying later
            plane_count = 255;
        } else {
            // Format supported - store the plane count in the cache
            plane_count = format_info.PlaneCount;
        }
    }

    if (plane_count == 255) {
        return 0;
    }

    return plane_count;
}

auto Graphics::CreateImGuiModule(EngineWeakPtr engine, platform::WindowIdType window_id) const -> std::unique_ptr<imgui::ImguiModule>
{
    return std::make_unique<ImGuiModule>(std::move(engine), window_id);
}

auto Graphics::CreateSurface(
    std::weak_ptr<platform::Window> window_weak,
    std::shared_ptr<graphics::CommandQueue> command_queue) const
    -> std::shared_ptr<graphics::Surface>
{
    DCHECK_F(!window_weak.expired());
    DCHECK_NOTNULL_F(command_queue);
    DCHECK_EQ_F(command_queue->GetTypeId(), graphics::d3d12::CommandQueue::ClassTypeId(), "Invalid command queue class");

    auto* queue = static_cast<graphics::d3d12::CommandQueue*>(command_queue.get());
    auto surface = std::make_shared<graphics::d3d12::detail::WindowSurface>(window_weak, queue->GetCommandQueue());
    CHECK_NOTNULL_F(surface, "Failed to create surface");
    return std::static_pointer_cast<graphics::Surface>(surface);
}

namespace {

using oxygen::graphics::TextureDesc;
using oxygen::graphics::TextureDimension;
using oxygen::graphics::d3d12::detail::GetDxgiFormatMapping;
using oxygen::graphics::detail::FormatInfo;
using oxygen::graphics::detail::GetFormatInfo;

static auto ConvertTextureDesc(const TextureDesc& d) -> D3D12_RESOURCE_DESC
{
    const auto& formatMapping = GetDxgiFormatMapping(d.format);
    const FormatInfo& formatInfo = GetFormatInfo(d.format);

    D3D12_RESOURCE_DESC desc = {};
    desc.Width = d.width;
    desc.Height = d.height;
    desc.MipLevels = UINT16(d.mip_levels);
    desc.Format = d.is_typeless ? formatMapping.resource_format : formatMapping.rtv_format;
    desc.SampleDesc.Count = d.sample_count;
    desc.SampleDesc.Quality = d.sample_quality;

    switch (d.dimension) {
    case TextureDimension::kTexture1D:
    case TextureDimension::kTexture1DArray:
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE1D;
        desc.DepthOrArraySize = UINT16(d.array_size);
        break;
    case TextureDimension::kTexture2D:
    case TextureDimension::kTexture2DArray:
    case TextureDimension::kTextureCube:
    case TextureDimension::kTextureCubeArray:
    case TextureDimension::kTexture2DMS:
    case TextureDimension::kTexture2DMSArray:
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.DepthOrArraySize = UINT16(d.array_size);
        break;
    case TextureDimension::kTexture3D:
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
        desc.DepthOrArraySize = UINT16(d.depth);
        break;
    case TextureDimension::kUnknown:
    default:
        ABORT_F("Invalid texture dimension: {}", nostd::to_string(d.dimension));
        break;
    }

    if (!d.is_shader_resource)
        desc.Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;

    if (d.is_render_target) {
        if (formatInfo.has_depth || formatInfo.has_stencil)
            desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        else
            desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    }

    if (d.is_uav)
        desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    return desc;
}

auto ConvertTextureClearValue(const TextureDesc& d) -> D3D12_CLEAR_VALUE
{
    const auto& formatMapping = GetDxgiFormatMapping(d.format);
    const FormatInfo& formatInfo = GetFormatInfo(d.format);

    D3D12_CLEAR_VALUE cv = {};
    cv.Format = formatMapping.rtv_format;
    if (formatInfo.has_depth || formatInfo.has_stencil) {
        cv.DepthStencil.Depth = d.clear_value.r;
        cv.DepthStencil.Stencil = UINT8(d.clear_value.g);
    } else {
        cv.Color[0] = d.clear_value.r;
        cv.Color[1] = d.clear_value.g;
        cv.Color[2] = d.clear_value.b;
        cv.Color[3] = d.clear_value.a;
    }

    return cv;
}

} // namespace

auto Graphics::CreateTexture(graphics::TextureDesc desc, std::string_view name = "Texture") const
    -> std::shared_ptr<graphics::Texture>
{
    D3D12_RESOURCE_DESC rd = ConvertTextureDesc(desc);
    D3D12_CLEAR_VALUE clear_value = ConvertTextureClearValue(desc);
    D3D12_HEAP_FLAGS heap_flags = D3D12_HEAP_FLAG_NONE; // For D3D12MA ExtraHeapFlags

    // TODO: consider supporting other heap types than default

    // Use D3D12MA for non-tiled, non-virtual textures
    D3D12MA::ALLOCATION_DESC alloc_desc = {};
    alloc_desc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
    // Pass shared flags to D3D12MA. If these are non-zero, D3D12MA
    // will ensure a separate heap or pass them to CreateCommittedResource.
    alloc_desc.ExtraHeapFlags = heap_flags;

    // To closely match original CreateCommittedResource behavior (dedicated resource),
    // use ALLOCATION_FLAG_COMMITTED. D3D12MA will then use
    // ID3D12Device::CreateCommittedResource internally.
    alloc_desc.Flags = D3D12MA::ALLOCATION_FLAG_COMMITTED;

    D3D12MA::Allocation* d3dmaAllocation { nullptr };
    ID3D12Resource* resource { nullptr };
    // We'll get the ID3D12Resource via the D3D12MA::Allocation object.
    // So, riidResource is IID_NULL and ppvResource is nullptr.
    HRESULT hr = GetAllocator()->CreateResource(
        &alloc_desc,
        &rd,
        detail::ConvertResourceStates(desc.initial_state),
        desc.use_clear_value ? &clear_value : nullptr,
        &d3dmaAllocation,
        IID_PPV_ARGS(&resource));
    if (FAILED(hr)) {
        LOG_F(ERROR, "Failed to create texture `{}` with error {:#010X}", name, hr);
        return nullptr;
    }

    return std::make_shared<oxygen::graphics::d3d12::Texture>(
        desc,
        rd,
        // Use custom deleter with ObjectRelease
        std::unique_ptr<ID3D12Resource, Texture::ResourceDeleter>(
            resource,
            [](ID3D12Resource* resource) {
                ObjectRelease(resource);
            }),
        d3dmaAllocation,
        name);
}

auto Graphics::CreateTextureFromNativeObject(
    graphics::TextureDesc desc,
    NativeObject native,
    std::string_view name = "Texture") const
    -> std::shared_ptr<graphics::Texture>
{
    auto* resource = native.AsPointer<ID3D12Resource>();
    CHECK_NOTNULL_F(resource, "Invalid native object");

    // Increment the reference count since we're creating a new owner
    resource->AddRef();

    return std::make_shared<oxygen::graphics::d3d12::Texture>(
        desc,
        resource->GetDesc(),
        // Use custom deleter with ObjectRelease
        std::unique_ptr<ID3D12Resource, Texture::ResourceDeleter>(
            resource,
            [](ID3D12Resource* resource) {
                ObjectRelease(resource);
            }),
        nullptr, // No allocation object for native resources
        name);
}
