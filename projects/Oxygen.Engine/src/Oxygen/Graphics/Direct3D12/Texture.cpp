//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <utility>

#include <Oxygen/Base/logging.h>
#include <Oxygen/Graphics/Common/ObjectRelease.h>
#include <Oxygen/Graphics/Direct3D12/Detail/FormatUtils.h>
#include <Oxygen/Graphics/Direct3D12/Detail/dx12_utils.h>
#include <Oxygen/Graphics/Direct3D12/Graphics.h>
#include <Oxygen/Graphics/Direct3D12/Resources/DescriptorHeap.h>
#include <Oxygen/Graphics/Direct3D12/Resources/DescriptorHeaps.h>
#include <Oxygen/Graphics/Direct3D12/Resources/GraphicResource.h>
#include <Oxygen/Graphics/Direct3D12/Texture.h>

using oxygen::graphics::d3d12::GraphicResource;
using oxygen::graphics::d3d12::Texture;
using oxygen::graphics::d3d12::detail::DescriptorHandle;
using oxygen::graphics::d3d12::detail::GetDxgiFormatMapping;
using oxygen::graphics::d3d12::detail::GetGraphics;

Texture::Texture(
    TextureDesc desc,
    // ReSharper disable once CppPassValueParameterByConstReference
    D3D12_RESOURCE_DESC resource_desc,
    GraphicResource::ManagedPtr<ID3D12Resource> resource,
    GraphicResource::ManagedPtr<D3D12MA::Allocation> allocation)
    : Base(desc.debug_name)
    , desc_(std::move(desc))
    , resource_desc_(resource_desc)
{
    static_assert(std::is_trivially_copyable<D3D12_RESOURCE_DESC>());
    DCHECK_NOTNULL_F(resource);

    AddComponent<GraphicResource>(desc_.debug_name, std::move(resource), std::move(allocation));

    if (desc_.is_uav) {
        clear_mip_level_uav_cache_.resize(desc_.mip_levels);
    }

    plane_count_ = GetGraphics().GetFormatPlaneCount(resource_desc_.Format);
}

Texture::~Texture()
{
    rtv_cache_.clear();
    dsv_cache_.clear();
    srv_cache_.clear();
    uav_cache_.clear();
    clear_mip_level_uav_cache_.clear();
}

// ReSharper disable CppClangTidyBugproneUseAfterMove
Texture::Texture(Texture&& other) noexcept
    : Base(std::move(other))
    , desc_(std::move(other.desc_))
    , resource_desc_(std::exchange(other.resource_desc_, {})) // Reset to default
    , plane_count_(std::exchange(other.plane_count_, 1)) // Reset to default
    , rtv_cache_(std::move(other.rtv_cache_))
    , dsv_cache_(std::move(other.dsv_cache_))
    , srv_cache_(std::move(other.srv_cache_))
    , uav_cache_(std::move(other.uav_cache_))
    , clear_mip_level_uav_cache_(std::move(other.clear_mip_level_uav_cache_))
{
    // Leave the moved-from object in a valid state
    other.rtv_cache_.clear();
    other.dsv_cache_.clear();
    other.srv_cache_.clear();
    other.uav_cache_.clear();
    other.clear_mip_level_uav_cache_.clear();
}

auto Texture::operator=(Texture&& other) noexcept -> Texture&
{
    if (this != &other) {
        Base::operator=(std::move(other));
        desc_ = std::move(other.desc_);
        resource_desc_ = std::exchange(other.resource_desc_, {}); // Reset to default
        plane_count_ = std::exchange(other.plane_count_, 1); // Reset to default

        rtv_cache_ = std::move(other.rtv_cache_);
        dsv_cache_ = std::move(other.dsv_cache_);
        srv_cache_ = std::move(other.srv_cache_);
        uav_cache_ = std::move(other.uav_cache_);
        clear_mip_level_uav_cache_ = std::move(other.clear_mip_level_uav_cache_);

        // Leave the moved-from object in a valid state
        other.rtv_cache_.clear();
        other.dsv_cache_.clear();
        other.srv_cache_.clear();
        other.uav_cache_.clear();
        other.clear_mip_level_uav_cache_.clear();
    }
    return *this;
}
// ReSharper enable CppClangTidyBugproneUseAfterMove

void Texture::SetName(const std::string_view name) noexcept
{
    Base::SetName(name);
    GetComponent<GraphicResource>().SetName(name);
}

