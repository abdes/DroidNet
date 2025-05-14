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

#include <Oxygen/Graphics/Direct3D12/CommandRecorder.h>
#include <Oxygen/Graphics/Direct3D12/Detail/Converters.h>
#include <Oxygen/Graphics/Direct3D12/Detail/FormatUtils.h>
#include <Oxygen/Graphics/Direct3D12/Detail/WindowSurface.h>
#include <Oxygen/Graphics/Direct3D12/Framebuffer.h>
#include <Oxygen/Graphics/Direct3D12/Graphics.h>
#include <Oxygen/Graphics/Direct3D12/Renderer.h>
#include <Oxygen/Graphics/Direct3D12/Resources/Buffer.h>

using oxygen::graphics::d3d12::Renderer;
using oxygen::graphics::d3d12::detail::GetGraphics;

Renderer::Renderer(
    const std::string_view name,
    std::weak_ptr<oxygen::Graphics> gfx_weak,
    std::weak_ptr<Surface> surface_weak,
    const uint32_t frames_in_flight)
    : graphics::Renderer(name, std::move(gfx_weak), std::move(surface_weak), frames_in_flight)
{
}

auto Renderer::CreateCommandRecorder(graphics::CommandList* command_list, graphics::CommandQueue* target_queue)
    -> std::unique_ptr<graphics::CommandRecorder>
{
    return std::make_unique<CommandRecorder>(command_list, target_queue);
}

namespace {

using oxygen::graphics::TextureDesc;
using oxygen::graphics::TextureDimension;
using oxygen::graphics::d3d12::detail::GetDxgiFormatMapping;
using oxygen::graphics::detail::FormatInfo;
using oxygen::graphics::detail::GetFormatInfo;

auto ConvertTextureDesc(const TextureDesc& d) -> D3D12_RESOURCE_DESC
{
    const auto& formatMapping = GetDxgiFormatMapping(d.format);
    const FormatInfo& formatInfo = GetFormatInfo(d.format);

    D3D12_RESOURCE_DESC desc = {};
    desc.Width = d.width;
    desc.Height = d.height;
    desc.MipLevels = static_cast<UINT16>(d.mip_levels);
    desc.Format = d.is_typeless ? formatMapping.resource_format : formatMapping.rtv_format;
    desc.SampleDesc.Count = d.sample_count;
    desc.SampleDesc.Quality = d.sample_quality;

    switch (d.dimension) {
    case TextureDimension::kTexture1D:
    case TextureDimension::kTexture1DArray:
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE1D;
        desc.DepthOrArraySize = static_cast<UINT16>(d.array_size);
        break;
    case TextureDimension::kTexture2D:
    case TextureDimension::kTexture2DArray:
    case TextureDimension::kTextureCube:
    case TextureDimension::kTextureCubeArray:
    case TextureDimension::kTexture2DMS:
    case TextureDimension::kTexture2DMSArray:
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.DepthOrArraySize = static_cast<UINT16>(d.array_size);
        break;
    case TextureDimension::kTexture3D:
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
        desc.DepthOrArraySize = static_cast<UINT16>(d.depth);
        break;
    case TextureDimension::kUnknown:
    default:
        ABORT_F("Invalid texture dimension: {}", nostd::to_string(d.dimension));
        // ReSharper disable once CppDFAUnreachableCode
        break;
    }

    if (!d.is_shader_resource) {
        desc.Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
    }

    if (d.is_render_target) {
        if (formatInfo.has_depth || formatInfo.has_stencil) {
            desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        } else {
            desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        }
    }

    if (d.is_uav) {
        desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }

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
        cv.DepthStencil.Stencil = static_cast<UINT8>(d.clear_value.g);
    } else {
        cv.Color[0] = d.clear_value.r;
        cv.Color[1] = d.clear_value.g;
        cv.Color[2] = d.clear_value.b;
        cv.Color[3] = d.clear_value.a;
    }

    return cv;
}

} // namespace

auto Renderer::CreateTexture(TextureDesc desc) const
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

    // To closely match the original CreateCommittedResource behavior (dedicated
    // resource), use ALLOCATION_FLAG_COMMITTED. D3D12MA will then use
    // ID3D12Device::CreateCommittedResource internally.
    alloc_desc.Flags = D3D12MA::ALLOCATION_FLAG_COMMITTED;

    D3D12MA::Allocation* d3dmaAllocation { nullptr };
    ID3D12Resource* resource { nullptr };
    // We'll get the ID3D12Resource via the D3D12MA::Allocation object.
    // So, riidResource is IID_NULL and ppvResource is nullptr.
    HRESULT hr = GetGraphics().GetAllocator()->CreateResource(
        &alloc_desc,
        &rd,
        detail::ConvertResourceStates(desc.initial_state),
        desc.use_clear_value ? &clear_value : nullptr,
        &d3dmaAllocation,
        IID_PPV_ARGS(&resource));
    if (FAILED(hr)) {
        LOG_F(ERROR, "Failed to create texture `{}` with error {:#010X}", desc.debug_name, hr);
        return nullptr;
    }

    return std::make_shared<Texture>(
        desc,
        rd,
        GetTextureViewsCache(),
        GraphicResource::WrapForDeferredRelease<ID3D12Resource>(resource, GetPerFrameResourceManager()),
        GraphicResource::WrapForDeferredRelease<D3D12MA::Allocation>(d3dmaAllocation, GetPerFrameResourceManager()));
}

auto Renderer::CreateTextureFromNativeObject(
    TextureDesc desc,
    NativeObject native) const
    -> std::shared_ptr<graphics::Texture>
{
    auto* resource = native.AsPointer<ID3D12Resource>();
    CHECK_NOTNULL_F(resource, "Invalid native object");

    // Increment the reference count since we're creating a new owner
    resource->AddRef();

    return std::make_shared<Texture>(
        desc,
        resource->GetDesc(),
        GetTextureViewsCache(),
        GraphicResource::WrapForDeferredRelease<ID3D12Resource>(resource, GetPerFrameResourceManager()),
        nullptr // No allocation object for native resources
    );
}

auto Renderer::CreateFramebuffer(FramebufferDesc desc) const
    -> std::shared_ptr<graphics::Framebuffer>
{
    return std::make_shared<Framebuffer>(desc);
}

auto Renderer::CreateBuffer(const BufferDesc& desc, const void* initial_data) const -> std::shared_ptr<graphics::Buffer>
{
    return std::make_shared<Buffer>(GetPerFrameResourceManager(), desc, initial_data);
}
