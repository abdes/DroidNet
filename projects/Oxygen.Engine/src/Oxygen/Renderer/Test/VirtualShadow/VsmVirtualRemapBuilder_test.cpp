//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualRemapBuilder.h>

#include "VirtualShadowStageCpuHarness.h"

namespace {

using oxygen::renderer::vsm::BuildVirtualRemapTable;
using oxygen::renderer::vsm::VsmReuseRejectionReason;
using oxygen::renderer::vsm::VsmVirtualRemapEntry;
using oxygen::renderer::vsm::testing::DirectionalStageClipmapSpec;
using oxygen::renderer::vsm::testing::LocalStageLightSpec;
using oxygen::renderer::vsm::testing::VsmStageCpuHarness;

auto FindEntry(const std::vector<VsmVirtualRemapEntry>& entries,
  const std::uint32_t previous_id) -> const VsmVirtualRemapEntry*
{
  const auto it = std::find_if(entries.begin(), entries.end(),
    [previous_id](const VsmVirtualRemapEntry& entry) {
      return entry.previous_id == previous_id;
    });
  return it == entries.end() ? nullptr : &*it;
}

auto MakeLocalSpec(const std::string_view remap_key,
  const std::uint32_t level_count = 1U,
  const std::uint32_t pages_per_level_x = 1U,
  const std::uint32_t pages_per_level_y = 1U) -> LocalStageLightSpec
{
  return LocalStageLightSpec {
    .remap_key = remap_key,
    .level_count = level_count,
    .pages_per_level_x = pages_per_level_x,
    .pages_per_level_y = pages_per_level_y,
  };
}

auto MakeClipmapSpec(const std::string_view remap_key,
  std::vector<glm::ivec2> page_grid_origin = { { 0, 0 }, { 4, 4 } },
  std::vector<float> page_world_size = { 32.0F, 64.0F },
  std::vector<float> near_depth = { 1.0F, 2.0F },
  std::vector<float> far_depth = { 101.0F, 202.0F },
  const std::uint32_t pages_per_axis = 4U) -> DirectionalStageClipmapSpec
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

class VsmLocalRemapContractTest : public VsmStageCpuHarness { };
class VsmDirectionalRemapContractTest : public VsmStageCpuHarness { };

NOLINT_TEST_F(VsmLocalRemapContractTest,
  RejectsMissingCurrentLocalLayoutsWithExplicitReason)
{
  const auto previous_frame = MakeLocalFrame(1ULL, 10U,
    std::array { MakeLocalSpec("hero", 2U, 2U, 2U) },
    "vsm-remap.local.missing.previous");
  const auto current_frame = MakeLocalFrame(2ULL, 100U,
    std::array { MakeLocalSpec("other", 2U, 2U, 2U) },
    "vsm-remap.local.missing.current");

  const auto table = BuildVirtualRemapTable(previous_frame, current_frame);

  ASSERT_EQ(table.entries.size(), 1U);
  EXPECT_EQ(
    table.entries[0].previous_id, previous_frame.local_light_layouts[0].id);
  EXPECT_EQ(table.entries[0].current_id, 0U);
  EXPECT_EQ(table.entries[0].rejection_reason,
    VsmReuseRejectionReason::kNoMatchingCurrentLayout);
}

NOLINT_TEST_F(
  VsmLocalRemapContractTest, RejectsStableKeysWhoseLocalLayoutsChanged)
{
  const auto previous_frame = MakeLocalFrame(1ULL, 10U,
    std::array { MakeLocalSpec("hero", 2U, 2U, 2U) },
    "vsm-remap.local.mismatch.previous");
  const auto current_frame = MakeLocalFrame(2ULL, 100U,
    std::array { MakeLocalSpec("hero", 3U, 2U, 2U) },
    "vsm-remap.local.mismatch.current");

  const auto table = BuildVirtualRemapTable(previous_frame, current_frame);

  ASSERT_EQ(table.entries.size(), 1U);
  EXPECT_EQ(
    table.entries[0].previous_id, previous_frame.local_light_layouts[0].id);
  EXPECT_EQ(
    table.entries[0].current_id, current_frame.local_light_layouts[0].id);
  EXPECT_EQ(table.entries[0].rejection_reason,
    VsmReuseRejectionReason::kLocalLightLayoutMismatch);
}

NOLINT_TEST_F(
  VsmLocalRemapContractTest, RejectsMissingLocalRemapKeysFromRealFrameSnapshots)
{
  const auto previous_frame
    = MakeLocalFrame(1ULL, 10U, std::array { MakeLocalSpec("", 2U, 2U, 2U) },
      "vsm-remap.local.missing-key.previous");
  const auto current_frame = MakeLocalFrame(2ULL, 100U,
    std::array { MakeLocalSpec("hero", 2U, 2U, 2U) },
    "vsm-remap.local.missing-key.current");

  const auto table = BuildVirtualRemapTable(previous_frame, current_frame);

  ASSERT_EQ(table.entries.size(), 1U);
  EXPECT_EQ(
    table.entries[0].previous_id, previous_frame.local_light_layouts[0].id);
  EXPECT_EQ(table.entries[0].current_id, 0U);
  EXPECT_EQ(table.entries[0].rejection_reason,
    VsmReuseRejectionReason::kMissingRemapKey);
}

NOLINT_TEST_F(VsmLocalRemapContractTest, RejectsDuplicateCurrentLocalRemapKeys)
{
  const auto previous_frame = MakeLocalFrame(1ULL, 10U,
    std::array { MakeLocalSpec("hero", 2U, 2U, 2U) },
    "vsm-remap.local.duplicate-current.previous");
  const auto current_frame = MakeLocalFrame(2ULL, 100U,
    std::array {
      MakeLocalSpec("hero", 2U, 2U, 2U),
      MakeLocalSpec("hero", 2U, 2U, 2U),
    },
    "vsm-remap.local.duplicate-current.current");

  const auto table = BuildVirtualRemapTable(previous_frame, current_frame);

  ASSERT_EQ(table.entries.size(), 1U);
  EXPECT_EQ(
    table.entries[0].previous_id, previous_frame.local_light_layouts[0].id);
  EXPECT_EQ(table.entries[0].current_id, 0U);
  EXPECT_EQ(table.entries[0].rejection_reason,
    VsmReuseRejectionReason::kDuplicateRemapKey);
}

NOLINT_TEST_F(VsmLocalRemapContractTest,
  RejectsDuplicatePreviousLocalRemapKeysForEachPreviousLayout)
{
  const auto previous_frame = MakeLocalFrame(1ULL, 10U,
    std::array {
      MakeLocalSpec("hero", 2U, 2U, 2U),
      MakeLocalSpec("hero"),
    },
    "vsm-remap.local.duplicate-previous.previous");
  const auto current_frame = MakeLocalFrame(2ULL, 100U,
    std::array { MakeLocalSpec("hero", 2U, 2U, 2U) },
    "vsm-remap.local.duplicate-previous.current");

  const auto table = BuildVirtualRemapTable(previous_frame, current_frame);

  ASSERT_EQ(table.entries.size(), 2U);
  EXPECT_EQ(table.entries[0].rejection_reason,
    VsmReuseRejectionReason::kDuplicateRemapKey);
  EXPECT_EQ(table.entries[1].rejection_reason,
    VsmReuseRejectionReason::kDuplicateRemapKey);
  EXPECT_EQ(table.entries[0].current_id, 0U);
  EXPECT_EQ(table.entries[1].current_id, 0U);
}

NOLINT_TEST_F(
  VsmLocalRemapContractTest, RejectsMalformedCurrentLocalLightLayouts)
{
  const auto previous_frame = MakeLocalFrame(1ULL, 10U,
    std::array { MakeLocalSpec("hero", 2U, 2U, 2U) },
    "vsm-remap.local.malformed.previous");
  auto malformed_current = MakeLocalFrame(2ULL, 100U,
    std::array { MakeLocalSpec("hero", 2U, 2U, 2U) },
    "vsm-remap.local.malformed.current");
  malformed_current.local_light_layouts[0].total_page_count -= 1U;

  const auto table = BuildVirtualRemapTable(previous_frame, malformed_current);

  ASSERT_EQ(table.entries.size(), 1U);
  EXPECT_EQ(
    table.entries[0].previous_id, previous_frame.local_light_layouts[0].id);
  EXPECT_EQ(
    table.entries[0].current_id, malformed_current.local_light_layouts[0].id);
  EXPECT_EQ(
    table.entries[0].rejection_reason, VsmReuseRejectionReason::kUnspecified);
}

NOLINT_TEST_F(
  VsmLocalRemapContractTest, RejectsMalformedPreviousLocalLightLayouts)
{
  auto malformed_previous = MakeLocalFrame(1ULL, 10U,
    std::array { MakeLocalSpec("hero", 2U, 2U, 2U) },
    "vsm-remap.local.malformed-previous.previous");
  malformed_previous.local_light_layouts[0].total_page_count -= 1U;
  const auto current_frame = MakeLocalFrame(2ULL, 100U,
    std::array { MakeLocalSpec("hero", 2U, 2U, 2U) },
    "vsm-remap.local.malformed-previous.current");

  const auto table = BuildVirtualRemapTable(malformed_previous, current_frame);

  ASSERT_EQ(table.entries.size(), 1U);
  EXPECT_EQ(
    table.entries[0].previous_id, malformed_previous.local_light_layouts[0].id);
  EXPECT_EQ(table.entries[0].current_id, 0U);
  EXPECT_EQ(
    table.entries[0].rejection_reason, VsmReuseRejectionReason::kUnspecified);
}

NOLINT_TEST_F(VsmDirectionalRemapContractTest,
  RejectsMissingCurrentClipmapsPerPreviousLevel)
{
  const auto previous_frame
    = MakeDirectionalFrame(1ULL, 10U, std::array { MakeClipmapSpec("sun") },
      "vsm-remap.directional.missing.previous");
  const auto current_frame
    = MakeDirectionalFrame(2ULL, 100U, std::array { MakeClipmapSpec("moon") },
      "vsm-remap.directional.missing.current");

  const auto table = BuildVirtualRemapTable(previous_frame, current_frame);

  ASSERT_EQ(table.entries.size(), 2U);
  for (std::uint32_t clip_index = 0; clip_index < 2U; ++clip_index) {
    const auto* entry = FindEntry(table.entries,
      previous_frame.directional_layouts[0].first_id + clip_index);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->current_id, 0U);
    EXPECT_EQ(entry->rejection_reason,
      VsmReuseRejectionReason::kNoMatchingCurrentLayout);
  }
}

