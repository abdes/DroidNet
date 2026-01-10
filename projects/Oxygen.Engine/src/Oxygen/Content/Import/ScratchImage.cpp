//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <bit>
#include <cassert>

#include <Oxygen/Content/Import/ScratchImage.h>

namespace oxygen::content::import {

namespace {

  //! Compute bytes per pixel for a given format.
  /*!
    Returns 0 for block-compressed formats (BC*), as they require special
    handling.
  */
  [[nodiscard]] constexpr auto ComputeBytesPerPixel(Format format) noexcept
    -> uint32_t
  {
    switch (format) {
    // Single 8-bit values
    case Format::kR8UInt:
    case Format::kR8SInt:
    case Format::kR8UNorm:
    case Format::kR8SNorm:
      return 1;

    // Single 16-bit values
    case Format::kR16UInt:
    case Format::kR16SInt:
    case Format::kR16UNorm:
    case Format::kR16SNorm:
    case Format::kR16Float:
      return 2;

    // Double 8-bit values
    case Format::kRG8UInt:
    case Format::kRG8SInt:
    case Format::kRG8UNorm:
    case Format::kRG8SNorm:
      return 2;

    // Single 32-bit values
    case Format::kR32UInt:
    case Format::kR32SInt:
    case Format::kR32Float:
      return 4;

    // Double 16-bit values
    case Format::kRG16UInt:
    case Format::kRG16SInt:
    case Format::kRG16UNorm:
    case Format::kRG16SNorm:
    case Format::kRG16Float:
      return 4;

    // Quadruple 8-bit values
    case Format::kRGBA8UInt:
    case Format::kRGBA8SInt:
    case Format::kRGBA8UNorm:
    case Format::kRGBA8UNormSRGB:
    case Format::kRGBA8SNorm:
    case Format::kBGRA8UNorm:
    case Format::kBGRA8UNormSRGB:
      return 4;

    // Double 32-bit values
    case Format::kRG32UInt:
    case Format::kRG32SInt:
    case Format::kRG32Float:
      return 8;

    // Quadruple 16-bit values
    case Format::kRGBA16UInt:
    case Format::kRGBA16SInt:
    case Format::kRGBA16UNorm:
    case Format::kRGBA16SNorm:
    case Format::kRGBA16Float:
      return 8;

    // Triple 32-bit values
    case Format::kRGB32UInt:
    case Format::kRGB32SInt:
    case Format::kRGB32Float:
      return 12;

    // Quadruple 32-bit values
    case Format::kRGBA32UInt:
    case Format::kRGBA32SInt:
    case Format::kRGBA32Float:
      return 16;

    // Packed types
    case Format::kB5G6R5UNorm:
    case Format::kB5G5R5A1UNorm:
    case Format::kB4G4R4A4UNorm:
      return 2;

    case Format::kR11G11B10Float:
    case Format::kR10G10B10A2UNorm:
    case Format::kR10G10B10A2UInt:
    case Format::kR9G9B9E5Float:
      return 4;

    // Block-compressed formats return 0 (require special handling)
    case Format::kBC1UNorm:
    case Format::kBC1UNormSRGB:
    case Format::kBC2UNorm:
    case Format::kBC2UNormSRGB:
    case Format::kBC3UNorm:
    case Format::kBC3UNormSRGB:
    case Format::kBC4UNorm:
    case Format::kBC4SNorm:
    case Format::kBC5UNorm:
    case Format::kBC5SNorm:
    case Format::kBC6HFloatU:
    case Format::kBC6HFloatS:
    case Format::kBC7UNorm:
    case Format::kBC7UNormSRGB:
      return 0;

    // Depth formats (not typically used in ScratchImage)
    case Format::kDepth16:
      return 2;
    case Format::kDepth24Stencil8:
    case Format::kDepth32:
      return 4;
    case Format::kDepth32Stencil8:
      return 8;

    default:
      return 0;
    }
  }

