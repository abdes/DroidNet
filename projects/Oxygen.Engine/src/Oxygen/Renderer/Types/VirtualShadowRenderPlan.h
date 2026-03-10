//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <span>

#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Renderer/Types/DirectionalVirtualShadowMetadata.h>
#include <Oxygen/Renderer/Types/ViewConstants.h>

namespace oxygen::renderer {

enum class VirtualPageResidencyState : std::uint8_t {
  kUnmapped = 0U,
  kResidentClean = 1U,
  kResidentDirty = 2U,
  kPendingRender = 3U,
};

struct VirtualShadowRasterJob {
  std::uint32_t shadow_instance_index { 0xFFFFFFFFU };
  std::uint32_t payload_index { 0xFFFFFFFFU };
  std::uint32_t clip_level { 0U };
  std::uint32_t page_index { 0U };
  std::uint64_t resident_key { 0U };
  std::uint16_t atlas_tile_x { 0U };
  std::uint16_t atlas_tile_y { 0U };
  engine::ViewConstants::GpuData view_constants {};
};

struct VirtualShadowRenderPlan {
  const graphics::Texture* depth_texture { nullptr };
  std::span<const VirtualShadowRasterJob> jobs {};
  std::uint32_t page_size_texels { 0U };
  std::uint32_t atlas_tiles_per_axis { 0U };
};

struct VirtualShadowViewIntrospection {
  std::span<const engine::DirectionalVirtualShadowMetadata>
    directional_virtual_metadata {};
  std::span<const VirtualShadowRasterJob> virtual_raster_jobs {};
  std::span<const std::uint32_t> page_table_entries {};
  std::uint32_t mapped_page_count { 0U };
  std::uint32_t resident_page_count { 0U };
  std::uint32_t clean_page_count { 0U };
  std::uint32_t dirty_page_count { 0U };
  std::uint32_t pending_page_count { 0U };
};

} // namespace oxygen::renderer
