//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/DescriptorHandle.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/Texture.h>

using oxygen::graphics::Texture;
using oxygen::graphics::TextureSlice;
using oxygen::graphics::TextureSubResourceSet;

auto TextureSlice::Resolve(const TextureDesc& desc) const -> TextureSlice
{
  TextureSlice ret(*this);

  DCHECK_LT_F(mip_level, desc.mip_levels, "Invalid mip level: {} >= {}",
    mip_level, desc.mip_levels);

  if (std::cmp_equal(width, -1)) {
    ret.width = (std::max)(desc.width >> mip_level, 1u);
  }

  if (std::cmp_equal(height, -1)) {
    ret.height = (std::max)(desc.height >> mip_level, 1u);
  }

  if (std::cmp_equal(depth, -1)) {
    if (desc.texture_type == TextureType::kTexture3D) {
      ret.depth = (std::max)(desc.depth >> mip_level, 1u);
    } else {
      ret.depth = 1;
    }
  }

  return ret;
}

auto TextureSubResourceSet::Resolve(const TextureDesc& desc,
  const bool single_mip_level) const -> TextureSubResourceSet
{
  TextureSubResourceSet ret;
  ret.base_mip_level = base_mip_level;

  if (single_mip_level) {
    ret.num_mip_levels = 1;
  } else {
    const auto last_mip_level_plus_one
      = (std::min)(base_mip_level + num_mip_levels, desc.mip_levels);
    ret.num_mip_levels = static_cast<MipLevel>(
      (std::max)(0u, last_mip_level_plus_one - base_mip_level));
  }

  switch (desc.texture_type) // NOLINT(clang-diagnostic-switch-enum)
  {
  case TextureType::kTexture1DArray:
  case TextureType::kTexture2DArray:
  case TextureType::kTextureCube:
  case TextureType::kTextureCubeArray:
  case TextureType::kTexture2DMultiSampleArray: {
    ret.base_array_slice = base_array_slice;
    const auto last_array_slice_plus_one
      = (std::min)(base_array_slice + num_array_slices, desc.array_size);
    ret.num_array_slices = static_cast<ArraySlice>(
      (std::max)(0u, last_array_slice_plus_one - base_array_slice));
    break;
  }
  default:
    ret.base_array_slice = 0;
    ret.num_array_slices = 1;
    break;
  }

  return ret;
}

auto TextureSubResourceSet::IsEntireTexture(const TextureDesc& desc) const
  -> bool
{
  if (base_mip_level > 0u
    || base_mip_level + num_mip_levels < desc.mip_levels) {
    return false;
  }

  switch (desc.texture_type) // NOLINT(clang-diagnostic-switch-enum)
  {
  case TextureType::kTexture1DArray:
  case TextureType::kTexture2DArray:
  case TextureType::kTextureCube:
  case TextureType::kTextureCubeArray:
  case TextureType::kTexture2DMultiSampleArray:
    if (base_array_slice > 0u
      || base_array_slice + num_array_slices < desc.array_size) {
      return false;
    }
    return true;
  default:
    return true;
  }
}

Texture::~Texture() { }

auto Texture::GetNativeView(const DescriptorHandle& view_handle,
  const TextureViewDescription& view_desc) const -> NativeView
{
  using graphics::ResourceViewType;

  switch (view_desc.view_type) {
  case ResourceViewType::kTexture_SRV:
    return CreateShaderResourceView(view_handle, view_desc.format,
      view_desc.dimension, view_desc.sub_resources);
  case ResourceViewType::kTexture_UAV:
    return CreateUnorderedAccessView(view_handle, view_desc.format,
      view_desc.dimension, view_desc.sub_resources);
  case ResourceViewType::kTexture_RTV:
    return CreateRenderTargetView(
      view_handle, view_desc.format, view_desc.sub_resources);
  case ResourceViewType::kTexture_DSV:
    return CreateDepthStencilView(view_handle, view_desc.format,
      view_desc.sub_resources, view_desc.is_read_only_dsv);
  default:
    // Unknown or unsupported view type
    return {};
  }
}
