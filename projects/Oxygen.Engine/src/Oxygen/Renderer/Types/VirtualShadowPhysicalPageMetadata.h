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
  std::uint16_t atlas_tile_x { 0U };
  std::uint16_t atlas_tile_y { 0U };
};

struct alignas(16) VirtualShadowPhysicalPageListEntry {
  std::uint64_t resident_key { 0U };
  std::uint32_t physical_page_index { 0xFFFFFFFFU };
  std::uint32_t page_flags { 0U };
};

static_assert(sizeof(VirtualShadowPhysicalPageMetadata) % 16U == 0U);
static_assert(sizeof(VirtualShadowPhysicalPageListEntry) % 16U == 0U);

} // namespace oxygen::renderer
