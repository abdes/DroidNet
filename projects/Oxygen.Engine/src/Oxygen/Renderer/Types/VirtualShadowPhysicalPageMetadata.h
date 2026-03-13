//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

namespace oxygen::renderer {

struct alignas(16) VirtualShadowPhysicalPageMetadata {
  std::uint64_t resident_key { 0U };
  std::uint32_t page_flags { 0U };
  std::uint32_t packed_atlas_tile_coords { 0U };

  [[nodiscard]] static constexpr auto PackAtlasTileCoords(
    const std::uint16_t tile_x, const std::uint16_t tile_y) -> std::uint32_t
  {
    return static_cast<std::uint32_t>(tile_x)
      | (static_cast<std::uint32_t>(tile_y) << 16U);
  }

  [[nodiscard]] constexpr auto AtlasTileX() const -> std::uint16_t
  {
    return static_cast<std::uint16_t>(packed_atlas_tile_coords & 0xFFFFU);
  }

  [[nodiscard]] constexpr auto AtlasTileY() const -> std::uint16_t
  {
    return static_cast<std::uint16_t>(
      (packed_atlas_tile_coords >> 16U) & 0xFFFFU);
  }
};

struct alignas(16) VirtualShadowPhysicalPageListEntry {
  std::uint64_t resident_key { 0U };
  std::uint32_t physical_page_index { 0xFFFFFFFFU };
  std::uint32_t page_flags { 0U };
};

static_assert(sizeof(VirtualShadowPhysicalPageMetadata) % 16U == 0U);
static_assert(sizeof(VirtualShadowPhysicalPageListEntry) % 16U == 0U);

} // namespace oxygen::renderer
