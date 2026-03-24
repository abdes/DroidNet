//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <optional>

#include <Oxygen/Renderer/api_export.h>

namespace oxygen::renderer::vsm {

struct VsmPhysicalPageCoord {
  std::uint32_t tile_x { 0 };
  std::uint32_t tile_y { 0 };
  std::uint32_t slice { 0 };

  auto operator==(const VsmPhysicalPageCoord&) const -> bool = default;
};

struct VsmPhysicalPageIndex {
  std::uint32_t value { 0 };

  auto operator==(const VsmPhysicalPageIndex&) const -> bool = default;
};

OXGN_RNDR_NDAPI auto ComputeTilesPerAxis(std::uint32_t physical_tile_capacity,
  std::uint32_t slice_count) noexcept -> std::uint32_t;

OXGN_RNDR_NDAPI auto TryConvertToCoord(VsmPhysicalPageIndex index,
  std::uint32_t tile_capacity, std::uint32_t tiles_per_axis,
  std::uint32_t slice_count) noexcept -> std::optional<VsmPhysicalPageCoord>;

OXGN_RNDR_NDAPI auto TryConvertToIndex(const VsmPhysicalPageCoord& coord,
  std::uint32_t tile_capacity, std::uint32_t tiles_per_axis,
  std::uint32_t slice_count) noexcept -> std::optional<VsmPhysicalPageIndex>;

} // namespace oxygen::renderer::vsm
