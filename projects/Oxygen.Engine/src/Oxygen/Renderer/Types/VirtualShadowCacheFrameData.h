//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <Oxygen/Renderer/Types/DirectionalVirtualShadowMetadata.h>

namespace oxygen::renderer {

struct VirtualShadowClipmapCacheKey {
  std::uint64_t directional_address_space_hash { 0U };
  std::uint32_t clip_level_count { 0U };
  std::uint32_t pages_per_axis { 0U };
  std::uint32_t page_size_texels { 0U };
};

struct VirtualShadowCacheFrameData {
  bool has_authoritative_page_management_state { false };
  bool is_uncached { true };
  bool page_management_finalized { false };
  std::int64_t rendered_frame_number { -1 };
  std::int64_t scheduled_frame_number { -1 };
  VirtualShadowClipmapCacheKey clipmap_cache_key {};
  std::uint64_t shadow_caster_content_hash { 0U };
  std::array<std::int32_t, engine::kMaxVirtualDirectionalClipLevels>
    cached_clip_grid_origin_x {};
  std::array<std::int32_t, engine::kMaxVirtualDirectionalClipLevels>
    cached_clip_grid_origin_y {};
  bool has_cached_clip_grid_origins { false };
  std::vector<engine::DirectionalVirtualShadowMetadata> directional_metadata {};
};

} // namespace oxygen::renderer
