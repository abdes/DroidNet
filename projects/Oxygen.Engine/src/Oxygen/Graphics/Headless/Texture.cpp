//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Detail/FormatUtils.h>
#include <Oxygen/Graphics/Headless/Texture.h>
#include <algorithm>
#include <compare>
#include <cstdint>
#include <cstring>
#include <fmt/format.h>
#include <limits>

namespace oxygen::graphics::headless {

Texture::Texture(const TextureDesc& desc)
  : graphics::Texture("HeadlessTexture")
  , desc_(desc)
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
      return (std::max)(1u, base >> mip);
    }

    auto ComputeMipSizeBytes(const TextureDesc& desc, uint32_t mip) const
      -> uint32_t override
    {
      const auto finfo = detail::GetFormatInfo(desc.format);
      const uint32_t w = MipDim(desc.width, mip);
      const uint32_t h = MipDim(desc.height, mip);
      if (finfo.block_size > 1) {
        const uint32_t blocks_x = (w + finfo.block_size - 1) / finfo.block_size;
        const uint32_t blocks_y = (h + finfo.block_size - 1) / finfo.block_size;
        // bytes_per_block is small; multiplication fits in 32 bits for
        // realistic textures used in tests. Use 64-bit temporaries to avoid
        // UB, then clamp to uint32_t.
        const uint64_t v = static_cast<uint64_t>(blocks_x)
          * static_cast<uint64_t>(blocks_y) * finfo.bytes_per_block;
        return static_cast<uint32_t>(v);
      }
      const uint64_t v = static_cast<uint64_t>(w) * static_cast<uint64_t>(h)
        * finfo.bytes_per_block;
      return static_cast<uint32_t>(v);
    }

    auto ComputeTotalBytesPerArraySlice(const TextureDesc& desc) const
      -> uint32_t override
    {
      uint64_t total = 0;
      for (uint32_t m = 0; m < desc.mip_levels; ++m) {
        total += ComputeMipSizeBytes(desc, m);
      }
      return static_cast<uint32_t>(total);
    }

    auto ComputeSliceMipBaseOffset(const TextureDesc& desc,
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
  const uint32_t layers = std::max<uint32_t>(1u, desc_.array_size);
  const uint64_t bytes = static_cast<uint64_t>(per_slice) * layers;
  constexpr uint64_t kMaxBacking = 1024ull * 1024ull * 128ull; // 128MB cap
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
  const uint32_t to_copy = std::min<uint32_t>(size, avail - src_offset);
  std::memcpy(dst, data_.data() + src_offset, to_copy);
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
  const uint32_t to_copy = std::min<uint32_t>(size, avail - dst_offset);
  std::memcpy(data_.data() + dst_offset, src, to_copy);
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
  return NativeResource(const_cast<Texture*>(this), ClassTypeId());
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
auto Texture::CreateShaderResourceView(const DescriptorHandle& /*view_handle*/,
  Format /*format*/, TextureType /*dimension*/,
  TextureSubResourceSet sub_resources) const -> NativeView
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
  (void)total_size; // debug log removed

  void* raw = operator new(sizeof(SRV));
  auto typed = new (raw) SRV { this, Format::kUnknown, TextureType::kTexture2D,
    resolved, base_offset, total_size };
  const void* payload_ptr = typed;
  owned_view_payloads_.emplace_back(raw, [](void* p) {
    if (p) {
      static_cast<SRV*>(p)->~SRV();
      operator delete(p);
    }
  });
  return NativeView(const_cast<void*>(payload_ptr), ClassTypeId());
}

auto Texture::CreateUnorderedAccessView(const DescriptorHandle& /*view_handle*/,
  Format /*format*/, TextureType /*dimension*/,
  TextureSubResourceSet sub_resources) const -> NativeView
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

  void* raw = operator new(sizeof(UAV));
  auto typed = new (raw) UAV { this, Format::kUnknown, TextureType::kTexture2D,
    resolved, base_offset, total_size };
  const void* payload_ptr = typed;
  owned_view_payloads_.emplace_back(raw, [](void* p) {
    if (p) {
      static_cast<UAV*>(p)->~UAV();
      operator delete(p);
    }
  });
  return NativeView(const_cast<void*>(payload_ptr), ClassTypeId());
}

auto Texture::CreateRenderTargetView(const DescriptorHandle& /*view_handle*/,
  Format /*format*/, TextureSubResourceSet sub_resources) const -> NativeView
{
  // Resolve subresources for RTV (often entire texture or a single mip)
  const auto resolved = sub_resources.Resolve(desc_, /*single_mip_level=*/true);
  void* raw = operator new(sizeof(RTV));
  auto typed = new (raw)
    RTV { this, Format::kUnknown, TextureType::kTexture2D, resolved };
  const void* payload_ptr = typed;
  owned_view_payloads_.emplace_back(raw, [](void* p) {
    if (p) {
      static_cast<RTV*>(p)->~RTV();
      operator delete(p);
    }
  });
  return NativeView(const_cast<void*>(payload_ptr), ClassTypeId());
}

auto Texture::CreateDepthStencilView(const DescriptorHandle& /*view_handle*/,
  Format /*format*/, TextureSubResourceSet sub_resources,
  bool is_read_only) const -> NativeView
{
  const auto resolved = sub_resources.Resolve(desc_, /*single_mip_level=*/true);
  void* raw = operator new(sizeof(DSV));
  auto typed = new (raw) DSV { this, Format::kUnknown, TextureType::kTexture2D,
    resolved, is_read_only };
  const void* payload_ptr = typed;
  owned_view_payloads_.emplace_back(raw, [](void* p) {
    if (p) {
      static_cast<DSV*>(p)->~DSV();
      operator delete(p);
    }
  });
  return NativeView(const_cast<void*>(payload_ptr), ClassTypeId());
}

} // namespace oxygen::graphics::headless
