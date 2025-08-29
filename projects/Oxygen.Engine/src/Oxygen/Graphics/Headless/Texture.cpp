//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Detail/FormatUtils.h>
#include <Oxygen/Graphics/Headless/Texture.h>
#include <algorithm>
#include <cstdint>
#include <cstring>

namespace oxygen::graphics::headless {

Texture::Texture(const TextureDesc& desc)
  : graphics::Texture("HeadlessTexture")
  , desc_(desc)
{
  // Conservative backing allocation: width * height * array_size * 4 bytes.
  const uint64_t w = std::max<uint32_t>(1u, desc_.width);
  const uint64_t h = std::max<uint32_t>(1u, desc_.height);
  const uint64_t layers = std::max<uint32_t>(1u, desc_.array_size);
  const uint64_t pixels = w * h * layers;
  // 4 bytes per pixel estimate when format is unknown; keep size bounded.
  const uint64_t bytes = pixels * 4ull;
  if (bytes > 0 && bytes <= 1024ull * 1024ull * 128ull) { // cap at 128MB
    data_.resize(bytes);
  }

  struct ContiguousLayout : TextureLayoutStrategy {

    static auto MipDim(uint32_t base, uint32_t mip)
    {
      return (std::max)(1u, base >> mip);
    }

    auto ComputeMipSizeBytes(const TextureDesc& desc, uint32_t mip) const
      -> size_t override
    {
      const auto finfo = detail::GetFormatInfo(desc.format);
      const uint32_t w = MipDim(desc.width, mip);
      const uint32_t h = MipDim(desc.height, mip);
      if (finfo.block_size > 1) {
        const uint32_t blocks_x = (w + finfo.block_size - 1) / finfo.block_size;
        const uint32_t blocks_y = (h + finfo.block_size - 1) / finfo.block_size;
        return static_cast<size_t>(blocks_x) * static_cast<size_t>(blocks_y)
          * finfo.bytes_per_block;
      }
      return static_cast<size_t>(w) * static_cast<size_t>(h)
        * finfo.bytes_per_block;
    }

    auto ComputeTotalBytesPerArraySlice(const TextureDesc& desc) const
      -> size_t override
    {
      size_t total = 0;
      for (uint32_t m = 0; m < desc.mip_levels; ++m) {
        total += ComputeMipSizeBytes(desc, m);
      }
      return total;
    }

    auto ComputeSliceMipBaseOffset(const TextureDesc& desc,
      uint32_t array_slice, uint32_t mip) const -> size_t override
    {
      const size_t per_slice = ComputeTotalBytesPerArraySlice(desc);
      size_t offset = static_cast<size_t>(array_slice) * per_slice;
      for (uint32_t m = 0; m < mip; ++m) {
        offset += ComputeMipSizeBytes(desc, m);
      }
      return offset;
    }
  };

  // Instantiate strategy
  layout_strategy_ = std::make_unique<ContiguousLayout>();
}

auto Texture::ReadBacking(void* dst, size_t src_offset, size_t size) const
  -> void
{
  if (data_.empty()) {
    return;
  }
  const size_t avail = data_.size();
  if (src_offset >= avail) {
    LOG_F(WARNING, "Texture::ReadBacking: src_offset out of range");
    return;
  }
  const size_t to_copy = std::min(size, avail - src_offset);
  std::memcpy(dst, data_.data() + src_offset, to_copy);
}

auto Texture::WriteBacking(const void* src, size_t dst_offset, size_t size)
  -> void
{
  if (data_.empty()) {
    return;
  }
  const size_t avail = data_.size();
  if (dst_offset >= avail) {
    LOG_F(WARNING, "Texture::WriteBacking: dst_offset out of range");
    return;
  }
  const size_t to_copy = std::min(size, avail - dst_offset);
  std::memcpy(data_.data() + dst_offset, src, to_copy);
}

[[nodiscard]] auto Texture::GetDescriptor() const -> const TextureDesc&
{
  return desc_;
}

[[nodiscard]] auto Texture::GetNativeResource() const -> NativeObject
{
  return NativeObject(const_cast<Texture*>(this), ClassTypeId());
}

[[nodiscard]] auto Texture::GetLayoutStrategy() const
  -> const TextureLayoutStrategy&
{
  return *layout_strategy_;
}

[[nodiscard]] auto Texture::CreateShaderResourceView(
  const DescriptorHandle& /*view_handle*/, Format /*format*/,
  TextureType /*dimension*/, TextureSubResourceSet /*sub_resources*/) const
  -> NativeObject
{
  return {};
}

[[nodiscard]] auto Texture::CreateUnorderedAccessView(
  const DescriptorHandle& /*view_handle*/, Format /*format*/,
  TextureType /*dimension*/, TextureSubResourceSet /*sub_resources*/) const
  -> NativeObject
{
  return {};
}

[[nodiscard]] auto Texture::CreateRenderTargetView(
  const DescriptorHandle& /*view_handle*/, Format /*format*/,
  TextureSubResourceSet /*sub_resources*/) const -> NativeObject
{
  return {};
}

[[nodiscard]] auto Texture::CreateDepthStencilView(
  const DescriptorHandle& /*view_handle*/, Format /*format*/,
  TextureSubResourceSet /*sub_resources*/, bool /*is_read_only*/) const
  -> NativeObject
{
  return {};
}

} // namespace oxygen::graphics::headless
