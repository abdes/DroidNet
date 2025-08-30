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
#include <cstdint>
#include <limits>

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
  // Doing arithmetic directly in uint32_t as requested by caller.

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
    auto bytes_per_pixel = static_cast<uint32_t>(finfo.bytes_per_block);
    auto texture_row_pitch = dst_slice.width * bytes_per_pixel;
    auto buffer_row_pitch
      = region_.buffer_row_pitch ? region_.buffer_row_pitch : texture_row_pitch;
    auto buffer_slice_pitch = region_.buffer_slice_pitch
      ? region_.buffer_slice_pitch
      : buffer_row_pitch * dst_slice.height;

    // For each array slice / mip in the resolved subresources, copy rows.
    for (auto s = 0u; s < subresources.num_array_slices; ++s) {
      for (auto y = 0u; y < dst_slice.height; ++y) {
        const auto row_index_in_buffer = region_.buffer_offset
          + (s * buffer_slice_pitch) + (y * buffer_row_pitch);

        // Destination byte offset: use backend-provided layout if available.
        const auto& layout = dst_h->GetLayoutStrategy();
        // compute destination offsets directly as uint32_t
        // Compute base offset for this array slice + mip
        const auto mip_base = layout.ComputeSliceMipBaseOffset(
          dst_desc, dst_slice.array_slice + s, dst_slice.mip_level);
        // Per-row offset within the mip (direct uint32_t arithmetic)
        const auto row_index_in_texture = mip_base
          + ((dst_slice.y + y) * dst_slice.width * bytes_per_pixel)
          + (dst_slice.x * bytes_per_pixel);

        std::vector<uint8_t> row(texture_row_pitch);
        src_h->ReadBacking(row.data(), row_index_in_buffer, texture_row_pitch);
        dst_h->WriteBacking(
          row.data(), row_index_in_texture, texture_row_pitch);
      }
    }
  } else {
    // Block-compressed: operate on block units (e.g., 4x4 blocks)
    auto block_w = static_cast<uint32_t>(finfo.block_size);
    auto block_h = static_cast<uint32_t>(finfo.block_size);
    auto bytes_per_block = static_cast<uint32_t>(finfo.bytes_per_block);

    // Compute number of blocks horizontally and vertically (ceil)
    auto blocks_x = (dst_slice.width + block_w - 1) / block_w;
    auto blocks_y = (dst_slice.height + block_h - 1) / block_h;

    // Each row in buffer now corresponds to blocks_x * bytes_per_block
    auto texture_block_row_pitch = blocks_x * bytes_per_block;
    auto buffer_row_pitch = region_.buffer_row_pitch ? region_.buffer_row_pitch
                                                     : texture_block_row_pitch;
    auto buffer_slice_pitch = region_.buffer_slice_pitch
      ? region_.buffer_slice_pitch
      : (buffer_row_pitch * blocks_y);

    const auto& layout = dst_h->GetLayoutStrategy();

    for (auto s = 0u; s < subresources.num_array_slices; ++s) {
      const auto array_index = dst_slice.array_slice + s;
      const auto mip = dst_slice.mip_level;

      // Precompute mip-specific values
      const auto mip_w = (std::max)(1u, dst_desc.width >> mip);
      const auto mip_blocks_x = (mip_w + block_w - 1) / block_w;

      for (auto by = 0u; by < blocks_y; ++by) {
        const auto row_index_in_buffer = region_.buffer_offset
          + (s * buffer_slice_pitch) + (by * buffer_row_pitch);

        // Compute destination offset using layout when available.
        const auto mip_base
          = layout.ComputeSliceMipBaseOffset(dst_desc, array_index, mip);
        const auto row_index_in_texture
          = mip_base + (by * mip_blocks_x * bytes_per_block);

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