auto Texture::GetNativeResource() const -> NativeObject
{
    return NativeObject(GetComponent<GraphicResource>().GetResource(), ClassTypeId());
}

auto Texture::GetShaderResourceView(
    const Format format,
    const TextureSubResourceSet sub_resources,
    const TextureDimension dimension) -> NativeObject
{
    static_assert(sizeof(void*) == sizeof(D3D12_CPU_DESCRIPTOR_HANDLE),
        "Cannot typecast a descriptor to void*");

    const TextureBindingKey key(sub_resources, format);
    D3D12_GPU_DESCRIPTOR_HANDLE native_handle;
    if (srv_cache_.contains(key)) {
        native_handle = srv_cache_[key].gpu;
    } else {
        auto handle = GetGraphics().Descriptors().SrvHeap().Allocate();
        CreateShaderResourceView(handle, format, dimension, sub_resources);
        native_handle = handle.gpu;
        srv_cache_[key] = std::move(handle);
    }
    return NativeObject(native_handle.ptr, ClassTypeId());
}

auto Texture::GetUnorderedAccessView(
    const Format format,
    const TextureSubResourceSet sub_resources,
    const TextureDimension dimension) -> NativeObject
{
    static_assert(sizeof(void*) == sizeof(D3D12_GPU_DESCRIPTOR_HANDLE),
        "Cannot typecast a descriptor to void*");

    const TextureBindingKey key(sub_resources, format);
    D3D12_GPU_DESCRIPTOR_HANDLE native_handle;
    if (uav_cache_.contains(key)) {
        native_handle = uav_cache_[key].gpu;
    } else {
        auto handle = GetGraphics().Descriptors().UavHeap().Allocate();
        CreateUnorderedAccessView(handle, format, dimension, sub_resources);
        native_handle = handle.gpu;
        uav_cache_[key] = std::move(handle);
    }
    return NativeObject(native_handle.ptr, ClassTypeId());
}

auto Texture::GetRenderTargetView(
    const Format format,
    const TextureSubResourceSet sub_resources) -> NativeObject
{
    static_assert(sizeof(void*) == sizeof(D3D12_CPU_DESCRIPTOR_HANDLE),
        "Cannot typecast a descriptor to void*");

    const TextureBindingKey key(sub_resources, format);
    D3D12_CPU_DESCRIPTOR_HANDLE native_handle;
    if (rtv_cache_.contains(key)) {
        native_handle = rtv_cache_[key].cpu;
    } else {
        auto handle = GetGraphics().Descriptors().RtvHeap().Allocate();
        CreateRenderTargetView(handle, format, sub_resources);
        native_handle = handle.cpu;
        rtv_cache_[key] = std::move(handle);
    }
    return NativeObject(native_handle.ptr, ClassTypeId());
}

auto Texture::GetDepthStencilView(
    const Format format,
    const TextureSubResourceSet sub_resources,
    const bool is_read_only) -> NativeObject
{
    static_assert(sizeof(void*) == sizeof(D3D12_CPU_DESCRIPTOR_HANDLE),
        "Cannot typecast a descriptor to void*");

    const TextureBindingKey key(sub_resources, format, is_read_only);
    D3D12_CPU_DESCRIPTOR_HANDLE native_handle;
    if (dsv_cache_.contains(key)) {
        native_handle = dsv_cache_[key].cpu;
    } else {
        auto handle = GetGraphics().Descriptors().DsvHeap().Allocate();
        CreateDepthStencilView(handle, sub_resources, is_read_only);
        native_handle = handle.cpu;
        dsv_cache_[key] = std::move(handle);
    }
    return NativeObject(native_handle.ptr, ClassTypeId());
}

