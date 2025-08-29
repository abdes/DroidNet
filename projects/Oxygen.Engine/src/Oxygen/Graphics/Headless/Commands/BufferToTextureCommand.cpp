//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Detail/FormatUtils.h>
#include <Oxygen/Graphics/Headless/Buffer.h>
#include <Oxygen/Graphics/Headless/Commands/BufferToTextureCommand.h>
#include <Oxygen/Graphics/Headless/Texture.h>

using oxygen::graphics::detail::FormatInfo;
using oxygen::graphics::detail::GetFormatInfo;

namespace oxygen::graphics::headless {

BufferToTextureCommand::BufferToTextureCommand(const graphics::Buffer* src,
  const TextureUploadRegion& region, graphics::Texture* dst)
  : src_(src)
  , region_(region)
  , dst_(static_cast<Texture*>(dst))
{
}

auto BufferToTextureCommand::Execute(CommandContext& /*ctx*/) -> void
{
  auto src_h = static_cast<const Buffer*>(src_);
  auto dst_h = static_cast<Texture*>(dst_);
  if (src_h == nullptr || dst_h == nullptr) {
    LOG_F(WARNING, "Headless BufferToTexture: non-headless resources");
    return;
  }

  // Resolve destination slice.
  auto dst_desc = dst_->GetDescriptor();
  auto dst_slice = region_.dst_slice.Resolve(dst_desc);
  auto subresources = region_.dst_subresources.Resolve(dst_desc, true);

  // Get format info
  const auto fmt = dst_desc.format;
  const auto& finfo = GetFormatInfo(fmt);

  const bool is_block = finfo.block_size > 1;
  if (!is_block) {
    const size_t bytes_per_pixel
      = finfo
          .bytes_per_block; // for non-block formats this holds bytes per texel
    const size_t texture_row_pitch = dst_slice.width * bytes_per_pixel;
    const size_t buffer_row_pitch
      = region_.buffer_row_pitch ? region_.buffer_row_pitch : texture_row_pitch;
    const size_t buffer_slice_pitch = region_.buffer_slice_pitch
      ? region_.buffer_slice_pitch
      : (buffer_row_pitch * dst_slice.height);

    // For each array slice / mip in the resolved subresources, copy rows.
    for (uint32_t s = 0; s < subresources.num_array_slices; ++s) {
      for (uint32_t y = 0; y < dst_slice.height; ++y) {
        const size_t row_index_in_buffer = region_.buffer_offset
          + s * buffer_slice_pitch + y * buffer_row_pitch;

        // Destination byte offset: use backend-provided layout if available.
        const auto& layout = dst_h->GetLayoutStrategy();
        size_t row_index_in_texture = 0;
        // Compute base offset for this array slice + mip
        const size_t mip_base = layout.ComputeSliceMipBaseOffset(
          dst_desc, dst_slice.array_slice + s, dst_slice.mip_level);
        // Per-row offset within the mip
        const size_t row_offset = static_cast<size_t>(dst_slice.y + y)
          * static_cast<size_t>(dst_slice.width) * bytes_per_pixel;
        row_index_in_texture
          = mip_base + row_offset + (dst_slice.x * bytes_per_pixel);

        std::vector<uint8_t> row(texture_row_pitch);
        src_h->ReadBacking(row.data(), row_index_in_buffer, texture_row_pitch);
        dst_h->WriteBacking(
          row.data(), row_index_in_texture, texture_row_pitch);
      }
    }
  } else {
    // Block-compressed: operate on block units (e.g., 4x4 blocks)
    const uint32_t block_w = finfo.block_size;
    const uint32_t block_h = finfo.block_size;
    const size_t bytes_per_block = finfo.bytes_per_block;

    // Compute number of blocks horizontally and vertically (ceil)
    const uint32_t blocks_x = (dst_slice.width + block_w - 1) / block_w;
    const uint32_t blocks_y = (dst_slice.height + block_h - 1) / block_h;

    // Each row in buffer now corresponds to blocks_x * bytes_per_block
    const size_t texture_block_row_pitch = blocks_x * bytes_per_block;
    const size_t buffer_row_pitch = region_.buffer_row_pitch
      ? region_.buffer_row_pitch
      : texture_block_row_pitch;
    const size_t buffer_slice_pitch = region_.buffer_slice_pitch
      ? region_.buffer_slice_pitch
      : (buffer_row_pitch * blocks_y);

    const auto& layout = dst_h->GetLayoutStrategy();

    for (uint32_t s = 0; s < subresources.num_array_slices; ++s) {
      const uint32_t array_index = dst_slice.array_slice + s;
      const uint32_t mip = dst_slice.mip_level;

      // Precompute mip-specific values
      const uint32_t mip_w = (std::max)(1u, dst_desc.width >> mip);
      const uint32_t mip_blocks_x = (mip_w + block_w - 1) / block_w;

      for (uint32_t by = 0; by < blocks_y; ++by) {
        const size_t row_index_in_buffer = region_.buffer_offset
          + s * buffer_slice_pitch + by * buffer_row_pitch;

        // Compute destination offset using layout when available.
        size_t row_index_in_texture = 0;
        const size_t mip_base
          = layout.ComputeSliceMipBaseOffset(dst_desc, array_index, mip);
        row_index_in_texture = mip_base
          + static_cast<size_t>(by) * static_cast<size_t>(mip_blocks_x)
            * bytes_per_block;

        std::vector<uint8_t> row(texture_block_row_pitch);
        src_h->ReadBacking(
          row.data(), row_index_in_buffer, texture_block_row_pitch);
        dst_h->WriteBacking(
          row.data(), row_index_in_texture, texture_block_row_pitch);
      }
    }
  }
}

} // namespace oxygen::graphics::headless