NOLINT_TEST_F(VsmDirectionalRemapContractTest,
  RejectsChangedClipLevelCountForEveryPreviousLevel)
{
  const auto previous_frame = MakeDirectionalFrame(1ULL, 10U,
    std::array { MakeClipmapSpec("sun", { { 0, 0 }, { 4, 4 }, { 8, 8 } },
      { 16.0F, 32.0F, 64.0F }, { 1.0F, 2.0F, 4.0F },
      { 51.0F, 102.0F, 204.0F }) },
    "vsm-remap.directional.level-count.previous");
  const auto current_frame
    = MakeDirectionalFrame(2ULL, 100U, std::array { MakeClipmapSpec("sun") },
      "vsm-remap.directional.level-count.current");

  const auto table = BuildVirtualRemapTable(previous_frame, current_frame);

  ASSERT_EQ(table.entries.size(), 3U);
  for (std::uint32_t clip_index = 0; clip_index < 2U; ++clip_index) {
    const auto* entry = FindEntry(table.entries,
      previous_frame.directional_layouts[0].first_id + clip_index);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->current_id,
      current_frame.directional_layouts[0].first_id + clip_index);
    EXPECT_EQ(entry->page_offset, glm::ivec2(0, 0));
    EXPECT_EQ(entry->rejection_reason,
      VsmReuseRejectionReason::kClipLevelCountMismatch);
  }

  const auto* dropped_level = FindEntry(
    table.entries, previous_frame.directional_layouts[0].first_id + 2U);
  ASSERT_NE(dropped_level, nullptr);
  EXPECT_EQ(dropped_level->current_id, 0U);
  EXPECT_EQ(dropped_level->rejection_reason,
    VsmReuseRejectionReason::kClipLevelCountMismatch);
}