  //! Check if format is block-compressed.
  [[nodiscard]] constexpr auto IsBlockCompressed(Format format) noexcept -> bool
  {
    switch (format) {
    case Format::kBC1UNorm:
    case Format::kBC1UNormSRGB:
    case Format::kBC2UNorm:
    case Format::kBC2UNormSRGB:
    case Format::kBC3UNorm:
    case Format::kBC3UNormSRGB:
    case Format::kBC4UNorm:
    case Format::kBC4SNorm:
    case Format::kBC5UNorm:
    case Format::kBC5SNorm:
    case Format::kBC6HFloatU:
    case Format::kBC6HFloatS:
    case Format::kBC7UNorm:
    case Format::kBC7UNormSRGB:
      return true;
    default:
      return false;
    }
  }

  //! Compute bytes per block for block-compressed formats.
  [[nodiscard]] constexpr auto ComputeBytesPerBlock(Format format) noexcept
    -> uint32_t
  {
    switch (format) {
    case Format::kBC1UNorm:
    case Format::kBC1UNormSRGB:
    case Format::kBC4UNorm:
    case Format::kBC4SNorm:
      return 8; // 4x4 block = 8 bytes

    case Format::kBC2UNorm:
    case Format::kBC2UNormSRGB:
    case Format::kBC3UNorm:
    case Format::kBC3UNormSRGB:
    case Format::kBC5UNorm:
    case Format::kBC5SNorm:
    case Format::kBC6HFloatU:
    case Format::kBC6HFloatS:
    case Format::kBC7UNorm:
    case Format::kBC7UNormSRGB:
      return 16; // 4x4 block = 16 bytes

    default:
      return 0;
    }
  }

  //! Compute row pitch for a given width and format.
  [[nodiscard]] auto ComputeRowPitch(uint32_t width, Format format) noexcept
    -> uint32_t
  {
    if (IsBlockCompressed(format)) {
      // Block-compressed: blocks_x * bytes_per_block
      const auto blocks_x = (width + 3) / 4;
      return blocks_x * ComputeBytesPerBlock(format);
    }

    return width * ComputeBytesPerPixel(format);
  }

  //! Compute slice pitch (total bytes for one 2D surface).
  [[nodiscard]] auto ComputeSlicePitch(
    uint32_t width, uint32_t height, Format format) noexcept -> uint32_t
  {
    if (IsBlockCompressed(format)) {
      const auto blocks_x = (width + 3) / 4;
      const auto blocks_y = (height + 3) / 4;
      return blocks_x * blocks_y * ComputeBytesPerBlock(format);
    }

    return ComputeRowPitch(width, format) * height;
  }

} // namespace

//=== Static Helpers ===------------------------------------------------------//

auto ScratchImage::ComputeMipCount(uint32_t width, uint32_t height) noexcept
  -> uint32_t
{
  if (width == 0 || height == 0) {
    return 0;
  }

  const auto max_dim = std::max(width, height);
  // floor(log2(max_dim)) + 1
  return static_cast<uint32_t>(std::bit_width(max_dim));
}

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

  // Compute total size and fill subresource info
  uint32_t total_size = 0;

  for (uint16_t layer = 0; layer < meta.array_layers; ++layer) {
    for (uint16_t mip = 0; mip < meta.mip_levels; ++mip) {
      const auto index = ComputeSubresourceIndex(layer, mip, meta.mip_levels);
      const auto mip_width = ComputeMipDimension(meta.width, mip);
      const auto mip_height = ComputeMipDimension(meta.height, mip);
      const auto row_pitch = ComputeRowPitch(mip_width, meta.format);
      const auto slice_size
        = ComputeSlicePitch(mip_width, mip_height, meta.format);

      image.subresources_[index] = SubresourceInfo {
        .offset = total_size,
        .row_pitch = row_pitch,
        .width = mip_width,
        .height = mip_height,
      };

      total_size += slice_size;
    }
  }

  // Allocate storage
  image.storage_.resize(total_size);

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
  const auto slice_size
    = ComputeSlicePitch(info.width, info.height, meta_.format);

  return ImageView {
    .width = info.width,
    .height = info.height,
    .format = meta_.format,
    .row_pitch_bytes = info.row_pitch,
    .pixels
    = std::span<const std::byte>(storage_.data() + info.offset, slice_size),
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

  const auto slice_size
    = ComputeSlicePitch(info.width, info.height, meta_.format);

  return std::span<std::byte>(storage_.data() + info.offset, slice_size);
}

} // namespace oxygen::content::import
