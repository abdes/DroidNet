//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include "VirtualShadowTestFixtures.h"

#include <glm/vec2.hpp>

#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualClipmapHelpers.h>

namespace {

using oxygen::renderer::vsm::ComputeClipmapReuse;
using oxygen::renderer::vsm::VsmClipmapLayout;
using oxygen::renderer::vsm::VsmClipmapReuseConfig;
using oxygen::renderer::vsm::VsmReuseRejectionReason;
using oxygen::renderer::vsm::testing::VirtualShadowTest;

auto MakeClipmapLayout() -> VsmClipmapLayout
{
  return VsmClipmapLayout {
    .first_id = 10,
    .remap_key = "sun",
    .clip_level_count = 2,
    .pages_per_axis = 8,
    .first_page_table_entry = 0,
    .page_grid_origin = { { 4, 5 }, { 8, 10 } },
    .page_world_size = { 32.0F, 64.0F },
    .near_depth = { 1.0F, 2.0F },
    .far_depth = { 101.0F, 202.0F },
  };
}

auto MakeReuseConfig() -> VsmClipmapReuseConfig
{
  return VsmClipmapReuseConfig {
    .max_page_offset_x = 4,
    .max_page_offset_y = 4,
    .depth_range_epsilon = 0.01F,
    .page_world_size_epsilon = 0.01F,
  };
}

class VsmVirtualClipmapHelpersTest : public VirtualShadowTest { };

NOLINT_TEST_F(
  VsmVirtualClipmapHelpersTest, StableClipmapLayoutReportsReusableZeroOffset)
{
  const auto layout = MakeClipmapLayout();
  const auto result = ComputeClipmapReuse(layout, layout, MakeReuseConfig());

  EXPECT_TRUE(result.reusable);
  ASSERT_EQ(result.page_offsets.size(), 2U);
  EXPECT_EQ(result.page_offsets[0], glm::ivec2(0, 0));
  EXPECT_EQ(result.page_offsets[1], glm::ivec2(0, 0));
  EXPECT_EQ(result.rejection_reason, VsmReuseRejectionReason::kNone);
}

NOLINT_TEST_F(VsmVirtualClipmapHelpersTest,
  PageAlignedPanInsideToleranceReportsReusableOffset)
{
  const auto previous = MakeClipmapLayout();
  auto current = MakeClipmapLayout();
  current.page_grid_origin = { { 6, 4 }, { 10, 9 } };

  const auto result = ComputeClipmapReuse(previous, current, MakeReuseConfig());

  EXPECT_TRUE(result.reusable);
  ASSERT_EQ(result.page_offsets.size(), 2U);
  EXPECT_EQ(result.page_offsets[0], glm::ivec2(2, -1));
  EXPECT_EQ(result.page_offsets[1], glm::ivec2(2, -1));
  EXPECT_EQ(result.rejection_reason, VsmReuseRejectionReason::kNone);
}

NOLINT_TEST_F(
  VsmVirtualClipmapHelpersTest, PanOutsideToleranceReportsExplicitRejection)
{
  const auto previous = MakeClipmapLayout();
  auto current = MakeClipmapLayout();
  current.page_grid_origin = { { 9, 5 }, { 13, 10 } };

  const auto result = ComputeClipmapReuse(previous, current, MakeReuseConfig());

  EXPECT_FALSE(result.reusable);
  ASSERT_EQ(result.page_offsets.size(), 2U);
  EXPECT_EQ(result.page_offsets[0], glm::ivec2(5, 0));
  EXPECT_EQ(
    result.rejection_reason, VsmReuseRejectionReason::kPageOffsetOutOfRange);
}

NOLINT_TEST_F(
  VsmVirtualClipmapHelpersTest, DepthRangeSizeMismatchReportsExplicitRejection)
{
  const auto previous = MakeClipmapLayout();
  auto current = MakeClipmapLayout();
  current.far_depth[1] = 210.0F;

  const auto result = ComputeClipmapReuse(previous, current, MakeReuseConfig());

  EXPECT_FALSE(result.reusable);
  EXPECT_EQ(
    result.rejection_reason, VsmReuseRejectionReason::kDepthRangeMismatch);
}

NOLINT_TEST_F(
  VsmVirtualClipmapHelpersTest, PageWorldSizeMismatchReportsExplicitRejection)
{
  const auto previous = MakeClipmapLayout();
  auto current = MakeClipmapLayout();
  current.page_world_size[1] = 96.0F;

  const auto result = ComputeClipmapReuse(previous, current, MakeReuseConfig());

  EXPECT_FALSE(result.reusable);
  EXPECT_EQ(
    result.rejection_reason, VsmReuseRejectionReason::kPageWorldSizeMismatch);
}

NOLINT_TEST_F(
  VsmVirtualClipmapHelpersTest, NonUniformPerLevelOffsetsReportReusable)
{
  const auto previous = MakeClipmapLayout();
  auto current = MakeClipmapLayout();
  // Level 0 pans by (1, 0), level 1 pans by (3, -1) -- different offsets OK.
  current.page_grid_origin = { { 5, 5 }, { 11, 9 } };

  const auto result = ComputeClipmapReuse(previous, current, MakeReuseConfig());

  EXPECT_TRUE(result.reusable);
  ASSERT_EQ(result.page_offsets.size(), 2U);
  EXPECT_EQ(result.page_offsets[0], glm::ivec2(1, 0));
  EXPECT_EQ(result.page_offsets[1], glm::ivec2(3, -1));
  EXPECT_EQ(result.rejection_reason, VsmReuseRejectionReason::kNone);
}

NOLINT_TEST_F(
  VsmVirtualClipmapHelpersTest, MalformedClipmapLayoutsReportExplicitRejection)
{
  const auto previous = MakeClipmapLayout();
  auto current = MakeClipmapLayout();
  current.page_world_size.pop_back();

  const auto result = ComputeClipmapReuse(previous, current, MakeReuseConfig());

  EXPECT_FALSE(result.reusable);
  EXPECT_EQ(result.rejection_reason, VsmReuseRejectionReason::kUnspecified);
}

NOLINT_TEST_F(
  VsmVirtualClipmapHelpersTest, ChangedClipLevelCountReportsExplicitRejection)
{
  const auto previous = MakeClipmapLayout();
  auto current = MakeClipmapLayout();
  current.clip_level_count = 3;
  current.page_grid_origin.push_back({ 16, 20 });
  current.page_world_size.push_back(128.0F);
  current.near_depth.push_back(4.0F);
  current.far_depth.push_back(404.0F);

  const auto result = ComputeClipmapReuse(previous, current, MakeReuseConfig());

  EXPECT_FALSE(result.reusable);
  EXPECT_EQ(
    result.rejection_reason, VsmReuseRejectionReason::kClipLevelCountMismatch);
}

} // namespace