NOLINT_TEST_F(VsmDirectionalRemapContractTest,
  RejectsDirectionalPageOffsetsOutsideToleranceAndKeepsComputedOffsets)
{
  const auto previous_frame
    = MakeDirectionalFrame(1ULL, 10U, std::array { MakeClipmapSpec("sun") },
      "vsm-remap.directional.pan.previous");
  const auto current_frame = MakeDirectionalFrame(2ULL, 100U,
    std::array { MakeClipmapSpec("sun", { { 5, 0 }, { 7, 1 } }) },
    "vsm-remap.directional.pan.current");

  const auto table = BuildVirtualRemapTable(previous_frame, current_frame);

  ASSERT_EQ(table.entries.size(), 2U);
  const auto* level0
    = FindEntry(table.entries, previous_frame.directional_layouts[0].first_id);
  const auto* level1 = FindEntry(
    table.entries, previous_frame.directional_layouts[0].first_id + 1U);
  ASSERT_NE(level0, nullptr);
  ASSERT_NE(level1, nullptr);

  EXPECT_EQ(level0->current_id, current_frame.directional_layouts[0].first_id);
  EXPECT_EQ(level0->page_offset, glm::ivec2(5, 0));
  EXPECT_EQ(
    level0->rejection_reason, VsmReuseRejectionReason::kPageOffsetOutOfRange);

  EXPECT_EQ(
    level1->current_id, current_frame.directional_layouts[0].first_id + 1U);
  EXPECT_EQ(level1->page_offset, glm::ivec2(0, 0));
  EXPECT_EQ(
    level1->rejection_reason, VsmReuseRejectionReason::kPageOffsetOutOfRange);
}

