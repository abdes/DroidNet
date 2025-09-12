//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Headless/api_export.h>

namespace oxygen::graphics::headless {

//! Texture layout policy (interface)
/*!
 Brief interface used to map texture subresources (mip levels and array
 slices) into linear byte sizes and offsets for a CPU-side backing buffer.

 Implementations should be deterministic and non-throwing. All values are
 reported in bytes and are expected to fit in a 32-bit unsigned integer for
 headless backing allocations.

 Use this interface to provide an alternate layout strategy for tests or
 tools that need a predictable mapping from subresources to linear memory.

 @note
 - `ComputeMipSizeBytes()` returns the number of bytes required to store a
   single mip level for one array slice (accounting for block-compressed
   formats when applicable).
 - `ComputeTotalBytesPerArraySlice()` returns the sum of all mip sizes for a
   single array slice.
 - `ComputeSliceMipBaseOffset()` returns the byte offset to the start of the
   specified mip within the specified array slice (offset measured from the
   start of the array slice region).
*/
struct TextureLayoutStrategy {
  virtual ~TextureLayoutStrategy() = default;

  // Compute number of bytes used by a single mip level in one array slice.
  virtual auto ComputeMipSizeBytes(const TextureDesc& desc, uint32_t mip) const
    -> uint32_t
    = 0;

  // Compute total bytes used by all mips in a single array slice.
  virtual auto ComputeTotalBytesPerArraySlice(const TextureDesc& desc) const
    -> uint32_t
    = 0;

  // Compute byte offset to the start of the specified mip within the
  // specified array slice.
  virtual auto ComputeSliceMipBaseOffset(const TextureDesc& desc,
    uint32_t array_slice, uint32_t mip) const -> uint32_t
    = 0;
};

//! Headless CPU-backed Texture (detailed)
/*!
 Detailed headless `graphics::Texture` implementation that stores texture texel
 data in a contiguous CPU-side backing buffer and exposes a simple contiguous
 layout strategy used by the implementation to compute per-mip and
 per-array-slice offsets.

 ### Key Features

 - **CPU backing allocation**: backing size is computed using a contiguous
   layout strategy derived from `TextureDesc`. The implementation applies a hard
   cap of 128 MiB (kMaxBacking = 128 * 1024 * 1024) to avoid unbounded
   allocations in tests. If the computed size is zero or exceeds the cap the
   internal backing remains empty and `GetBackingSize()` returns zero.
 - **Contiguous layout strategy**: a built-in `ContiguousLayout` is used to
   compute per-mip sizes and per-slice base offsets. The internal helper
   `MipDim(base, mip)` clamps `mip` to 31 to avoid undefined behaviour when
   shifting by >= width of the type.
 - **Read/Write helpers**: `ReadBacking()` and `WriteBacking()` perform
   bounds-checked, clamped memcpy operations into/from the backing. Null
   pointers and zero sizes are ignored. These helpers are convenience APIs for
   tests and do not model GPU-side transfer or synchronization semantics.
 - **View payloads**: `Create*View()` allocates small POD view payloads
   (SRV/UAV/RTV/DSV) via raw allocation and stores them in
   `owned_view_payloads_`. Returned `NativeView` values are non-owning
   pointers into that storage and remain valid for the lifetime of the `Texture`
   instance.

 ### Usage Examples

 ```cpp
 // Create a simple 256x256 RGBA texture with one mip and one array slice.
 TextureDesc d{};
 d.width = 256; d.height = 256; d.format = Format::kR8G8B8A8_UNORM;
 d.mip_levels = 1; d.array_size = 1;
 headless::Texture t(d);

 // Read entire backing if allocated
 const uint32_t sz = t.GetBackingSize();
 if (sz) {
   std::vector<uint8_t> buf(sz);
   t.ReadBacking(buf.data(), 0, sz);
 }

 // Create a shader resource view for the full texture
 auto view = t.CreateShaderResourceView(DescriptorHandle{}, d.format,
   TextureType::kTexture2D, TextureSubResourceSet::Full());
 ```

 ### Performance Characteristics

 - Time Complexity: O(mip_levels * array_size) for layout computations and view
   size calculations.
 - Memory: Backing memory equals the sum of per-mip sizes times array size,
   clamped to 128 MiB by default.

 @note The class is optimized for determinism and predictability in tests, not
 for runtime performance of production renderers.

 @warning If an external `ResourceRegistry` caches `NativeView` pointers to
 view payloads, ensure those payloads remain valid (unregister views before
 destroying the `Texture` or transfer ownership of payloads to the registry).

 @see TextureLayoutStrategy, ReadBacking, WriteBacking
*/
class Texture final : public graphics::Texture {
public:
  // Public POD payload types for view payloads owned by the Texture.
  // These are headless-only helpers and provide a small, stable contract for
  // tests and the ResourceRegistry to interpret NativeView pointers.
  struct ViewBase {
    const Texture* texture;
    Format format;
    TextureType dimension;
    TextureSubResourceSet subresources;
  };

