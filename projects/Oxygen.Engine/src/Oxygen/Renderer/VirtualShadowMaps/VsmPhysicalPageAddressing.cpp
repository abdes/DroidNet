//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/VirtualShadowMaps/VsmPhysicalPageAddressing.h>

#include <cmath>

namespace oxygen::renderer::vsm {

auto ComputeTilesPerAxis(const std::uint32_t physical_tile_capacity,
  const std::uint32_t slice_count) noexcept -> std::uint32_t
{
  if (physical_tile_capacity == 0 || slice_count == 0) {
    return 0;
  }

  if ((physical_tile_capacity % slice_count) != 0) {
    return 0;
  }

  const auto per_slice_capacity = physical_tile_capacity / slice_count;
  const auto root = static_cast<std::uint32_t>(
    std::sqrt(static_cast<double>(per_slice_capacity)));
  return (root * root) == per_slice_capacity ? root : 0;
}

auto TryConvertToCoord(const VsmPhysicalPageIndex index,
  const std::uint32_t tile_capacity, const std::uint32_t tiles_per_axis,
  const std::uint32_t slice_count) noexcept
  -> std::optional<VsmPhysicalPageCoord>
{
  if (tile_capacity == 0 || tiles_per_axis == 0 || slice_count == 0
    || index.value >= tile_capacity) {
    return std::nullopt;
  }

  const auto tiles_per_slice = tiles_per_axis * tiles_per_axis;
  const auto max_capacity = tiles_per_slice * slice_count;
  if (max_capacity != tile_capacity) {
    return std::nullopt;
  }

  const auto slice = index.value / tiles_per_slice;
  const auto in_slice = index.value % tiles_per_slice;
  return VsmPhysicalPageCoord {
    .tile_x = in_slice % tiles_per_axis,
    .tile_y = in_slice / tiles_per_axis,
    .slice = slice,
  };
}

auto TryConvertToIndex(const VsmPhysicalPageCoord& coord,
  const std::uint32_t tile_capacity, const std::uint32_t tiles_per_axis,
  const std::uint32_t slice_count) noexcept
  -> std::optional<VsmPhysicalPageIndex>
{
  if (tile_capacity == 0 || tiles_per_axis == 0 || slice_count == 0
    || coord.tile_x >= tiles_per_axis || coord.tile_y >= tiles_per_axis
    || coord.slice >= slice_count) {
    return std::nullopt;
  }

  const auto tiles_per_slice = tiles_per_axis * tiles_per_axis;
  const auto max_capacity = tiles_per_slice * slice_count;
  if (max_capacity != tile_capacity) {
    return std::nullopt;
  }

  return VsmPhysicalPageIndex {
    .value = coord.slice * tiles_per_slice + coord.tile_y * tiles_per_axis
      + coord.tile_x,
  };
}

} // namespace oxygen::renderer::vsm