NOLINT_TEST_F(
  VsmDirectionalRemapContractTest, RejectsDirectionalDepthRangeMismatch)
{
  const auto previous_frame
    = MakeDirectionalFrame(1ULL, 10U, std::array { MakeClipmapSpec("sun") },
      "vsm-remap.directional.depth.previous");
  const auto current_frame = MakeDirectionalFrame(2ULL, 100U,
    std::array { MakeClipmapSpec("sun", { { 0, 0 }, { 4, 4 } },
      { 32.0F, 64.0F }, { 1.0F, 2.0F }, { 101.0F, 210.0F }) },
    "vsm-remap.directional.depth.current");

  const auto table = BuildVirtualRemapTable(previous_frame, current_frame);

  ASSERT_EQ(table.entries.size(), 2U);
  EXPECT_EQ(table.entries[0].rejection_reason,
    VsmReuseRejectionReason::kDepthRangeMismatch);
  EXPECT_EQ(table.entries[1].rejection_reason,
    VsmReuseRejectionReason::kDepthRangeMismatch);
}

NOLINT_TEST_F(
  VsmDirectionalRemapContractTest, RejectsDirectionalPageWorldSizeMismatch)
{
  const auto previous_frame
    = MakeDirectionalFrame(1ULL, 10U, std::array { MakeClipmapSpec("sun") },
      "vsm-remap.directional.world-size.previous");
  const auto current_frame = MakeDirectionalFrame(2ULL, 100U,
    std::array {
      MakeClipmapSpec("sun", { { 0, 0 }, { 4, 4 } }, { 32.0F, 96.0F }) },
    "vsm-remap.directional.world-size.current");

  const auto table = BuildVirtualRemapTable(previous_frame, current_frame);

  ASSERT_EQ(table.entries.size(), 2U);
  EXPECT_EQ(table.entries[0].rejection_reason,
    VsmReuseRejectionReason::kPageWorldSizeMismatch);
  EXPECT_EQ(table.entries[1].rejection_reason,
    VsmReuseRejectionReason::kPageWorldSizeMismatch);
}

NOLINT_TEST_F(
  VsmDirectionalRemapContractTest, RejectsDirectionalPagesPerAxisMismatch)
{
  const auto previous_frame = MakeDirectionalFrame(1ULL, 10U,
    std::array { MakeClipmapSpec("sun", { { 0, 0 }, { 4, 4 } },
      { 32.0F, 64.0F }, { 1.0F, 2.0F }, { 101.0F, 202.0F }, 8U) },
    "vsm-remap.directional.pages-per-axis.previous");
  const auto current_frame
    = MakeDirectionalFrame(2ULL, 100U, std::array { MakeClipmapSpec("sun") },
      "vsm-remap.directional.pages-per-axis.current");

  const auto table = BuildVirtualRemapTable(previous_frame, current_frame);

  ASSERT_EQ(table.entries.size(), 2U);
  EXPECT_EQ(table.entries[0].rejection_reason,
    VsmReuseRejectionReason::kPagesPerAxisMismatch);
  EXPECT_EQ(table.entries[1].rejection_reason,
    VsmReuseRejectionReason::kPagesPerAxisMismatch);
}

NOLINT_TEST_F(VsmDirectionalRemapContractTest,
  RejectsMissingDirectionalRemapKeysPerPreviousLevel)
{
  const auto previous_frame
    = MakeDirectionalFrame(1ULL, 10U, std::array { MakeClipmapSpec("") },
      "vsm-remap.directional.missing-key.previous");
  const auto current_frame
    = MakeDirectionalFrame(2ULL, 100U, std::array { MakeClipmapSpec("sun") },
      "vsm-remap.directional.missing-key.current");

  const auto table = BuildVirtualRemapTable(previous_frame, current_frame);

  ASSERT_EQ(table.entries.size(), 2U);
  EXPECT_EQ(table.entries[0].rejection_reason,
    VsmReuseRejectionReason::kMissingRemapKey);
  EXPECT_EQ(table.entries[1].rejection_reason,
    VsmReuseRejectionReason::kMissingRemapKey);
  EXPECT_EQ(table.entries[0].current_id, 0U);
  EXPECT_EQ(table.entries[1].current_id, 0U);
}

