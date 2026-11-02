//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <span>
#include <vector>

#include <Oxygen/Cooker/Import/ScratchImage.h>
#include <Oxygen/Cooker/Import/TexturePackingPolicy.h>
#include <Oxygen/Core/Detail/FormatUtils.h>
#include <Oxygen/Core/Types/TextureType.h>

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
      layout.width = MipExtent(meta.width, mip);
      layout.height = MipExtent(meta.height, mip);
      layout.depth = MipExtent(meta.depth, mip);

      // Each row of blocks is padded to the policy's pitch; a subresource
      // holds its rows of every depth slice.
      layout.row_pitch = policy.AlignRowPitchBytes(
        graphics::detail::RowBytes(meta.format, layout.width));
      layout.size_bytes = layout.row_pitch
        * graphics::detail::BlockRows(meta.format, layout.height)
        * layout.depth;

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
