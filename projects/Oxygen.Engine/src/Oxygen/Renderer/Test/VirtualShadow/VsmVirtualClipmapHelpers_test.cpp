//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualClipmapHelpers.h>

#include "VirtualShadowStageCpuHarness.h"

namespace {

using oxygen::renderer::vsm::ComputeClipmapReuse;
using oxygen::renderer::vsm::VsmReuseRejectionReason;
using oxygen::renderer::vsm::testing::DirectionalStageClipmapSpec;
using oxygen::renderer::vsm::testing::VsmStageCpuHarness;

auto MakeClipmapSpec(const std::string_view remap_key,
  std::vector<glm::ivec2> page_grid_origin = { { 4, 5 }, { 8, 10 } },
  std::vector<float> page_world_size = { 32.0F, 64.0F },
  std::vector<float> near_depth = { 1.0F, 2.0F },
  std::vector<float> far_depth = { 101.0F, 202.0F },
  const std::uint32_t pages_per_axis = 8U) -> DirectionalStageClipmapSpec
{
  return DirectionalStageClipmapSpec {
    .remap_key = remap_key,
    .clip_level_count = static_cast<std::uint32_t>(page_grid_origin.size()),
    .pages_per_axis = pages_per_axis,
    .page_grid_origin = std::move(page_grid_origin),
    .page_world_size = std::move(page_world_size),
    .near_depth = std::move(near_depth),
    .far_depth = std::move(far_depth),
  };
}

class VsmClipmapReuseTest : public VsmStageCpuHarness { };

NOLINT_TEST_F(VsmClipmapReuseTest, IdenticalClipmapsReuseWithZeroOffsets)
{
  const auto frame = MakeDirectionalFrame(1ULL, 10U,
    std::array { MakeClipmapSpec("sun") }, "vsm-remap.clipmap.identical");
  const auto& layout = frame.directional_layouts[0];
  const auto result
    = ComputeClipmapReuse(layout, layout, frame.config.clipmap_reuse_config);

  EXPECT_TRUE(result.reusable);
  ASSERT_EQ(result.page_offsets.size(), 2U);
  EXPECT_EQ(result.page_offsets[0], glm::ivec2(0, 0));
  EXPECT_EQ(result.page_offsets[1], glm::ivec2(0, 0));
  EXPECT_EQ(result.rejection_reason, VsmReuseRejectionReason::kNone);
}

NOLINT_TEST_F(
  VsmClipmapReuseTest, PerLevelPagePansWithinToleranceProduceIndependentOffsets)
{
  const auto previous_frame = MakeDirectionalFrame(1ULL, 10U,
    std::array { MakeClipmapSpec("sun") }, "vsm-remap.clipmap.pan.previous");
  const auto current_frame = MakeDirectionalFrame(2ULL, 100U,
    std::array { MakeClipmapSpec("sun", { { 6, 5 }, { 11, 9 } }) },
    "vsm-remap.clipmap.pan.current");

  const auto result = ComputeClipmapReuse(previous_frame.directional_layouts[0],
    current_frame.directional_layouts[0],
    current_frame.config.clipmap_reuse_config);

  EXPECT_TRUE(result.reusable);
  ASSERT_EQ(result.page_offsets.size(), 2U);
  EXPECT_EQ(result.page_offsets[0], glm::ivec2(2, 0));
  EXPECT_EQ(result.page_offsets[1], glm::ivec2(3, -1));
  EXPECT_EQ(result.rejection_reason, VsmReuseRejectionReason::kNone);
}

NOLINT_TEST_F(VsmClipmapReuseTest,
  PanOutsideToleranceRejectsReuseAndReturnsComputedOffsetsUpToFailure)
{
  const auto previous_frame
    = MakeDirectionalFrame(1ULL, 10U, std::array { MakeClipmapSpec("sun") },
      "vsm-remap.clipmap.pan-out.previous");
  const auto current_frame = MakeDirectionalFrame(2ULL, 100U,
    std::array { MakeClipmapSpec("sun", { { 9, 5 }, { 13, 10 } }) },
    "vsm-remap.clipmap.pan-out.current");

  const auto result = ComputeClipmapReuse(previous_frame.directional_layouts[0],
    current_frame.directional_layouts[0],
    current_frame.config.clipmap_reuse_config);

  EXPECT_FALSE(result.reusable);
  ASSERT_EQ(result.page_offsets.size(), 2U);
  EXPECT_EQ(result.page_offsets[0], glm::ivec2(5, 0));
  EXPECT_EQ(result.page_offsets[1], glm::ivec2(0, 0));
  EXPECT_EQ(
    result.rejection_reason, VsmReuseRejectionReason::kPageOffsetOutOfRange);
}

NOLINT_TEST_F(
  VsmClipmapReuseTest, DepthRangeSizeMismatchReportsExplicitRejection)
{
  const auto previous_frame = MakeDirectionalFrame(1ULL, 10U,
    std::array { MakeClipmapSpec("sun") }, "vsm-remap.clipmap.depth.previous");
  const auto current_frame = MakeDirectionalFrame(2ULL, 100U,
    std::array { MakeClipmapSpec("sun", { { 4, 5 }, { 8, 10 } },
      { 32.0F, 64.0F }, { 1.0F, 2.0F }, { 101.0F, 210.0F }) },
    "vsm-remap.clipmap.depth.current");

  const auto result = ComputeClipmapReuse(previous_frame.directional_layouts[0],
    current_frame.directional_layouts[0],
    current_frame.config.clipmap_reuse_config);

  EXPECT_FALSE(result.reusable);
  EXPECT_EQ(
    result.rejection_reason, VsmReuseRejectionReason::kDepthRangeMismatch);
}

NOLINT_TEST_F(
  VsmClipmapReuseTest, PageWorldSizeMismatchReportsExplicitRejection)
{
  const auto previous_frame
    = MakeDirectionalFrame(1ULL, 10U, std::array { MakeClipmapSpec("sun") },
      "vsm-remap.clipmap.world-size.previous");
  const auto current_frame = MakeDirectionalFrame(2ULL, 100U,
    std::array {
      MakeClipmapSpec("sun", { { 4, 5 }, { 8, 10 } }, { 32.0F, 96.0F }) },
    "vsm-remap.clipmap.world-size.current");

  const auto result = ComputeClipmapReuse(previous_frame.directional_layouts[0],
    current_frame.directional_layouts[0],
    current_frame.config.clipmap_reuse_config);

  EXPECT_FALSE(result.reusable);
  EXPECT_EQ(
    result.rejection_reason, VsmReuseRejectionReason::kPageWorldSizeMismatch);
}

NOLINT_TEST_F(VsmClipmapReuseTest, PagesPerAxisMismatchReportsExplicitRejection)
{
  const auto previous_frame
    = MakeDirectionalFrame(1ULL, 10U, std::array { MakeClipmapSpec("sun") },
      "vsm-remap.clipmap.pages-per-axis.previous");
  const auto current_frame = MakeDirectionalFrame(2ULL, 100U,
    std::array { MakeClipmapSpec("sun", { { 4, 5 }, { 8, 10 } },
      { 32.0F, 64.0F }, { 1.0F, 2.0F }, { 101.0F, 202.0F }, 4U) },
    "vsm-remap.clipmap.pages-per-axis.current");

  const auto result = ComputeClipmapReuse(previous_frame.directional_layouts[0],
    current_frame.directional_layouts[0],
    current_frame.config.clipmap_reuse_config);

  EXPECT_FALSE(result.reusable);
  EXPECT_TRUE(result.page_offsets.empty());
  EXPECT_EQ(
    result.rejection_reason, VsmReuseRejectionReason::kPagesPerAxisMismatch);
}

NOLINT_TEST_F(
  VsmClipmapReuseTest, MalformedClipmapLayoutsReportExplicitRejection)
{
  const auto previous_frame
    = MakeDirectionalFrame(1ULL, 10U, std::array { MakeClipmapSpec("sun") },
      "vsm-remap.clipmap.malformed.previous");
  auto current_frame
    = MakeDirectionalFrame(2ULL, 100U, std::array { MakeClipmapSpec("sun") },
      "vsm-remap.clipmap.malformed.current");
  current_frame.directional_layouts[0].page_world_size.pop_back();

  const auto result = ComputeClipmapReuse(previous_frame.directional_layouts[0],
    current_frame.directional_layouts[0],
    current_frame.config.clipmap_reuse_config);

  EXPECT_FALSE(result.reusable);
  EXPECT_EQ(result.rejection_reason, VsmReuseRejectionReason::kUnspecified);
}

NOLINT_TEST_F(
  VsmClipmapReuseTest, ChangedClipLevelCountReportsExplicitRejection)
{
  const auto previous_frame
    = MakeDirectionalFrame(1ULL, 10U, std::array { MakeClipmapSpec("sun") },
      "vsm-remap.clipmap.level-count.previous");
  const auto current_frame = MakeDirectionalFrame(2ULL, 100U,
    std::array { MakeClipmapSpec("sun", { { 4, 5 }, { 8, 10 }, { 16, 20 } },
      { 32.0F, 64.0F, 128.0F }, { 1.0F, 2.0F, 4.0F },
      { 101.0F, 202.0F, 404.0F }) },
    "vsm-remap.clipmap.level-count.current");

  const auto result = ComputeClipmapReuse(previous_frame.directional_layouts[0],
    current_frame.directional_layouts[0],
    current_frame.config.clipmap_reuse_config);

  EXPECT_FALSE(result.reusable);
  EXPECT_EQ(
    result.rejection_reason, VsmReuseRejectionReason::kClipLevelCountMismatch);
}

} // namespace