NOLINT_TEST_F(VsmDirectionalRemapContractTest,
  RejectsDuplicateCurrentDirectionalRemapKeysForEachPreviousLevel)
{
  const auto previous_frame
    = MakeDirectionalFrame(1ULL, 10U, std::array { MakeClipmapSpec("sun") },
      "vsm-remap.directional.duplicate-current.previous");
  const auto current_frame = MakeDirectionalFrame(2ULL, 100U,
    std::array {
      MakeClipmapSpec("sun"), MakeClipmapSpec("sun", { { 1, 0 }, { 5, 4 } }) },
    "vsm-remap.directional.duplicate-current.current");

  const auto table = BuildVirtualRemapTable(previous_frame, current_frame);

  ASSERT_EQ(table.entries.size(), 2U);
  EXPECT_EQ(table.entries[0].rejection_reason,
    VsmReuseRejectionReason::kDuplicateRemapKey);
  EXPECT_EQ(table.entries[1].rejection_reason,
    VsmReuseRejectionReason::kDuplicateRemapKey);
  EXPECT_EQ(table.entries[0].current_id, 0U);
  EXPECT_EQ(table.entries[1].current_id, 0U);
}

NOLINT_TEST_F(VsmDirectionalRemapContractTest,
  RejectsDuplicatePreviousDirectionalRemapKeysForEachPreviousLevel)
{
  const auto previous_frame = MakeDirectionalFrame(1ULL, 10U,
    std::array { MakeClipmapSpec("sun"),
      MakeClipmapSpec("sun", { { 8, 8 }, { 12, 12 } }) },
    "vsm-remap.directional.duplicate-previous.previous");
  const auto current_frame
    = MakeDirectionalFrame(2ULL, 100U, std::array { MakeClipmapSpec("sun") },
      "vsm-remap.directional.duplicate-previous.current");

  const auto table = BuildVirtualRemapTable(previous_frame, current_frame);

  ASSERT_EQ(table.entries.size(), 4U);
  for (const auto& entry : table.entries) {
    EXPECT_EQ(entry.current_id, 0U);
    EXPECT_EQ(
      entry.rejection_reason, VsmReuseRejectionReason::kDuplicateRemapKey);
  }
}

NOLINT_TEST_F(VsmDirectionalRemapContractTest,
  RejectsMalformedCurrentClipmapLayoutsPerPreviousLevel)
{
  const auto previous_frame
    = MakeDirectionalFrame(1ULL, 10U, std::array { MakeClipmapSpec("sun") },
      "vsm-remap.directional.malformed.previous");
  auto malformed_current
    = MakeDirectionalFrame(2ULL, 100U, std::array { MakeClipmapSpec("sun") },
      "vsm-remap.directional.malformed.current");
  malformed_current.directional_layouts[0].page_world_size.pop_back();

  const auto table = BuildVirtualRemapTable(previous_frame, malformed_current);

  ASSERT_EQ(table.entries.size(), 2U);
  EXPECT_EQ(table.entries[0].current_id,
    malformed_current.directional_layouts[0].first_id);
  EXPECT_EQ(table.entries[1].current_id,
    malformed_current.directional_layouts[0].first_id + 1U);
  EXPECT_EQ(
    table.entries[0].rejection_reason, VsmReuseRejectionReason::kUnspecified);
  EXPECT_EQ(
    table.entries[1].rejection_reason, VsmReuseRejectionReason::kUnspecified);
}

NOLINT_TEST_F(VsmDirectionalRemapContractTest,
  RejectsMalformedPreviousClipmapLayoutsPerPreviousLevel)
{
  auto malformed_previous
    = MakeDirectionalFrame(1ULL, 10U, std::array { MakeClipmapSpec("sun") },
      "vsm-remap.directional.malformed-previous.previous");
  malformed_previous.directional_layouts[0].page_world_size.pop_back();
  const auto current_frame
    = MakeDirectionalFrame(2ULL, 100U, std::array { MakeClipmapSpec("sun") },
      "vsm-remap.directional.malformed-previous.current");

  const auto table = BuildVirtualRemapTable(malformed_previous, current_frame);

  ASSERT_EQ(table.entries.size(), 2U);
  EXPECT_EQ(table.entries[0].current_id, 0U);
  EXPECT_EQ(table.entries[1].current_id, 0U);
  EXPECT_EQ(
    table.entries[0].rejection_reason, VsmReuseRejectionReason::kUnspecified);
  EXPECT_EQ(
    table.entries[1].rejection_reason, VsmReuseRejectionReason::kUnspecified);
}

} // namespace