  struct SRV : ViewBase {
    uint32_t base_offset;
    uint32_t total_size;
  };

  struct UAV : ViewBase {
    uint32_t base_offset;
    uint32_t total_size;
  };

  struct RTV : ViewBase { };

  struct DSV : ViewBase {
    bool read_only;
  };

  // Nested headless-only contiguous layout strategy. Kept inside the
  // headless Texture so the implementation is not exported as a separate
  // public type.
  OXGN_HDLS_API Texture(const TextureDesc& desc);
  ~Texture() override = default;

  [[nodiscard]] auto GetDescriptor() const -> const TextureDesc& override;
  [[nodiscard]] auto GetNativeResource() const -> NativeResource override;
  OXGN_HDLS_NDAPI auto GetLayoutStrategy() const
    -> const TextureLayoutStrategy&;

private:
  TextureDesc desc_ {};

  // CPU-side backing storage for texture data. Conservatively sized using a
  // 4 bytes-per-pixel estimate when format is unknown.
  std::vector<std::uint8_t> data_;

  // Headless-specific Contiguous layout strategy (owned by the texture)
  std::unique_ptr<TextureLayoutStrategy> layout_strategy_;
  // Owned view payloads. The Texture must keep payloads alive for any
  // NativeView pointers returned to callers. Use a function-pointer
  // deleter to avoid deleting incomplete types with the default deleter.
  using ViewPayloadPtr = std::unique_ptr<void, void (*)(void*)>;
  mutable std::deque<ViewPayloadPtr> owned_view_payloads_;

public:
  // Headless-only helpers to access CPU-side backing. Bounds-checked copies.
  OXGN_HDLS_API auto ReadBacking(
    void* dst, uint32_t src_offset, uint32_t size) const -> void;
  OXGN_HDLS_API auto WriteBacking(
    const void* src, uint32_t dst_offset, uint32_t size) -> void;

  //! Return the number of bytes currently backed/allocated for this texture.
  /*!
   This reports the size of the internal CPU-side backing storage in bytes.
   Callers can use this to validate whether layout-computed offsets/size
   ranges are addressable by Read/WriteBacking. The value may be zero when
   the texture allocation was capped or omitted.
  */
  OXGN_HDLS_API auto GetBackingSize() const -> uint32_t;

protected:
  OXGN_HDLS_API [[nodiscard]] auto CreateShaderResourceView(
    const DescriptorHandle& view_handle, Format format, TextureType dimension,
    TextureSubResourceSet sub_resources) const -> NativeView override;

  OXGN_HDLS_API [[nodiscard]] auto CreateUnorderedAccessView(
    const DescriptorHandle& view_handle, Format format, TextureType dimension,
    TextureSubResourceSet sub_resources) const -> NativeView override;

  OXGN_HDLS_API [[nodiscard]] auto CreateRenderTargetView(
    const DescriptorHandle& view_handle, Format format,
    TextureSubResourceSet sub_resources) const -> NativeView override;

  OXGN_HDLS_API [[nodiscard]] auto CreateDepthStencilView(
    const DescriptorHandle& view_handle, Format format,
    TextureSubResourceSet sub_resources, bool is_read_only) const
    -> NativeView override;
};

} // namespace oxygen::graphics::headless
