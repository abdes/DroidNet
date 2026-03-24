//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Renderer/VirtualShadowMaps/VsmPhysicalPageAddressing.h>

namespace {

using oxygen::renderer::vsm::ComputeTilesPerAxis;
using oxygen::renderer::vsm::TryConvertToCoord;
using oxygen::renderer::vsm::TryConvertToIndex;
using oxygen::renderer::vsm::VsmPhysicalPageCoord;
using oxygen::renderer::vsm::VsmPhysicalPageIndex;

NOLINT_TEST(
  VirtualShadowContractsScaffoldTest, PhysicalPoolAddressingHeaderCompiles)
{
  const auto index = VsmPhysicalPageIndex {};
  const auto coord = VsmPhysicalPageCoord {};
  EXPECT_EQ(index.value, 0U);
  EXPECT_EQ(coord.slice, 0U);
}

NOLINT_TEST(VirtualShadowContractsScaffoldTest,
  PhysicalPoolAddressingConvertsBetweenLinearAndTiledCoordinates)
{
  constexpr auto tile_capacity = 512U;
  constexpr auto slice_count = 2U;
  const auto tiles_per_axis = ComputeTilesPerAxis(tile_capacity, slice_count);

  EXPECT_EQ(tiles_per_axis, 16U);

  const auto coord = TryConvertToCoord(VsmPhysicalPageIndex { .value = 273 },
    tile_capacity, tiles_per_axis, slice_count);
  ASSERT_TRUE(coord.has_value());
  EXPECT_EQ(coord->tile_x, 1U);
  EXPECT_EQ(coord->tile_y, 1U);
  EXPECT_EQ(coord->slice, 1U);

  const auto roundtrip
    = TryConvertToIndex(*coord, tile_capacity, tiles_per_axis, slice_count);
  ASSERT_TRUE(roundtrip.has_value());
  EXPECT_EQ(roundtrip->value, 273U);
}

NOLINT_TEST(VirtualShadowContractsScaffoldTest,
  PhysicalPoolAddressingRejectsInvalidLayoutsAndCoordinates)
{
  EXPECT_EQ(ComputeTilesPerAxis(500U, 2U), 0U);
  EXPECT_FALSE(
    TryConvertToCoord(VsmPhysicalPageIndex { .value = 512 }, 512U, 16U, 2U)
      .has_value());
  EXPECT_FALSE(TryConvertToIndex(
    VsmPhysicalPageCoord { .tile_x = 16, .tile_y = 0, .slice = 0 }, 512U, 16U,
    2U)
      .has_value());
  EXPECT_FALSE(TryConvertToIndex(
    VsmPhysicalPageCoord { .tile_x = 0, .tile_y = 0, .slice = 2 }, 512U, 16U,
    2U)
      .has_value());
}

} // namespace
