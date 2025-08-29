//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include <Oxygen/Graphics/Common/Texture.h>
#include <vector>

namespace oxygen::graphics::headless {

// Strategy for computing texture subresource layout (bytes per-mip, offsets,
// etc.). Backends that manage different memory layouts should provide an
// implementation. This interface is intentionally lightweight and header-only.
struct TextureLayoutStrategy {
  virtual ~TextureLayoutStrategy() = default;

  // Compute number of bytes used by a single mip level in one array slice.
  virtual auto ComputeMipSizeBytes(const TextureDesc& desc, uint32_t mip) const
    -> size_t
    = 0;

  // Compute total bytes used by all mips in a single array slice.
  virtual auto ComputeTotalBytesPerArraySlice(const TextureDesc& desc) const
    -> size_t
    = 0;

  // Compute byte offset to the start of the specified mip within the
  // specified array slice.
  virtual auto ComputeSliceMipBaseOffset(
    const TextureDesc& desc, uint32_t array_slice, uint32_t mip) const -> size_t
    = 0;
};

class Texture final : public graphics::Texture {
public:
  // Nested headless-only contiguous layout strategy. Kept inside the
  // headless Texture so the implementation is not exported as a separate
  // public type.
  Texture(const TextureDesc& desc);
  ~Texture() override = default;

  [[nodiscard]] auto GetDescriptor() const -> const TextureDesc& override;
  [[nodiscard]] auto GetNativeResource() const -> NativeObject override;
  [[nodiscard]] auto GetLayoutStrategy() const -> const TextureLayoutStrategy&;

private:
  TextureDesc desc_ {};

  // CPU-side backing storage for texture data. Conservatively sized using a
  // 4 bytes-per-pixel estimate when format is unknown.
  std::vector<std::uint8_t> data_;

  // Headless-specific Contiguous layout strategy (owned by the texture)
  std::unique_ptr<TextureLayoutStrategy> layout_strategy_;

public:
  // Headless-only helpers to access CPU-side backing. Bounds-checked copies.
  auto ReadBacking(void* dst, size_t src_offset, size_t size) const -> void;
  auto WriteBacking(const void* src, size_t dst_offset, size_t size) -> void;

protected:
  [[nodiscard]] auto CreateShaderResourceView(
    const DescriptorHandle& view_handle, Format format, TextureType dimension,
    TextureSubResourceSet sub_resources) const -> NativeObject override;

  [[nodiscard]] auto CreateUnorderedAccessView(
    const DescriptorHandle& view_handle, Format format, TextureType dimension,
    TextureSubResourceSet sub_resources) const -> NativeObject override;

  [[nodiscard]] auto CreateRenderTargetView(const DescriptorHandle& view_handle,
    Format format, TextureSubResourceSet sub_resources) const
    -> NativeObject override;

  [[nodiscard]] auto CreateDepthStencilView(const DescriptorHandle& view_handle,
    Format format, TextureSubResourceSet sub_resources, bool is_read_only) const
    -> NativeObject override;
};

} // namespace oxygen::graphics::headless
