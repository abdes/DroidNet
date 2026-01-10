//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include <Oxygen/Content/Import/ScratchImage.h>
#include <Oxygen/Content/api_export.h>
#include <Oxygen/Core/Types/Format.h>

namespace oxygen::content::import {

//===----------------------------------------------------------------------===//
// Alignment Constants
//===----------------------------------------------------------------------===//

//! D3D12 row pitch alignment (256 bytes).
inline constexpr uint32_t kD3D12RowPitchAlignment = 256;

//! D3D12 subresource placement alignment (512 bytes).
inline constexpr uint32_t kD3D12SubresourcePlacementAlignment = 512;

//! Minimum subresource offset alignment for tight packing.
inline constexpr uint32_t kTightPackedSubresourceAlignment = 4;

//===----------------------------------------------------------------------===//
// Subresource Layout
//===----------------------------------------------------------------------===//

//! Describes the layout of a single subresource within a packed texture.
struct SubresourceLayout {
  //! Offset in bytes from the start of the texture payload.
  uint64_t offset = 0;

  //! Row pitch in bytes (stride between rows).
  uint32_t row_pitch = 0;

  //! Size in bytes of this subresource.
  uint32_t size_bytes = 0;

  //! Width of this subresource in pixels.
  uint32_t width = 0;

  //! Height of this subresource in pixels.
  uint32_t height = 0;

  //! Depth of this subresource (for 3D textures, otherwise 1).
  uint32_t depth = 1;
};

//===----------------------------------------------------------------------===//
// Packing Policy Interface
//===----------------------------------------------------------------------===//

//! Interface for backend-specific texture packing strategies.
/*!
  Different graphics APIs have different alignment requirements for texture
  data. This interface abstracts those requirements, allowing the cooker to
  produce correctly-aligned data for any target backend.

  ### Implementations

  - **D3D12PackingPolicy**: 256-byte row pitch, 512-byte subresource alignment
  - **TightPackedPolicy**: Minimal alignment for storage efficiency

  ### Usage

  ```cpp
  const auto& policy = D3D12PackingPolicy::Instance();
  auto layouts = ComputeSubresourceLayouts(meta, policy);
  auto total_size = ComputeTotalPayloadSize(layouts);
  ```
*/
class ITexturePackingPolicy {
public:
  virtual ~ITexturePackingPolicy() = default;

  //! Returns the unique identifier for this packing policy.
  [[nodiscard]] virtual auto Id() const noexcept -> std::string_view = 0;

  //! Align row pitch to the required boundary.
  /*!
    @param row_bytes Unaligned row size in bytes
    @return Aligned row pitch in bytes
  */
  [[nodiscard]] virtual auto AlignRowPitchBytes(
    uint32_t row_bytes) const noexcept -> uint32_t
    = 0;

  //! Align subresource offset to the required boundary.
  /*!
    @param offset Unaligned offset in bytes
    @return Aligned offset in bytes
  */
  [[nodiscard]] virtual auto AlignSubresourceOffset(
    uint64_t offset) const noexcept -> uint64_t
    = 0;

protected:
  ITexturePackingPolicy() = default;
  ITexturePackingPolicy(const ITexturePackingPolicy&) = default;
  ITexturePackingPolicy(ITexturePackingPolicy&&) = default;
  auto operator=(const ITexturePackingPolicy&)
    -> ITexturePackingPolicy& = default;
  auto operator=(ITexturePackingPolicy&&) -> ITexturePackingPolicy& = default;
};

//===----------------------------------------------------------------------===//
// D3D12 Packing Policy
//===----------------------------------------------------------------------===//

//! Packing policy for D3D12-compatible texture layouts.
/*!
  Implements the alignment requirements for D3D12 texture uploads:
  - Row pitch aligned to 256 bytes (`D3D12_TEXTURE_DATA_PITCH_ALIGNMENT`)
  - Subresource offset aligned to 512 bytes
    (`D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT`)
*/
class D3D12PackingPolicy final : public ITexturePackingPolicy {
public:
  //! Returns the singleton instance.
  [[nodiscard]] OXGN_CNTT_API static auto Instance() noexcept
    -> const D3D12PackingPolicy&;

  [[nodiscard]] auto Id() const noexcept -> std::string_view override
  {
    return "d3d12";
  }

  [[nodiscard]] auto AlignRowPitchBytes(uint32_t row_bytes) const noexcept
    -> uint32_t override
  {
    return (row_bytes + kD3D12RowPitchAlignment - 1)
      & ~(kD3D12RowPitchAlignment - 1);
  }

