//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <utility>

#include <Oxygen/Base/logging.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Graphics/Common/Detail/PerFrameResourceManager.h>
#include <Oxygen/Graphics/Direct3D12/Allocator/D3D12MemAlloc.h>
#include <Oxygen/Graphics/Direct3D12/Bindless/DescriptorAllocator.h>
#include <Oxygen/Graphics/Direct3D12/Detail/Converters.h>
#include <Oxygen/Graphics/Direct3D12/Detail/FormatUtils.h>
#include <Oxygen/Graphics/Direct3D12/Detail/dx12_utils.h>
#include <Oxygen/Graphics/Direct3D12/GraphicResource.h>
#include <Oxygen/Graphics/Direct3D12/Graphics.h>
#include <Oxygen/Graphics/Direct3D12/Texture.h>

using oxygen::graphics::DescriptorHandle;
using oxygen::graphics::TextureDesc;
using oxygen::graphics::d3d12::GraphicResource;
using oxygen::graphics::d3d12::Graphics;
using oxygen::graphics::d3d12::Texture;
using oxygen::graphics::d3d12::detail::ConvertResourceStates;
using oxygen::graphics::d3d12::detail::GetDxgiFormatMapping;
using oxygen::graphics::detail::FormatInfo;
using oxygen::graphics::detail::GetFormatInfo;

