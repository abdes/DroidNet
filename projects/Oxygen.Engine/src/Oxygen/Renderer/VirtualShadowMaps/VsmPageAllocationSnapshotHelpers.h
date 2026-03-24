//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <vector>

#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManagerSeam.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManagerTypes.h>

namespace oxygen::renderer::vsm::detail {

[[nodiscard]] inline auto BuildLightCacheEntries(
  const VsmVirtualAddressSpaceFrame& virtual_frame)
  -> std::vector<VsmLightCacheEntryState>
{
  auto entries = std::vector<VsmLightCacheEntryState> {};
  entries.reserve(virtual_frame.local_light_layouts.size()
    + virtual_frame.directional_layouts.size());

  for (const auto& local_layout : virtual_frame.local_light_layouts) {
    entries.push_back(VsmLightCacheEntryState {
      .remap_key = local_layout.remap_key,
      .kind = VsmLightCacheKind::kLocal,
      .current_frame_state
      = {
          .virtual_map_id = local_layout.id,
          .first_page_table_entry = local_layout.first_page_table_entry,
          .page_table_entry_count = local_layout.total_page_count,
          .rendered_frame = 0,
          .scheduled_frame = virtual_frame.frame_generation,
          .is_uncached = false,
          .is_distant = local_layout.level_count == 1
            && local_layout.pages_per_level_x == 1
            && local_layout.pages_per_level_y == 1,
          .is_retained_unreferenced = false,
        },
    });
  }

  for (const auto& directional_layout : virtual_frame.directional_layouts) {
    entries.push_back(VsmLightCacheEntryState {
      .remap_key = directional_layout.remap_key,
      .kind = VsmLightCacheKind::kDirectional,
      .current_frame_state
      = {
          .virtual_map_id = directional_layout.first_id,
          .first_page_table_entry = directional_layout.first_page_table_entry,
          .page_table_entry_count = TotalPageCount(directional_layout),
          .rendered_frame = 0,
          .scheduled_frame = virtual_frame.frame_generation,
          .is_uncached = false,
          .is_distant = false,
          .is_retained_unreferenced = false,
        },
    });
  }

  return entries;
}

[[nodiscard]] inline auto BuildBaseSnapshot(const VsmCacheManagerSeam& seam)
  -> VsmPageAllocationSnapshot
{
  auto snapshot = VsmPageAllocationSnapshot {};
  snapshot.frame_generation = seam.current_frame.frame_generation;
  snapshot.pool_identity = seam.physical_pool.pool_identity;
  snapshot.pool_page_size_texels = seam.physical_pool.page_size_texels;
  snapshot.pool_tile_capacity = seam.physical_pool.tile_capacity;
  snapshot.pool_slice_count = seam.physical_pool.slice_count;
  snapshot.pool_depth_format = seam.physical_pool.depth_format;
  snapshot.pool_slice_roles = seam.physical_pool.slice_roles;
  snapshot.is_hzb_data_available = seam.hzb_pool.is_available;
  snapshot.virtual_frame = seam.current_frame;
  snapshot.light_cache_entries = BuildLightCacheEntries(seam.current_frame);
  snapshot.page_table.resize(seam.current_frame.total_page_table_entry_count);
  snapshot.physical_pages.resize(seam.physical_pool.tile_capacity);
  return snapshot;
}

} // namespace oxygen::renderer::vsm::detail
