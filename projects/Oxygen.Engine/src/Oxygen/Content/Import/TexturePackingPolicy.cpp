//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Content/Import/TexturePackingPolicy.h>

#include <Oxygen/Core/Detail/FormatUtils.h>

namespace oxygen::content::import {

//===----------------------------------------------------------------------===//
// Singleton Instances
//===----------------------------------------------------------------------===//

auto D3D12PackingPolicy::Instance() noexcept -> const D3D12PackingPolicy&
{
  static const D3D12PackingPolicy instance;
  return instance;
}

auto TightPackedPolicy::Instance() noexcept -> const TightPackedPolicy&
{
  static const TightPackedPolicy instance;
  return instance;
}

//===----------------------------------------------------------------------===//
// Format Utilities
//===----------------------------------------------------------------------===//

auto ComputeBytesPerPixelOrBlock(const Format format) noexcept -> uint32_t
{
  const auto& info = graphics::detail::GetFormatInfo(format);
  return info.bytes_per_block;
}

auto ComputeBlockDimension(const Format format) noexcept -> uint32_t
{
  const auto& info = graphics::detail::GetFormatInfo(format);
  return info.block_size;
}

auto ComputeRowBytes(const uint32_t width, const Format format) noexcept
  -> uint32_t
{
  const auto& info = graphics::detail::GetFormatInfo(format);

  if (info.block_size == 1) {
    // Uncompressed format: width * bytes_per_pixel
    return width * info.bytes_per_block;
  }

  // Block-compressed format: ceil(width / block_size) * bytes_per_block
  const uint32_t blocks = (width + info.block_size - 1) / info.block_size;
  return blocks * info.bytes_per_block;
}

auto ComputeSurfaceBytes(const uint32_t width, const uint32_t height,
  const Format format) noexcept -> uint64_t
{
  const auto& info = graphics::detail::GetFormatInfo(format);

  if (info.block_size == 1) {
    // Uncompressed format
    return static_cast<uint64_t>(width) * height * info.bytes_per_block;
  }

  // Block-compressed format
  const uint32_t blocks_x = (width + info.block_size - 1) / info.block_size;
  const uint32_t blocks_y = (height + info.block_size - 1) / info.block_size;
  return static_cast<uint64_t>(blocks_x) * blocks_y * info.bytes_per_block;
}

//===----------------------------------------------------------------------===//
// Subresource Layout Computation
//===----------------------------------------------------------------------===//

//! Compute subresource layouts for texture packing.
/*!
  CRITICAL: Subresource ordering MUST be LAYER-MAJOR to match D3D12
  subresource indexing.

  D3D12 subresource indexing formula:
    SubresourceIndex = MipSlice + (ArraySlice * MipLevels)

  Subresources are indexed with mip varying fastest within each array slice.
  Increasing subresource index order is:
    Layer0/Mip0, Layer0/Mip1, ..., Layer0/MipN,
    Layer1/Mip0, Layer1/Mip1, ..., Layer1/MipN,
    ...

  This ordering MUST match the cooker packing logic and the runtime upload
  layout builder.
*/
auto ComputeSubresourceLayouts(const ScratchImageMeta& meta,
  const ITexturePackingPolicy& policy) -> std::vector<SubresourceLayout>
{
  const uint32_t total_subresources
    = static_cast<uint32_t>(meta.array_layers) * meta.mip_levels;
  std::vector<SubresourceLayout> layouts;
  layouts.reserve(total_subresources);

  uint64_t current_offset = 0;

  // D3D12 subresource indexing is layer-major (array slice major):
  //   SubresourceIndex = MipSlice + ArraySlice * MipLevels
  // So we iterate layer in outer loop, mip in inner loop.
  for (uint16_t layer = 0; layer < meta.array_layers; ++layer) {
    for (uint16_t mip = 0; mip < meta.mip_levels; ++mip) {
      SubresourceLayout layout;

      // Compute dimensions at this mip level
      layout.width = ComputeMipDimension(meta.width, mip);
      layout.height = ComputeMipDimension(meta.height, mip);
      layout.depth
        = ComputeMipDimension(static_cast<uint32_t>(meta.depth), mip);

      // Compute row pitch with alignment
      const uint32_t unaligned_row_bytes
        = ComputeRowBytes(layout.width, meta.format);
      layout.row_pitch = policy.AlignRowPitchBytes(unaligned_row_bytes);

      // Compute subresource size
      const auto& info = graphics::detail::GetFormatInfo(meta.format);
      if (info.block_size == 1) {
        // Uncompressed: row_pitch * height * depth
        layout.size_bytes = layout.row_pitch * layout.height * layout.depth;
      } else {
        // Block-compressed: row_pitch * blocks_y * depth
        const uint32_t blocks_y
          = (layout.height + info.block_size - 1) / info.block_size;
        layout.size_bytes = layout.row_pitch * blocks_y * layout.depth;
      }

      // Align offset for this subresource
      layout.offset = policy.AlignSubresourceOffset(current_offset);

      layouts.push_back(layout);

      // Update offset for next subresource
      current_offset = layout.offset + layout.size_bytes;
    }
  }

  return layouts;
}

auto ComputeTotalPayloadSize(
  const std::span<const SubresourceLayout> layouts) noexcept -> uint64_t
{
  if (layouts.empty()) {
    return 0;
  }

  const auto& last = layouts.back();
  return last.offset + last.size_bytes;
}

} // namespace oxygen::content::import
