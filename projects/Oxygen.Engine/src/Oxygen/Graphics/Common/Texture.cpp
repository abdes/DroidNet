//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Detail/FormatUtils.h>
#include <Oxygen/Graphics/Common/DescriptorAllocationHandle.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/Texture.h>

using oxygen::graphics::Texture;
using oxygen::graphics::TextureSlice;
using oxygen::graphics::TextureSubResourceSet;
using oxygen::graphics::detail::FormatInfo;
using oxygen::graphics::detail::GetFormatInfo;

namespace {

inline auto AlignUp(const uint64_t value, const uint64_t alignment) -> uint64_t
{
  CHECK_NE_F(alignment, 0U, "alignment must be non-zero");
  CHECK_F(
    (alignment & (alignment - 1ULL)) == 0ULL, "alignment must be power-of-two");
  return (value + (alignment - 1ULL)) & ~(alignment - 1ULL);
}

inline auto BlocksX(const FormatInfo& info, const uint32_t width_texels)
  -> uint32_t
{
  return (width_texels + info.block_size - 1U) / info.block_size;
}

inline auto BlocksY(const FormatInfo& info, const uint32_t height_texels)
  -> uint32_t
{
  return (height_texels + info.block_size - 1U) / info.block_size;
}

} // namespace

auto TextureSlice::Resolve(const TextureDesc& desc) const -> TextureSlice
{
  TextureSlice ret(*this);
  constexpr auto kWholeExtent = static_cast<uint32_t>(-1);

  DCHECK_LT_F(mip_level, desc.mip_levels, "Invalid mip level: {} >= {}",
    mip_level, desc.mip_levels);

  if (width == kWholeExtent) {
    ret.width = (std::max)(desc.width >> mip_level, 1u);
  }

  if (height == kWholeExtent) {
    ret.height = (std::max)(desc.height >> mip_level, 1u);
  }

  if (depth == kWholeExtent) {
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

auto oxygen::graphics::ComputeLinearTextureCopyFootprint(const Format format,
  const LinearTextureExtent& extent, const SizeBytes row_pitch_alignment)
  -> LinearTextureCopyFootprint
{
  CHECK_NE_F(format, Format::kUnknown,
    "Cannot compute a texture copy footprint for an unknown format");

  const auto& info = GetFormatInfo(format);

  const auto row_count = BlocksY(info, extent.height);
  const auto slice_count = extent.depth;
  const auto tight_row_pitch
    = static_cast<uint64_t>(BlocksX(info, extent.width))
    * static_cast<uint64_t>(info.bytes_per_block);
  const auto row_pitch
    = SizeBytes { AlignUp(tight_row_pitch, row_pitch_alignment.get()) };
  const auto slice_pitch
    = SizeBytes { row_pitch.get() * static_cast<uint64_t>(row_count) };
  const auto total_bytes
    = SizeBytes { slice_pitch.get() * static_cast<uint64_t>(slice_count) };

  return { .row_pitch = row_pitch,
    .slice_pitch = slice_pitch,
    .total_bytes = total_bytes,
    .row_count = row_count,
    .slice_count = slice_count };
}

auto oxygen::graphics::ComputeLinearTextureCopyFootprint(
  const TextureDesc& desc, const TextureSlice& slice,
  const SizeBytes row_pitch_alignment) -> LinearTextureCopyFootprint
{
  const auto resolved = slice.Resolve(desc);
  return ComputeLinearTextureCopyFootprint(desc.format,
    LinearTextureExtent {
      .width = resolved.width,
      .height = resolved.height,
      .depth = resolved.depth,
    },
    row_pitch_alignment);
}

auto oxygen::graphics::ResolveTextureBufferCopyRegion(const TextureDesc& desc,
  const TextureBufferCopyRegion& region) -> TextureBufferCopyRegion
{
  auto resolved = region;
  resolved.texture_slice = region.texture_slice.Resolve(desc);

  const auto footprint
    = ComputeLinearTextureCopyFootprint(desc, resolved.texture_slice);
  resolved.buffer_row_pitch = region.buffer_row_pitch.get() == 0
    ? footprint.row_pitch
    : region.buffer_row_pitch;
  resolved.buffer_slice_pitch = region.buffer_slice_pitch.get() == 0
    ? SizeBytes { resolved.buffer_row_pitch.get()
        * static_cast<uint64_t>(footprint.row_count) }
    : region.buffer_slice_pitch;

  return resolved;
}

Texture::~Texture() { }

auto Texture::GetNativeView(const DescriptorAllocationHandle& view_handle,
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