void Texture::CreateShaderResourceView(
    DescriptorHandle& dh,
    Format format,
    TextureDimension dimension,
    TextureSubResourceSet sub_resources) const
{
    auto constexpr kNumArraySlicesInCube = 6;

    sub_resources = sub_resources.Resolve(desc_, false);

    if (dimension == TextureDimension::kUnknown) {
        dimension = desc_.dimension;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC view_desc = {};

    view_desc.Format = GetDxgiFormatMapping(format == Format::kUnknown ? desc_.format : format).srv_format;
    view_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

    uint32_t plane_slice = (view_desc.Format == DXGI_FORMAT_X24_TYPELESS_G8_UINT) ? 1 : 0;

    switch (dimension) {
    case TextureDimension::kTexture1D:
        view_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
        view_desc.Texture1D.MostDetailedMip = sub_resources.base_mip_level;
        view_desc.Texture1D.MipLevels = sub_resources.num_mip_levels;
        break;
    case TextureDimension::kTexture1DArray:
        view_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
        view_desc.Texture1DArray.FirstArraySlice = sub_resources.base_array_slice;
        view_desc.Texture1DArray.ArraySize = sub_resources.num_array_slices;
        view_desc.Texture1DArray.MostDetailedMip = sub_resources.base_mip_level;
        view_desc.Texture1DArray.MipLevels = sub_resources.num_mip_levels;
        break;
    case TextureDimension::kTexture2D:
        view_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        view_desc.Texture2D.MostDetailedMip = sub_resources.base_mip_level;
        view_desc.Texture2D.MipLevels = sub_resources.num_mip_levels;
        view_desc.Texture2D.PlaneSlice = plane_slice;
        break;
    case TextureDimension::kTexture2DArray:
        view_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        view_desc.Texture2DArray.FirstArraySlice = sub_resources.base_array_slice;
        view_desc.Texture2DArray.ArraySize = sub_resources.num_array_slices;
        view_desc.Texture2DArray.MostDetailedMip = sub_resources.base_mip_level;
        view_desc.Texture2DArray.MipLevels = sub_resources.num_mip_levels;
        view_desc.Texture2DArray.PlaneSlice = plane_slice;
        break;
    case TextureDimension::kTextureCube:
        view_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        view_desc.TextureCube.MostDetailedMip = sub_resources.base_mip_level;
        view_desc.TextureCube.MipLevels = sub_resources.num_mip_levels;
        break;
    case TextureDimension::kTextureCubeArray:
        view_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
        view_desc.TextureCubeArray.First2DArrayFace = sub_resources.base_array_slice;
        view_desc.TextureCubeArray.NumCubes = sub_resources.num_array_slices / kNumArraySlicesInCube;
        view_desc.TextureCubeArray.MostDetailedMip = sub_resources.base_mip_level;
        view_desc.TextureCubeArray.MipLevels = sub_resources.num_mip_levels;
        break;
    case TextureDimension::kTexture2DMS:
        view_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
        break;
    case TextureDimension::kTexture2DMSArray:
        view_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY;
        view_desc.Texture2DMSArray.FirstArraySlice = sub_resources.base_array_slice;
        view_desc.Texture2DMSArray.ArraySize = sub_resources.num_array_slices;
        break;
    case TextureDimension::kTexture3D:
        view_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
        view_desc.Texture3D.MostDetailedMip = sub_resources.base_mip_level;
        view_desc.Texture3D.MipLevels = sub_resources.num_mip_levels;
        break;
    case TextureDimension::kUnknown:
        ABORT_F("Invalid texture dimension: {}", nostd::to_string(dimension));
    default: // NOLINT(clang-diagnostic-covered-switch-default)
        ABORT_F("Unexpected texture dimension: {}", nostd::to_string(dimension));
    }

    GetGraphics().GetCurrentDevice()->CreateShaderResourceView(
        GetNativeResource().AsPointer<ID3D12Resource>(), &view_desc, dh.cpu);
}

void Texture::CreateUnorderedAccessView(
    DescriptorHandle& dh,
    Format format,
    TextureDimension dimension,
    TextureSubResourceSet sub_resources) const
{
    sub_resources = sub_resources.Resolve(desc_, true);

    if (dimension == TextureDimension::kUnknown) {
        dimension = desc_.dimension;
    }

    D3D12_UNORDERED_ACCESS_VIEW_DESC view_desc = {};

    view_desc.Format = GetDxgiFormatMapping(format == Format::kUnknown ? desc_.format : format).srv_format;

    switch (dimension) {
    case TextureDimension::kTexture1D:
        view_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1D;
        view_desc.Texture1D.MipSlice = sub_resources.base_mip_level;
        break;
    case TextureDimension::kTexture1DArray:
        view_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1DARRAY;
        view_desc.Texture1DArray.FirstArraySlice = sub_resources.base_array_slice;
        view_desc.Texture1DArray.ArraySize = sub_resources.num_array_slices;
        view_desc.Texture1DArray.MipSlice = sub_resources.base_mip_level;
        break;
    case TextureDimension::kTexture2D:
        view_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        view_desc.Texture2D.MipSlice = sub_resources.base_mip_level;
        break;
    case TextureDimension::kTexture2DArray:
    case TextureDimension::kTextureCube:
    case TextureDimension::kTextureCubeArray:
        view_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
        view_desc.Texture2DArray.FirstArraySlice = sub_resources.base_array_slice;
        view_desc.Texture2DArray.ArraySize = sub_resources.num_array_slices;
        view_desc.Texture2DArray.MipSlice = sub_resources.base_mip_level;
        break;
    case TextureDimension::kTexture3D:
        view_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
        view_desc.Texture3D.FirstWSlice = 0;
        view_desc.Texture3D.WSize = desc_.depth;
        view_desc.Texture3D.MipSlice = sub_resources.base_mip_level;
        break;
    case TextureDimension::kTexture2DMS:
    case TextureDimension::kTexture2DMSArray:
        DLOG_F(ERROR, "Texture `{}` has unsupported dimension `{}` for UAV",
            GetName(), nostd::to_string(dimension));
        return;
    case TextureDimension::kUnknown:
        ABORT_F("Invalid texture dimension: {}", nostd::to_string(dimension));
    default: // NOLINT(clang-diagnostic-covered-switch-default)
        ABORT_F("Unexpected texture dimension: {}", nostd::to_string(dimension));
    }

    GetGraphics().GetCurrentDevice()->CreateUnorderedAccessView(
        GetNativeResource().AsPointer<ID3D12Resource>(), nullptr, &view_desc, dh.cpu);
}

void Texture::CreateRenderTargetView(
    DescriptorHandle& dh,
    Format format,
    TextureSubResourceSet sub_resources) const
{
    sub_resources = sub_resources.Resolve(desc_, true);

    D3D12_RENDER_TARGET_VIEW_DESC view_desc = {};

    view_desc.Format = GetDxgiFormatMapping(format == Format::kUnknown ? desc_.format : format).rtv_format;

    switch (desc_.dimension) {
    case TextureDimension::kTexture1D:
        view_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1D;
        view_desc.Texture1D.MipSlice = sub_resources.base_mip_level;
        break;
    case TextureDimension::kTexture1DArray:
        view_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1DARRAY;
        view_desc.Texture1DArray.FirstArraySlice = sub_resources.base_array_slice;
        view_desc.Texture1DArray.ArraySize = sub_resources.num_array_slices;
        view_desc.Texture1DArray.MipSlice = sub_resources.base_mip_level;
        break;
    case TextureDimension::kTexture2D:
        view_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        view_desc.Texture2D.MipSlice = sub_resources.base_mip_level;
        break;
    case TextureDimension::kTexture2DArray:
    case TextureDimension::kTextureCube:
    case TextureDimension::kTextureCubeArray:
        view_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
        view_desc.Texture2DArray.ArraySize = sub_resources.num_array_slices;
        view_desc.Texture2DArray.FirstArraySlice = sub_resources.base_array_slice;
        view_desc.Texture2DArray.MipSlice = sub_resources.base_mip_level;
        break;
    case TextureDimension::kTexture2DMS:
        view_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
        break;
    case TextureDimension::kTexture2DMSArray:
        view_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY;
        view_desc.Texture2DMSArray.FirstArraySlice = sub_resources.base_array_slice;
        view_desc.Texture2DMSArray.ArraySize = sub_resources.num_array_slices;
        break;
    case TextureDimension::kTexture3D:
        view_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
        view_desc.Texture3D.FirstWSlice = sub_resources.base_array_slice;
        view_desc.Texture3D.WSize = sub_resources.num_array_slices;
        view_desc.Texture3D.MipSlice = sub_resources.base_mip_level;
        break;
    case TextureDimension::kUnknown:
        ABORT_F("Invalid texture dimension: {}", nostd::to_string(desc_.dimension));
    default: // NOLINT(clang-diagnostic-covered-switch-default)
        ABORT_F("Unexpected texture dimension: {}", nostd::to_string(desc_.dimension));
    }

    GetGraphics().GetCurrentDevice()->CreateRenderTargetView(
        GetNativeResource().AsPointer<ID3D12Resource>(), &view_desc, dh.cpu);
}

void Texture::CreateDepthStencilView(
    DescriptorHandle& dh,
    TextureSubResourceSet sub_resources,
    bool is_read_only) const
{
    sub_resources = sub_resources.Resolve(desc_, true);

    D3D12_DEPTH_STENCIL_VIEW_DESC view_desc = {};

    view_desc.Format = GetDxgiFormatMapping(desc_.format).rtv_format;

    if (is_read_only) {
        view_desc.Flags |= D3D12_DSV_FLAG_READ_ONLY_DEPTH;
        if (view_desc.Format == DXGI_FORMAT_D24_UNORM_S8_UINT
            || view_desc.Format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT) {
            view_desc.Flags |= D3D12_DSV_FLAG_READ_ONLY_STENCIL;
        }
    }

    switch (desc_.dimension) {
    case TextureDimension::kTexture1D:
        view_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1D;
        view_desc.Texture1D.MipSlice = sub_resources.base_mip_level;
        break;
    case TextureDimension::kTexture1DArray:
        view_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1DARRAY;
        view_desc.Texture1DArray.FirstArraySlice = sub_resources.base_array_slice;
        view_desc.Texture1DArray.ArraySize = sub_resources.num_array_slices;
        view_desc.Texture1DArray.MipSlice = sub_resources.base_mip_level;
        break;
    case TextureDimension::kTexture2D:
        view_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        view_desc.Texture2D.MipSlice = sub_resources.base_mip_level;
        break;
    case TextureDimension::kTexture2DArray:
    case TextureDimension::kTextureCube:
    case TextureDimension::kTextureCubeArray:
        view_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
        view_desc.Texture2DArray.ArraySize = sub_resources.num_array_slices;
        view_desc.Texture2DArray.FirstArraySlice = sub_resources.base_array_slice;
        view_desc.Texture2DArray.MipSlice = sub_resources.base_mip_level;
        break;
    case TextureDimension::kTexture2DMS:
        view_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS;
        break;
    case TextureDimension::kTexture2DMSArray:
        view_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY;
        view_desc.Texture2DMSArray.FirstArraySlice = sub_resources.base_array_slice;
        view_desc.Texture2DMSArray.ArraySize = sub_resources.num_array_slices;
        break;
    case TextureDimension::kTexture3D: {
        DLOG_F(ERROR, "Texture `{}` has unsupported dimension `{}` for DSV",
            GetName(), nostd::to_string(desc_.dimension));
        return;
    }
    case TextureDimension::kUnknown:
        ABORT_F("Invalid texture dimension: {}", nostd::to_string(desc_.dimension));
    default: // NOLINT(clang-diagnostic-covered-switch-default)
        ABORT_F("Unexpected texture dimension: {}", nostd::to_string(desc_.dimension));
    }

    GetGraphics().GetCurrentDevice()->CreateDepthStencilView(
        GetNativeResource().AsPointer<ID3D12Resource>(), &view_desc, dh.cpu);
}

auto Texture::GetClearMipLevelUnorderedAccessView(const uint32_t mip_level) -> const DescriptorHandle&
{
    DCHECK_F(desc_.is_uav, "Mip level UAVs are only supported for UAV textures.");

    // Check if the mip level UAV is already cached
    if (mip_level < clear_mip_level_uav_cache_.size()) {
        if (const auto& handle = clear_mip_level_uav_cache_[mip_level]; handle.IsValid()) {
            // return NativeObject(handle.cpu.ptr, ClassTypeId());
            return handle;
        }
    }

    // Create a new UAV for the mip level
    auto handle = GetGraphics().Descriptors().SrvHeap().Allocate();
    const TextureSubResourceSet sub_resources {
        .base_mip_level = mip_level,
        .num_mip_levels = 1,
        .base_array_slice = 0,
        .num_array_slices = TextureSubResourceSet::kAllArraySlices
    };
    CreateUnorderedAccessView(handle, Format::kUnknown, TextureDimension::kUnknown, sub_resources);

    // auto [native_handle] = handle.cpu;

    // Ensure the cache is large enough and store the handle
    if (mip_level >= clear_mip_level_uav_cache_.size()) {
        clear_mip_level_uav_cache_.resize(mip_level + 1);
    }
    clear_mip_level_uav_cache_[mip_level] = std::move(handle);

    // return NativeObject(native_handle, ClassTypeId());

    return clear_mip_level_uav_cache_[mip_level];
}
