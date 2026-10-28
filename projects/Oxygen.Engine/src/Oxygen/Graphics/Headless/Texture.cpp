//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#ifndef NDEBUG
#  include <limits>
#endif
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Detail/FormatUtils.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Graphics/Common/DescriptorHandle.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ResourceAccessMode.h>
#include <Oxygen/Graphics/Headless/Texture.h>

namespace oxygen::graphics::headless {

Texture::Texture(TextureDesc desc)
  : graphics::Texture("HeadlessTexture")
  , desc_(std::move(desc))
  , is_readback_surface_(desc_.cpu_access == ResourceAccessMode::kReadBack)
{
  // Determine backing size using the layout strategy so that all mip/data
  // ranges computed by the strategy are addressable via Read/WriteBacking.
  struct ContiguousLayout : TextureLayoutStrategy {

    static auto MipDim(uint32_t base, uint32_t mip)
    {
      // shifting by >= width of type is undefined; guard against large mip
      if (mip >= 32) {
        LOG_F(WARNING,
          "ContiguousLayout::MipDim: mip ({}) >= 32, clamping to 31", mip);
        mip = 31;
      }
      return (std::max)(1U, base >> mip);
    }

    [[nodiscard]] auto ComputeMipSizeBytes(
      const TextureDesc& desc, uint32_t mip) const -> uint32_t override
    {
      const auto finfo = detail::GetFormatInfo(desc.format);
      const uint32_t w = MipDim(desc.width, mip);
      const uint32_t h = MipDim(desc.height, mip);
      const uint32_t d = MipDim(desc.depth, mip);
      if (finfo.block_size > 1) {
        const uint32_t blocks_x = (w + finfo.block_size - 1) / finfo.block_size;
        const uint32_t blocks_y = (h + finfo.block_size - 1) / finfo.block_size;
        // bytes_per_block is small; multiplication fits in 32 bits for
        // realistic textures used in tests. Use 64-bit temporaries to avoid
        // UB, then clamp to uint32_t.
        const uint64_t v = static_cast<uint64_t>(blocks_x)
          * static_cast<uint64_t>(blocks_y) * static_cast<uint64_t>(d)
          * finfo.bytes_per_block;
        return static_cast<uint32_t>(v);
      }
      const uint64_t v = static_cast<uint64_t>(w) * static_cast<uint64_t>(h)
        * static_cast<uint64_t>(d) * finfo.bytes_per_block;
      return static_cast<uint32_t>(v);
    }

    [[nodiscard]] auto ComputeTotalBytesPerArraySlice(
      const TextureDesc& desc) const -> uint32_t override
    {
      uint64_t total = 0;
      for (uint32_t m = 0; m < desc.mip_levels; ++m) {
        total += ComputeMipSizeBytes(desc, m);
      }
      return static_cast<uint32_t>(total);
    }

    // The parameter order is the TextureLayoutStrategy interface's.
    [[nodiscard]] auto ComputeSliceMipBaseOffset(const TextureDesc& desc,
      // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
      uint32_t array_slice, uint32_t mip) const -> uint32_t override
    {
      const uint64_t per_slice = ComputeTotalBytesPerArraySlice(desc);
      uint64_t offset = static_cast<uint64_t>(array_slice) * per_slice;
      for (uint32_t m = 0; m < mip; ++m) {
        offset += ComputeMipSizeBytes(desc, m);
      }
      return static_cast<uint32_t>(offset);
    }
  };

  // Compute full backing using the same layout strategy used by this class.
  ContiguousLayout layout_tmp;
  const uint32_t per_slice = layout_tmp.ComputeTotalBytesPerArraySlice(desc_);
  const uint32_t layers = std::max<uint32_t>(1U, desc_.array_size);
  const uint64_t bytes = static_cast<uint64_t>(per_slice) * layers;
  if (desc_.allocation_budget.owner) {
    budget_reservation_ = desc_.allocation_budget.owner->Reserve(
      SizeBytes { bytes }, desc_.allocation_budget.category);
  }
  constexpr uint64_t kMaxBacking = 1024ULL * 1024ULL * 128ULL; // 128MB cap
  if (bytes > 0 && bytes <= kMaxBacking) {
    data_.resize(bytes);
  }
  // Instantiate strategy (same type as layout_tmp)
  layout_strategy_ = std::make_unique<ContiguousLayout>();
}

auto Texture::ReadBacking(void* dst, uint32_t src_offset, uint32_t size) const
  -> void
{
  if (data_.empty()) {
    return;
  }
  const uint32_t avail = GetBackingSize();
  if (src_offset >= avail) {
    LOG_F(WARNING, "Texture::ReadBacking: src_offset out of range");
    return;
  }
  const auto source = std::span(data_).subspan(
    src_offset, std::min<uint32_t>(size, avail - src_offset));
  std::memcpy(dst, source.data(), source.size());
}

auto Texture::WriteBacking(const void* src, uint32_t dst_offset, uint32_t size)
  -> void
{
  if (data_.empty()) {
    return;
  }
  const uint32_t avail = GetBackingSize();
  if (dst_offset >= avail) {
    LOG_F(WARNING, "Texture::WriteBacking: dst_offset out of range");
    return;
  }
  const auto target = std::span(data_).subspan(
    dst_offset, std::min<uint32_t>(size, avail - dst_offset));
  std::memcpy(target.data(), src, target.size());
}

auto Texture::GetDescriptor() const -> const TextureDesc& { return desc_; }

auto Texture::GetBackingSize() const -> uint32_t
{
  const uint64_t sz = data_.size();
  DCHECK_LE_F(sz, static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()));
  return static_cast<uint32_t>(sz);
}