  [[nodiscard]] auto AlignSubresourceOffset(uint64_t offset) const noexcept
    -> uint64_t override
  {
    return (offset + kD3D12SubresourcePlacementAlignment - 1)
      & ~static_cast<uint64_t>(kD3D12SubresourcePlacementAlignment - 1);
  }
};

//===----------------------------------------------------------------------===//
// Tight Packed Policy
//===----------------------------------------------------------------------===//

//! Packing policy for minimal-overhead texture storage.
/*!
  Implements minimal alignment for maximum storage efficiency:
  - No row pitch padding
  - 4-byte subresource offset alignment (for pointer safety)

  Use this policy for intermediate storage or when GPU alignment is not
  required.
*/
class TightPackedPolicy final : public ITexturePackingPolicy {
public:
  //! Returns the singleton instance.
  [[nodiscard]] OXGN_CNTT_API static auto Instance() noexcept
    -> const TightPackedPolicy&;

  [[nodiscard]] auto Id() const noexcept -> std::string_view override
  {
    return "tight";
  }

  [[nodiscard]] auto AlignRowPitchBytes(uint32_t row_bytes) const noexcept
    -> uint32_t override
  {
    return row_bytes; // No padding
  }

  [[nodiscard]] auto AlignSubresourceOffset(uint64_t offset) const noexcept
    -> uint64_t override
  {
    return (offset + kTightPackedSubresourceAlignment - 1)
      & ~static_cast<uint64_t>(kTightPackedSubresourceAlignment - 1);
  }
};

//===----------------------------------------------------------------------===//
// Format Utilities
//===----------------------------------------------------------------------===//

//! Compute bytes per pixel for uncompressed formats, or bytes per block for
//! compressed formats.
/*!
  @param format Pixel format
  @return Bytes per pixel/block, or 0 for unknown formats
*/
[[nodiscard]] OXGN_CNTT_API auto ComputeBytesPerPixelOrBlock(
  Format format) noexcept -> uint32_t;

//! Compute the block dimension for a format.
/*!
  @param format Pixel format
  @return Block dimension (1 for uncompressed, 4 for BC formats)
*/
[[nodiscard]] OXGN_CNTT_API auto ComputeBlockDimension(Format format) noexcept
  -> uint32_t;

//! Compute the unaligned row bytes for a surface.
/*!
  Accounts for both uncompressed and block-compressed formats.

  @param width  Surface width in pixels
  @param format Pixel format
  @return Unaligned row size in bytes
*/
[[nodiscard]] OXGN_CNTT_API auto ComputeRowBytes(
  uint32_t width, Format format) noexcept -> uint32_t;

//! Compute the unaligned surface size in bytes.
/*!
  @param width  Surface width in pixels
  @param height Surface height in pixels
  @param format Pixel format
  @return Unaligned surface size in bytes
*/
[[nodiscard]] OXGN_CNTT_API auto ComputeSurfaceBytes(
  uint32_t width, uint32_t height, Format format) noexcept -> uint64_t;

//===----------------------------------------------------------------------===//
// Subresource Layout Computation
//===----------------------------------------------------------------------===//

//! Compute mip dimension at a given level.
/*!
  @param base_dimension Base dimension (width or height at mip 0)
  @param mip_level      Mip level (0 = base)
  @return Dimension at the given mip level, minimum 1
*/
[[nodiscard]] constexpr auto ComputeMipDimension(
  uint32_t base_dimension, uint32_t mip_level) noexcept -> uint32_t
{
  const uint32_t result = base_dimension >> mip_level;
  return result > 0 ? result : 1;
}

//! Compute layouts for all subresources in a texture.
/*!
  Computes the offset, row pitch, and size for each subresource based on
  the packing policy's alignment requirements.

  Subresources are ordered: layer 0 mips 0..N-1, layer 1 mips 0..N-1, ...

  @param meta   Texture metadata
  @param policy Packing policy for alignment
  @return Vector of SubresourceLayout, one per subresource
*/
[[nodiscard]] OXGN_CNTT_API auto ComputeSubresourceLayouts(
  const ScratchImageMeta& meta, const ITexturePackingPolicy& policy)
  -> std::vector<SubresourceLayout>;

//! Compute total payload size for a texture.
/*!
  @param layouts Subresource layouts computed by ComputeSubresourceLayouts
  @return Total size in bytes for all subresources
*/
[[nodiscard]] OXGN_CNTT_API auto ComputeTotalPayloadSize(
  std::span<const SubresourceLayout> layouts) noexcept -> uint64_t;

} // namespace oxygen::content::import