namespace {

auto ConvertTextureDesc(const TextureDesc& d) -> D3D12_RESOURCE_DESC
{
  using oxygen::TextureType;

  const auto& format_mapping = GetDxgiFormatMapping(d.format);
  const FormatInfo& format_info = GetFormatInfo(d.format);

  D3D12_RESOURCE_DESC desc = {};
  desc.Width = d.width;
  desc.Height = d.height;
  desc.MipLevels = static_cast<UINT16>(d.mip_levels);
  desc.Format = d.is_typeless ? format_mapping.resource_format
                              : format_mapping.rtv_format;
  desc.SampleDesc.Count = d.sample_count;
  desc.SampleDesc.Quality = d.sample_quality;

  switch (d.texture_type) {
  case TextureType::kTexture1D:
  case TextureType::kTexture1DArray:
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE1D;
    desc.DepthOrArraySize = static_cast<UINT16>(d.array_size);
    break;
  case TextureType::kTexture2D:
  case TextureType::kTexture2DArray:
  case TextureType::kTextureCube:
  case TextureType::kTextureCubeArray:
  case TextureType::kTexture2DMultiSample:
  case TextureType::kTexture2DMultiSampleArray:
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.DepthOrArraySize = static_cast<UINT16>(d.array_size);
    break;
  case TextureType::kTexture3D:
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    desc.DepthOrArraySize = static_cast<UINT16>(d.depth);
    break;
  case TextureType::kUnknown:
    ABORT_F("Invalid texture dimension: {}", nostd::to_string(d.texture_type));
  }

  if (!d.is_shader_resource) {
    desc.Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
  }

  if (d.is_render_target) {
    if (format_info.has_depth || format_info.has_stencil) {
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
  const auto& format_mapping = GetDxgiFormatMapping(d.format);
  const FormatInfo& format_info = GetFormatInfo(d.format);

  D3D12_CLEAR_VALUE cv = {};
  cv.Format = format_mapping.rtv_format;
  if (format_info.has_depth || format_info.has_stencil) {
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

auto CreateTextureResource(const TextureDesc& desc, const Graphics* gfx)
  -> std::pair<ID3D12Resource*, D3D12MA::Allocation*>
{
  DCHECK_NOTNULL_F(gfx, "Graphics pointer cannot be null");

  const D3D12_RESOURCE_DESC rd = ConvertTextureDesc(desc);
  const D3D12_CLEAR_VALUE clear_value = ConvertTextureClearValue(desc);

  constexpr D3D12_HEAP_FLAGS heap_flags = D3D12_HEAP_FLAG_NONE;
  D3D12MA::ALLOCATION_DESC alloc_desc = {};
  alloc_desc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
  alloc_desc.ExtraHeapFlags = heap_flags;
  alloc_desc.Flags = D3D12MA::ALLOCATION_FLAG_COMMITTED;

  D3D12MA::Allocation* d3dma_allocation { nullptr };
  ID3D12Resource* resource { nullptr };
  const HRESULT hr = gfx->GetAllocator()->CreateResource(&alloc_desc, &rd,
    ConvertResourceStates(desc.initial_state),
    desc.use_clear_value ? &clear_value : nullptr, &d3dma_allocation,
    IID_PPV_ARGS(&resource));
  if (FAILED(hr)) {
    LOG_F(ERROR, "Failed to create texture `{}` with error {:#010X}",
      desc.debug_name, hr);
    throw std::runtime_error("Failed to create texture resource");
  }
  return { resource, d3dma_allocation };
}

auto GetDescriptorAllocator(const DescriptorHandle& view_handle)
{
  using oxygen::graphics::d3d12::DescriptorAllocator;

  DCHECK_F(view_handle.IsValid(), "Unexpected invalid view handle!");

  // NOLINTNEXTLINE(*-pro-type-static-cast-downcast)
  const auto* allocator
    = static_cast<DescriptorAllocator*>(view_handle.GetAllocator());
  DCHECK_NOTNULL_F(allocator, "Invalid descriptor allocator for handle: {}",
    nostd::to_string(view_handle));
  return allocator;
}

} // namespace

Texture::Texture(TextureDesc desc, const Graphics* gfx)
  : Base(desc.debug_name)
  , gfx_(gfx)
  , desc_(std::move(desc))
{
  DCHECK_NOTNULL_F(gfx_, "Graphics pointer cannot be null");

  auto [resource, d3dmaAllocation] = CreateTextureResource(desc_, gfx_);

  AddComponent<GraphicResource>(desc_.debug_name, resource, d3dmaAllocation);

  resource_desc_ = resource->GetDesc();
  plane_count_ = gfx_->GetFormatPlaneCount(resource_desc_.Format);
}

Texture::Texture(
  TextureDesc desc, const NativeObject& native, const Graphics* gfx)
  : Base(desc.debug_name)
  , gfx_(gfx)
  , desc_(std::move(desc))
{
  DCHECK_NOTNULL_F(gfx_, "Graphics pointer cannot be null");

  static_assert(std::is_trivially_copyable<D3D12_RESOURCE_DESC>());
  auto* resource = native.AsPointer<ID3D12Resource>();
  CHECK_NOTNULL_F(resource, "Invalid native object");

  AddComponent<GraphicResource>(desc_.debug_name, resource,
    nullptr // No allocation object for native resources
  );

  resource_desc_ = resource->GetDesc();
  plane_count_ = gfx_->GetFormatPlaneCount(resource_desc_.Format);
}

Texture::~Texture() { DLOG_F(1, "destroying texture: {}", Base::GetName()); }

// ReSharper disable CppClangTidyBugproneUseAfterMove
Texture::Texture(Texture&& other) noexcept
  : Base(std::move(other))
  , desc_(std::move(other.desc_))
  , resource_desc_(std::exchange(other.resource_desc_, {})) // Reset to default
  , plane_count_(std::exchange(other.plane_count_, 1)) // Reset to default
{
}

auto Texture::operator=(Texture&& other) noexcept -> Texture&
{
  if (this != &other) {
    Base::operator=(std::move(other));
    desc_ = std::move(other.desc_);
    resource_desc_
      = std::exchange(other.resource_desc_, {}); // Reset to default
    plane_count_ = std::exchange(other.plane_count_, 1); // Reset to default
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
  return { GetComponent<GraphicResource>().GetResource(), ClassTypeId() };
}

auto Texture::CurrentDevice() const -> dx::IDevice*
{
  return gfx_->GetCurrentDevice();
}

auto Texture::CreateShaderResourceView(const DescriptorHandle& view_handle,
  const Format format, const TextureType dimension,
  const TextureSubResourceSet sub_resources) const -> NativeObject
{
  if (!view_handle.IsValid()) {
    throw std::runtime_error("Invalid view handle");
  }

  const auto* allocator = GetDescriptorAllocator(view_handle);
  auto cpu_handle = allocator->GetCpuHandle(view_handle);
  CreateShaderResourceView(cpu_handle, format, dimension, sub_resources);
  auto gpu_handle = allocator->GetGpuHandle(view_handle);
  return { gpu_handle.ptr, ClassTypeId() };
}

auto Texture::CreateUnorderedAccessView(const DescriptorHandle& view_handle,
  const Format format, const TextureType dimension,
  const TextureSubResourceSet sub_resources) const -> NativeObject
{
  if (!view_handle.IsValid()) {
    throw std::runtime_error("Invalid view handle");
  }

  const auto* allocator = GetDescriptorAllocator(view_handle);
  auto cpu_handle = allocator->GetCpuHandle(view_handle);
  CreateUnorderedAccessView(cpu_handle, format, dimension, sub_resources);
  auto gpu_handle = allocator->GetGpuHandle(view_handle);
  return { gpu_handle.ptr, ClassTypeId() };
}

auto Texture::CreateRenderTargetView(const DescriptorHandle& view_handle,
  const Format format, const TextureSubResourceSet sub_resources) const
  -> NativeObject
{
  if (!view_handle.IsValid()) {
    throw std::runtime_error("Invalid view handle");
  }

  const auto* allocator = GetDescriptorAllocator(view_handle);
  auto cpu_handle = allocator->GetCpuHandle(view_handle);
  CreateRenderTargetView(cpu_handle, format, sub_resources);
  return { cpu_handle.ptr, ClassTypeId() };
}

auto Texture::CreateDepthStencilView(const DescriptorHandle& view_handle,
  const Format format, const TextureSubResourceSet sub_resources,
  const bool is_read_only) const -> NativeObject
{
  if (!view_handle.IsValid()) {
    throw std::runtime_error("Invalid view handle");
  }

  const auto* allocator = GetDescriptorAllocator(view_handle);
  auto cpu_handle = allocator->GetCpuHandle(view_handle);
  CreateDepthStencilView(cpu_handle, format, sub_resources, is_read_only);
  return { cpu_handle.ptr, ClassTypeId() };
}

void Texture::CreateShaderResourceView(D3D12_CPU_DESCRIPTOR_HANDLE& dh_cpu,
  Format format, TextureType dimension,
  TextureSubResourceSet sub_resources) const
{
  auto constexpr kNumArraySlicesInCube = 6;

  sub_resources = sub_resources.Resolve(desc_, false);

  if (dimension == TextureType::kUnknown) {
    dimension = desc_.texture_type;
  }

  D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};

  srv_desc.Format
    = GetDxgiFormatMapping(format == Format::kUnknown ? desc_.format : format)
        .srv_format;
  srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

  uint32_t plane_slice
    = (srv_desc.Format == DXGI_FORMAT_X24_TYPELESS_G8_UINT) ? 1 : 0;

  switch (dimension) {
  case TextureType::kTexture1D:
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
    srv_desc.Texture1D.MostDetailedMip = sub_resources.base_mip_level;
    srv_desc.Texture1D.MipLevels = sub_resources.num_mip_levels;
    break;
  case TextureType::kTexture1DArray:
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
    srv_desc.Texture1DArray.FirstArraySlice = sub_resources.base_array_slice;
    srv_desc.Texture1DArray.ArraySize = sub_resources.num_array_slices;
    srv_desc.Texture1DArray.MostDetailedMip = sub_resources.base_mip_level;
    srv_desc.Texture1DArray.MipLevels = sub_resources.num_mip_levels;
    break;
  case TextureType::kTexture2D:
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MostDetailedMip = sub_resources.base_mip_level;
    srv_desc.Texture2D.MipLevels = sub_resources.num_mip_levels;
    srv_desc.Texture2D.PlaneSlice = plane_slice;
    break;
  case TextureType::kTexture2DArray:
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    srv_desc.Texture2DArray.FirstArraySlice = sub_resources.base_array_slice;
    srv_desc.Texture2DArray.ArraySize = sub_resources.num_array_slices;
    srv_desc.Texture2DArray.MostDetailedMip = sub_resources.base_mip_level;
    srv_desc.Texture2DArray.MipLevels = sub_resources.num_mip_levels;
    srv_desc.Texture2DArray.PlaneSlice = plane_slice;
    break;
  case TextureType::kTextureCube:
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
    srv_desc.TextureCube.MostDetailedMip = sub_resources.base_mip_level;
    srv_desc.TextureCube.MipLevels = sub_resources.num_mip_levels;
    break;
  case TextureType::kTextureCubeArray:
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
    srv_desc.TextureCubeArray.First2DArrayFace = sub_resources.base_array_slice;
    srv_desc.TextureCubeArray.NumCubes
      = sub_resources.num_array_slices / kNumArraySlicesInCube;
    srv_desc.TextureCubeArray.MostDetailedMip = sub_resources.base_mip_level;
    srv_desc.TextureCubeArray.MipLevels = sub_resources.num_mip_levels;
    break;
  case TextureType::kTexture2DMultiSample:
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
    break;
  case TextureType::kTexture2DMultiSampleArray:
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY;
    srv_desc.Texture2DMSArray.FirstArraySlice = sub_resources.base_array_slice;
    srv_desc.Texture2DMSArray.ArraySize = sub_resources.num_array_slices;
    break;
  case TextureType::kTexture3D:
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
    srv_desc.Texture3D.MostDetailedMip = sub_resources.base_mip_level;
    srv_desc.Texture3D.MipLevels = sub_resources.num_mip_levels;
    break;
  case TextureType::kUnknown:
    ABORT_F("Invalid texture dimension: {}", nostd::to_string(dimension));
  default: // NOLINT(clang-diagnostic-covered-switch-default)
    ABORT_F("Unexpected texture dimension: {}", nostd::to_string(dimension));
  }

  CurrentDevice()->CreateShaderResourceView(
    GetNativeResource().AsPointer<ID3D12Resource>(), &srv_desc, dh_cpu);
}

void Texture::CreateUnorderedAccessView(D3D12_CPU_DESCRIPTOR_HANDLE& dh_cpu,
  Format format, TextureType dimension,
  TextureSubResourceSet sub_resources) const
{
  sub_resources = sub_resources.Resolve(desc_, true);

  if (dimension == TextureType::kUnknown) {
    dimension = desc_.texture_type;
  }

  D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};

  uav_desc.Format
    = GetDxgiFormatMapping(format == Format::kUnknown ? desc_.format : format)
        .srv_format;

  switch (dimension) {
  case TextureType::kTexture1D:
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1D;
    uav_desc.Texture1D.MipSlice = sub_resources.base_mip_level;
    break;
  case TextureType::kTexture1DArray:
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1DARRAY;
    uav_desc.Texture1DArray.FirstArraySlice = sub_resources.base_array_slice;
    uav_desc.Texture1DArray.ArraySize = sub_resources.num_array_slices;
    uav_desc.Texture1DArray.MipSlice = sub_resources.base_mip_level;
    break;
  case TextureType::kTexture2D:
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uav_desc.Texture2D.MipSlice = sub_resources.base_mip_level;
    break;
  case TextureType::kTexture2DArray:
  case TextureType::kTextureCube:
  case TextureType::kTextureCubeArray:
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
    uav_desc.Texture2DArray.FirstArraySlice = sub_resources.base_array_slice;
    uav_desc.Texture2DArray.ArraySize = sub_resources.num_array_slices;
    uav_desc.Texture2DArray.MipSlice = sub_resources.base_mip_level;
    break;
  case TextureType::kTexture3D:
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
    uav_desc.Texture3D.FirstWSlice = 0;
    uav_desc.Texture3D.WSize = desc_.depth;
    uav_desc.Texture3D.MipSlice = sub_resources.base_mip_level;
    break;
  case TextureType::kTexture2DMultiSample:
  case TextureType::kTexture2DMultiSampleArray:
    DLOG_F(ERROR, "Texture `{}` has unsupported dimension `{}` for UAV",
      GetName(), nostd::to_string(dimension));
    return;
  case TextureType::kUnknown:
    ABORT_F("Invalid texture dimension: {}", nostd::to_string(dimension));
  default: // NOLINT(clang-diagnostic-covered-switch-default)
    ABORT_F("Unexpected texture dimension: {}", nostd::to_string(dimension));
  }

  CurrentDevice()->CreateUnorderedAccessView(
    GetNativeResource().AsPointer<ID3D12Resource>(), nullptr, &uav_desc,
    dh_cpu);
}

void Texture::CreateRenderTargetView(D3D12_CPU_DESCRIPTOR_HANDLE& dh_cpu,
  Format format, TextureSubResourceSet sub_resources) const
{
  sub_resources = sub_resources.Resolve(desc_, true);

  D3D12_RENDER_TARGET_VIEW_DESC rtv_desc = {};

  rtv_desc.Format
    = GetDxgiFormatMapping(format == Format::kUnknown ? desc_.format : format)
        .rtv_format;

  switch (desc_.texture_type) {
  case TextureType::kTexture1D:
    rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1D;
    rtv_desc.Texture1D.MipSlice = sub_resources.base_mip_level;
    break;
  case TextureType::kTexture1DArray:
    rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1DARRAY;
    rtv_desc.Texture1DArray.FirstArraySlice = sub_resources.base_array_slice;
    rtv_desc.Texture1DArray.ArraySize = sub_resources.num_array_slices;
    rtv_desc.Texture1DArray.MipSlice = sub_resources.base_mip_level;
    break;
  case TextureType::kTexture2D:
    rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtv_desc.Texture2D.MipSlice = sub_resources.base_mip_level;
    break;
  case TextureType::kTexture2DArray:
  case TextureType::kTextureCube:
  case TextureType::kTextureCubeArray:
    rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
    rtv_desc.Texture2DArray.ArraySize = sub_resources.num_array_slices;
    rtv_desc.Texture2DArray.FirstArraySlice = sub_resources.base_array_slice;
    rtv_desc.Texture2DArray.MipSlice = sub_resources.base_mip_level;
    break;
  case TextureType::kTexture2DMultiSample:
    rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
    break;
  case TextureType::kTexture2DMultiSampleArray:
    rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY;
    rtv_desc.Texture2DMSArray.FirstArraySlice = sub_resources.base_array_slice;
    rtv_desc.Texture2DMSArray.ArraySize = sub_resources.num_array_slices;
    break;
  case TextureType::kTexture3D:
    rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
    rtv_desc.Texture3D.FirstWSlice = sub_resources.base_array_slice;
    rtv_desc.Texture3D.WSize = sub_resources.num_array_slices;
    rtv_desc.Texture3D.MipSlice = sub_resources.base_mip_level;
    break;
  case TextureType::kUnknown:
    ABORT_F(
      "Invalid texture dimension: {}", nostd::to_string(desc_.texture_type));
  default: // NOLINT(clang-diagnostic-covered-switch-default)
    ABORT_F(
      "Unexpected texture dimension: {}", nostd::to_string(desc_.texture_type));
  }

  CurrentDevice()->CreateRenderTargetView(
    GetNativeResource().AsPointer<ID3D12Resource>(), &rtv_desc, dh_cpu);
}

void Texture::CreateDepthStencilView(D3D12_CPU_DESCRIPTOR_HANDLE& dh_cpu,
  Format format, TextureSubResourceSet sub_resources, bool is_read_only) const
{
  sub_resources = sub_resources.Resolve(desc_, true);

  D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc = {};

  dsv_desc.Format = GetDxgiFormatMapping(format).rtv_format;

  if (is_read_only) {
    dsv_desc.Flags |= D3D12_DSV_FLAG_READ_ONLY_DEPTH;
    if (dsv_desc.Format == DXGI_FORMAT_D24_UNORM_S8_UINT
      || dsv_desc.Format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT) {
      dsv_desc.Flags |= D3D12_DSV_FLAG_READ_ONLY_STENCIL;
    }
  }

  switch (desc_.texture_type) {
  case TextureType::kTexture1D:
    dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1D;
    dsv_desc.Texture1D.MipSlice = sub_resources.base_mip_level;
    break;
  case TextureType::kTexture1DArray:
    dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1DARRAY;
    dsv_desc.Texture1DArray.FirstArraySlice = sub_resources.base_array_slice;
    dsv_desc.Texture1DArray.ArraySize = sub_resources.num_array_slices;
    dsv_desc.Texture1DArray.MipSlice = sub_resources.base_mip_level;
    break;
  case TextureType::kTexture2D:
    dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsv_desc.Texture2D.MipSlice = sub_resources.base_mip_level;
    break;
  case TextureType::kTexture2DArray:
  case TextureType::kTextureCube:
  case TextureType::kTextureCubeArray:
    dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
    dsv_desc.Texture2DArray.ArraySize = sub_resources.num_array_slices;
    dsv_desc.Texture2DArray.FirstArraySlice = sub_resources.base_array_slice;
    dsv_desc.Texture2DArray.MipSlice = sub_resources.base_mip_level;
    break;
  case TextureType::kTexture2DMultiSample:
    dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS;
    break;
  case TextureType::kTexture2DMultiSampleArray:
    dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY;
    dsv_desc.Texture2DMSArray.FirstArraySlice = sub_resources.base_array_slice;
    dsv_desc.Texture2DMSArray.ArraySize = sub_resources.num_array_slices;
    break;
  case TextureType::kTexture3D: {
    DLOG_F(ERROR, "Texture `{}` has unsupported dimension `{}` for DSV",
      GetName(), nostd::to_string(desc_.texture_type));
    return;
  }
  case TextureType::kUnknown:
    ABORT_F(
      "Invalid texture dimension: {}", nostd::to_string(desc_.texture_type));
  default: // NOLINT(clang-diagnostic-covered-switch-default)
    ABORT_F(
      "Unexpected texture dimension: {}", nostd::to_string(desc_.texture_type));
  }

  CurrentDevice()->CreateDepthStencilView(
    GetNativeResource().AsPointer<ID3D12Resource>(), &dsv_desc, dh_cpu);
}
