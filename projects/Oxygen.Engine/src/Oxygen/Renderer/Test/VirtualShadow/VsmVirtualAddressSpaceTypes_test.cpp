//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <glm/vec2.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualAddressSpace.h>

#include "VirtualShadowTestFixtures.h"

namespace {

using oxygen::renderer::vsm::to_string;
using oxygen::renderer::vsm::VsmClipmapLayout;
using oxygen::renderer::vsm::VsmReuseRejectionReason;
using oxygen::renderer::vsm::VsmVirtualMapLayout;
using oxygen::renderer::vsm::testing::VirtualShadowTest;

class VsmVirtualAddressSpaceTypesTest : public VirtualShadowTest { };

NOLINT_TEST_F(
  VsmVirtualAddressSpaceTypesTest, LayoutValueTypesPreserveConfiguredValues)
{
  const auto expected_local_layout = VsmVirtualMapLayout {
    .id = 17U,
    .remap_key = "hero-spot",
    .level_count = 3U,
    .pages_per_level_x = 4U,
    .pages_per_level_y = 2U,
    .total_page_count = 24U,
    .first_page_table_entry = 5U,
  };
  const auto copied_local_layout = expected_local_layout;
  EXPECT_EQ(copied_local_layout, expected_local_layout);

  const auto expected_clipmap_layout = VsmClipmapLayout {
    .first_id = 40U,
    .remap_key = "sun-main",
    .clip_level_count = 2U,
    .pages_per_axis = 8U,
    .first_page_table_entry = 11U,
    .page_grid_origin = { { 4, 5 }, { 6, 7 } },
    .page_world_size = { 32.0F, 64.0F },
    .near_depth = { 1.0F, 2.0F },
    .far_depth = { 100.0F, 200.0F },
  };
  const auto copied_clipmap_layout = expected_clipmap_layout;
  EXPECT_EQ(copied_clipmap_layout, expected_clipmap_layout);
}

NOLINT_TEST_F(VsmVirtualAddressSpaceTypesTest,
  ReuseRejectionReasonsExposeStableStringSurface)
{
  EXPECT_STREQ(to_string(VsmReuseRejectionReason::kNone), "None");
  EXPECT_STREQ(to_string(VsmReuseRejectionReason::kPageOffsetOutOfRange),
    "PageOffsetOutOfRange");
  EXPECT_STREQ(to_string(VsmReuseRejectionReason::kDuplicateRemapKey),
    "DuplicateRemapKey");
}

} // namespace