auto Texture::GetNativeResource() const -> NativeResource
{
  return { this, ClassTypeId() };
}

auto Texture::GetLayoutStrategy() const -> const TextureLayoutStrategy&
{
  return *layout_strategy_;
}

//! View payloads created here are owned by the Texture instance.
/*!
 The returned `NativeObject` is a non-owning pointer into the owned payload
 storage inside the `Texture`. The `ResourceRegistry` may cache the
 `NativeObject` value, but it must not assume ownership of the payload memory.
 Unregister views before destroying the texture or transfer ownership to the
 registry if views must outlive the resource.
*/
auto Texture::CreateShaderResourceView(
  const DescriptorAllocationHandle& /*view_handle*/, Format /*format*/,
  TextureType /*dimension*/, TextureSubResourceSet sub_resources) const
  -> NativeView
{
  // Resolve subresource set to concrete ranges and compute byte ranges.
  const auto resolved
    = sub_resources.Resolve(desc_, /*single_mip_level=*/false);
  const auto& strat = *layout_strategy_;
  // Compute base offset at requested base mip/array
  const uint32_t base_offset = strat.ComputeSliceMipBaseOffset(
    desc_, resolved.base_array_slice, resolved.base_mip_level);
  // Compute total size by summing the requested mips and array slices
  uint32_t total_size = 0;
  for (uint32_t s = 0; s < resolved.num_array_slices; ++s) {
    for (uint32_t m = 0; m < resolved.num_mip_levels; ++m) {
      total_size
        += strat.ComputeMipSizeBytes(desc_, resolved.base_mip_level + m);
    }
  }

  return OwnView(SRV {
    {
      .texture = this,
      .format = Format::kUnknown,
      .dimension = TextureType::kTexture2D,
      .subresources = resolved,
    },
    base_offset,
    total_size,
  });
}

auto Texture::CreateUnorderedAccessView(
  const DescriptorAllocationHandle& /*view_handle*/, Format /*format*/,
  TextureType /*dimension*/, TextureSubResourceSet sub_resources) const
  -> NativeView
{
  const auto resolved
    = sub_resources.Resolve(desc_, /*single_mip_level=*/false);
  const auto& strat = *layout_strategy_;
  const uint32_t base_offset = strat.ComputeSliceMipBaseOffset(
    desc_, resolved.base_array_slice, resolved.base_mip_level);
  uint32_t total_size = 0;
  for (uint32_t s = 0; s < resolved.num_array_slices; ++s) {
    for (uint32_t m = 0; m < resolved.num_mip_levels; ++m) {
      total_size
        += strat.ComputeMipSizeBytes(desc_, resolved.base_mip_level + m);
    }
  }

  return OwnView(UAV {
    {
      .texture = this,
      .format = Format::kUnknown,
      .dimension = TextureType::kTexture2D,
      .subresources = resolved,
    },
    base_offset,
    total_size,
  });
}

auto Texture::CreateRenderTargetView(
  const DescriptorAllocationHandle& /*view_handle*/, Format /*format*/,
  TextureSubResourceSet sub_resources) const -> NativeView
{
  // Resolve subresources for RTV (often entire texture or a single mip)
  const auto resolved = sub_resources.Resolve(desc_, /*single_mip_level=*/true);
  return OwnView(RTV {
    {
      .texture = this,
      .format = Format::kUnknown,
      .dimension = TextureType::kTexture2D,
      .subresources = resolved,
    },
  });
}

auto Texture::CreateDepthStencilView(
  const DescriptorAllocationHandle& /*view_handle*/, Format /*format*/,
  TextureSubResourceSet sub_resources, bool is_read_only) const -> NativeView
{
  const auto resolved = sub_resources.Resolve(desc_, /*single_mip_level=*/true);
  return OwnView(DSV {
    {
      .texture = this,
      .format = Format::kUnknown,
      .dimension = TextureType::kTexture2D,
      .subresources = resolved,
    },
    is_read_only,
  });
}

} // namespace oxygen::graphics::headless
