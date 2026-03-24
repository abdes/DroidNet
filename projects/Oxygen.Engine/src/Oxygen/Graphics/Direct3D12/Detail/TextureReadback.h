//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <vector>

#include <d3d12.h>

#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Direct3D12/Detail/Types.h>
#include <Oxygen/Graphics/Direct3D12/api_export.h>

namespace oxygen::graphics::d3d12::detail {

struct ReadbackSubresourceLayout {
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT placed_footprint {};
  UINT row_count = 0;
  UINT64 row_size_bytes = 0;
};

struct ReadbackSurfaceLayout {
  UINT plane_count = 1;
  UINT subresource_count = 0;
  UINT64 total_bytes = 0;
  std::vector<ReadbackSubresourceLayout> subresources {};
};

struct TextureToBufferCopyInfo {
  TextureBufferCopyRegion resolved_region {};
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT placed_footprint {};
  D3D12_BOX source_box {};
  UINT subresource_index = 0;
  UINT64 bytes_written = 0;
};

OXGN_D3D12_API auto GetD3D12SubresourceCount(
  const TextureDesc& desc, UINT plane_count) -> UINT;

OXGN_D3D12_API auto MakeTextureResourceDesc(const TextureDesc& desc)
  -> D3D12_RESOURCE_DESC;

OXGN_D3D12_API auto ComputeReadbackSurfaceLayout(dx::IDevice& device,
  const D3D12_RESOURCE_DESC& texture_desc, UINT subresource_count,
  UINT plane_count) -> ReadbackSurfaceLayout;

OXGN_D3D12_API auto ComputeTextureToBufferCopyInfo(const TextureDesc& desc,
  DXGI_FORMAT resource_format, uint8_t plane_count,
  const TextureBufferCopyRegion& region) -> TextureToBufferCopyInfo;

} // namespace oxygen::graphics::d3d12::detail
