//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

#include <Oxygen/Cooker/Import/ScratchImage.h>
#include <Oxygen/Core/Detail/FormatUtils.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>

namespace oxygen::content::import {

namespace {

  //! The bytes of a subresource whose rows of blocks lie `row_pitch` bytes
  //! apart, through its last row.
  [[nodiscard]] auto SliceBytes(const uint64_t row_pitch, const uint32_t height,
    const uint32_t depth, const Format format) -> uint64_t
  {
    return row_pitch * graphics::detail::BlockRows(format, height) * depth;
  }

} // namespace

//=== Factory Methods ===-----------------------------------------------------//

auto ScratchImage::Create(const ScratchImageMeta& meta) -> ScratchImage
{
  ScratchImage image;
  image.meta_ = meta;

  if (meta.width == 0 || meta.height == 0 || meta.mip_levels == 0) {
    return image; // Return empty image
  }

  const auto subresource_count
    = static_cast<uint32_t>(meta.array_layers) * meta.mip_levels;
  image.subresources_.resize(subresource_count);

  // Compute total size and fill subresource info. Offsets and pitches are
  // 32-bit, as a cooked texture's are: an image they cannot address is
  // refused, empty.
  uint64_t total_size = 0;

  for (uint16_t layer = 0; layer < meta.array_layers; ++layer) {
    for (uint16_t mip = 0; mip < meta.mip_levels; ++mip) {
      const auto index = ComputeSubresourceIndex(layer, mip, meta.mip_levels);
      const auto mip_width = MipExtent(meta.width, mip);
      const auto mip_height = MipExtent(meta.height, mip);
      const auto mip_depth = MipExtent(meta.depth, mip);
      const auto row_pitch = graphics::detail::RowBytes(meta.format, mip_width);
      const auto slice_size
        = SliceBytes(row_pitch, mip_height, mip_depth, meta.format);
      if (total_size + slice_size > std::numeric_limits<uint32_t>::max()) {
        return {};
      }

      // A row lies within its slice, so its pitch fits too.
      image.subresources_[index] = SubresourceInfo {
        .offset = static_cast<uint32_t>(total_size),
        .row_pitch = static_cast<uint32_t>(row_pitch),
        .width = mip_width,
        .height = mip_height,
      };

      total_size += slice_size;
    }
  }

  // Allocate storage
  image.storage_.resize(static_cast<std::size_t>(total_size));

  return image;
}

auto ScratchImage::CreateFromData(uint32_t width, uint32_t height,
  Format format, uint32_t row_pitch, std::vector<std::byte> pixel_data)
  -> ScratchImage
{
  ScratchImage image;

  image.meta_ = ScratchImageMeta {
    .texture_type = TextureType::kTexture2D,
    .width = width,
    .height = height,
    .depth = 1,
    .array_layers = 1,
    .mip_levels = 1,
    .format = format,
  };

  image.subresources_.push_back(SubresourceInfo {
    .offset = 0,
    .row_pitch = row_pitch,
    .width = width,
    .height = height,
  });

  image.storage_ = std::move(pixel_data);

  return image;
}

//=== Accessors ===-----------------------------------------------------------//

auto ScratchImage::GetImage(uint16_t array_layer, uint16_t mip_level) const
  -> ImageView
{
  assert(array_layer < meta_.array_layers);
  assert(mip_level < meta_.mip_levels);

  const auto index
    = ComputeSubresourceIndex(array_layer, mip_level, meta_.mip_levels);
  const auto& info = subresources_[index];

  // Compute slice size for span
  const auto depth = MipExtent(meta_.depth, mip_level);
  const auto slice_size = static_cast<std::size_t>(
    SliceBytes(info.row_pitch, info.height, depth, meta_.format));

  return ImageView {
    .width = info.width,
    .height = info.height,
    .format = meta_.format,
    .row_pitch_bytes = info.row_pitch,
    .pixels = std::span(storage_).subspan(info.offset, slice_size),
  };
}

auto ScratchImage::GetMutablePixels(uint16_t array_layer, uint16_t mip_level)
  -> std::span<std::byte>
{
  assert(array_layer < meta_.array_layers);
  assert(mip_level < meta_.mip_levels);

  const auto index
    = ComputeSubresourceIndex(array_layer, mip_level, meta_.mip_levels);
  const auto& info = subresources_[index];

  const auto depth = MipExtent(meta_.depth, mip_level);
  const auto slice_size = static_cast<std::size_t>(
    SliceBytes(info.row_pitch, info.height, depth, meta_.format));

  return std::span(storage_).subspan(info.offset, slice_size);
}

} // namespace oxygen::content::import
