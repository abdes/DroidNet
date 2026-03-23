//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Detail/FormatUtils.h>
#include <Oxygen/Graphics/Headless/Buffer.h>
#include <Oxygen/Graphics/Headless/Commands/TextureToBufferCommand.h>
#include <Oxygen/Graphics/Headless/Texture.h>

using oxygen::graphics::detail::FormatInfo;
using oxygen::graphics::detail::GetFormatInfo;

namespace {

auto BlocksX(const FormatInfo& info, const uint32_t width_texels) -> uint32_t
{
  return (width_texels + info.block_size - 1U) / info.block_size;
}

auto BlocksY(const FormatInfo& info, const uint32_t height_texels) -> uint32_t
{
  return (height_texels + info.block_size - 1U) / info.block_size;
}

auto ValidateSliceBounds(const oxygen::graphics::TextureSlice& slice,
  const uint32_t mip_width, const uint32_t mip_height, const uint32_t mip_depth)
  -> void
{
  CHECK_LE_F(static_cast<uint64_t>(slice.x) + slice.width,
    static_cast<uint64_t>(mip_width),
    "Texture copy width exceeds mip bounds: x={} width={} mip_width={}",
    slice.x, slice.width, mip_width);
  CHECK_LE_F(static_cast<uint64_t>(slice.y) + slice.height,
    static_cast<uint64_t>(mip_height),
    "Texture copy height exceeds mip bounds: y={} height={} mip_height={}",
    slice.y, slice.height, mip_height);
  CHECK_LE_F(static_cast<uint64_t>(slice.z) + slice.depth,
    static_cast<uint64_t>(mip_depth),
    "Texture copy depth exceeds mip bounds: z={} depth={} mip_depth={}",
    slice.z, slice.depth, mip_depth);
}

} // namespace

namespace oxygen::graphics::headless {

TextureToBufferCommand::TextureToBufferCommand(graphics::Buffer* dst,
  const graphics::Texture* src, TextureBufferCopyRegion region)
  : dst_(static_cast<Buffer*>(dst))
  , src_(src)
  , region_(region)
{
}

auto TextureToBufferCommand::DoExecute(CommandContext& /*ctx*/) -> void
{
  auto dst_h = static_cast<Buffer*>(dst_);
  auto src_h = static_cast<const Texture*>(src_);
  if (dst_h == nullptr || src_h == nullptr) {
    LOG_F(WARNING, "Headless TextureToBuffer: non-headless resources");
    return;
  }

  const auto src_desc = src_h->GetDescriptor();
  const auto resolved_region
    = ResolveTextureBufferCopyRegion(src_desc, region_);
  const auto slice = resolved_region.texture_slice;
  const auto tight_footprint
    = ComputeLinearTextureCopyFootprint(src_desc, slice);
  const auto& layout = src_h->GetLayoutStrategy();
  const auto& finfo = GetFormatInfo(src_desc.format);
  const auto mip = slice.mip_level;
  const auto mip_width = (std::max)(1u, src_desc.width >> mip);
  const auto mip_height = (std::max)(1u, src_desc.height >> mip);
  const auto mip_depth = (std::max)(1u, src_desc.depth >> mip);
  const auto mip_base
    = layout.ComputeSliceMipBaseOffset(src_desc, slice.array_slice, mip);

  ValidateSliceBounds(slice, mip_width, mip_height, mip_depth);

  const auto copy_bytes_per_row = tight_footprint.row_pitch.get();
  std::vector<uint8_t> row(static_cast<size_t>(copy_bytes_per_row));

  if (finfo.block_size == 1) {
    const auto bytes_per_pixel = static_cast<uint32_t>(finfo.bytes_per_block);
    const auto texture_row_stride = mip_width * bytes_per_pixel;
    const auto texture_depth_stride = texture_row_stride * mip_height;

    for (auto z = 0u; z < slice.depth; ++z) {
      for (auto y = 0u; y < slice.height; ++y) {
        const auto texture_offset = static_cast<uint64_t>(mip_base)
          + static_cast<uint64_t>(slice.z + z) * texture_depth_stride
          + static_cast<uint64_t>(slice.y + y) * texture_row_stride
          + static_cast<uint64_t>(slice.x) * bytes_per_pixel;
        const auto buffer_offset = resolved_region.buffer_offset.get()
          + static_cast<uint64_t>(z) * resolved_region.buffer_slice_pitch.get()
          + static_cast<uint64_t>(y) * resolved_region.buffer_row_pitch.get();

        src_h->ReadBacking(row.data(), static_cast<uint32_t>(texture_offset),
          static_cast<uint32_t>(copy_bytes_per_row));
        dst_h->WriteBacking(row.data(), buffer_offset, copy_bytes_per_row);
      }
    }
    return;
  }

  const auto block_width = static_cast<uint32_t>(finfo.block_size);
  const auto block_height = static_cast<uint32_t>(finfo.block_size);
  const auto bytes_per_block = static_cast<uint32_t>(finfo.bytes_per_block);
  CHECK_EQ_F(slice.x % block_width, 0u,
    "Block-compressed copies require block-aligned x: x={} block_width={}",
    slice.x, block_width);
  CHECK_EQ_F(slice.y % block_height, 0u,
    "Block-compressed copies require block-aligned y: y={} block_height={}",
    slice.y, block_height);

  const auto mip_blocks_x = BlocksX(finfo, mip_width);
  const auto mip_blocks_y = BlocksY(finfo, mip_height);
  const auto copy_block_x = slice.x / block_width;
  const auto copy_block_y = slice.y / block_height;
  const auto texture_block_row_stride = mip_blocks_x * bytes_per_block;
  const auto texture_depth_stride = texture_block_row_stride * mip_blocks_y;

  for (auto z = 0u; z < slice.depth; ++z) {
    for (auto row_index = 0u; row_index < tight_footprint.row_count;
      ++row_index) {
      const auto texture_offset = static_cast<uint64_t>(mip_base)
        + static_cast<uint64_t>(slice.z + z) * texture_depth_stride
        + static_cast<uint64_t>(copy_block_y + row_index)
          * texture_block_row_stride
        + static_cast<uint64_t>(copy_block_x) * bytes_per_block;
      const auto buffer_offset = resolved_region.buffer_offset.get()
        + static_cast<uint64_t>(z) * resolved_region.buffer_slice_pitch.get()
        + static_cast<uint64_t>(row_index)
          * resolved_region.buffer_row_pitch.get();

      src_h->ReadBacking(row.data(), static_cast<uint32_t>(texture_offset),
        static_cast<uint32_t>(copy_bytes_per_row));
      dst_h->WriteBacking(row.data(), buffer_offset, copy_bytes_per_row);
    }
  }
}

} // namespace oxygen::graphics::headless
