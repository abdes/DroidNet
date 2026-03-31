//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstdint>
#include <numeric>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManagerTypes.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmPhysicalPagePoolTypes.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>

#include "VirtualShadowLiveSceneHarness.h"

namespace {

using oxygen::frame::SequenceNumber;
using oxygen::frame::Slot;
using oxygen::renderer::vsm::VsmPhysicalPoolSliceRole;
using oxygen::renderer::vsm::testing::HarnessSingleChannelTextureSnapshot;
using oxygen::renderer::vsm::testing::TwoBoxShadowHzbResult;
using oxygen::renderer::vsm::testing::TwoBoxShadowSceneData;
using oxygen::renderer::vsm::testing::VsmLiveSceneHarness;

class VsmShadowHzbLiveSceneTest : public VsmLiveSceneHarness {
protected:
  static constexpr auto kDepthEpsilon = 1.0e-4F;

  static auto DisableDirectionalShadowCasts(TwoBoxShadowSceneData& scene_data)
    -> void
  {
    auto sun_impl = scene_data.sun_node.GetImpl();
    ASSERT_TRUE(sun_impl.has_value());
    auto& sun_light
      = sun_impl->get().GetComponent<oxygen::scene::DirectionalLight>();
    sun_light.Common().casts_shadows = false;
    UpdateTransforms(*scene_data.scene, scene_data.sun_node);
  }

  [[nodiscard]] static auto ComputeLocalPageMipCount(
    const std::uint32_t page_size_texels) -> std::uint32_t
  {
    auto mip_count = 0U;
    auto extent = page_size_texels;
    while (extent > 1U) {
      extent >>= 1U;
      ++mip_count;
    }
    return mip_count;
  }

  [[nodiscard]] static auto MakeFilledSnapshot(
    const std::uint32_t width, const std::uint32_t height, const float value)
    -> HarnessSingleChannelTextureSnapshot
  {
    return HarnessSingleChannelTextureSnapshot {
      .width = width,
      .height = height,
      .values
      = std::vector<float>(static_cast<std::size_t>(width) * height, value),
    };
  }

  [[nodiscard]] static auto MaxReduce4(
    const float a, const float b, const float c, const float d) -> float
  {
    return std::max(std::max(a, b), std::max(c, d));
  }

  [[nodiscard]] static auto SnapshotValue(
    const HarnessSingleChannelTextureSnapshot& snapshot, const std::uint32_t x,
    const std::uint32_t y) -> float
  {
    return snapshot.values[static_cast<std::size_t>(y) * snapshot.width + x];
  }

  [[nodiscard]] auto ComputeSelectedDynamicPages(
    const TwoBoxShadowHzbResult& result) const -> std::vector<std::uint32_t>
  {
    const auto dynamic_slice
      = FindSliceIndex(result.merge.rasterization.initialization.physical_pool,
        VsmPhysicalPoolSliceRole::kDynamicDepth);
    CHECK_F(dynamic_slice.has_value(),
      "Stage 14 tests require a dynamic slice in the physical pool");

    const auto tiles_per_axis
      = result.merge.rasterization.initialization.physical_pool.tiles_per_axis;
    const auto tiles_per_slice = tiles_per_axis * tiles_per_axis;

    auto selected_pages = std::vector<std::uint32_t> {};
    for (std::uint32_t physical_page = 0U;
      physical_page < result.physical_metadata_before.size(); ++physical_page) {
      const auto& metadata = result.physical_metadata_before[physical_page];
      if (tiles_per_slice == 0U || !static_cast<bool>(metadata.is_allocated)) {
        continue;
      }
      if (physical_page / tiles_per_slice != *dynamic_slice) {
        continue;
      }
      if (result.dirty_flags_before[physical_page] == 0U
        && !static_cast<bool>(metadata.is_dirty)
        && !static_cast<bool>(metadata.view_uncached)) {
        continue;
      }
      selected_pages.push_back(physical_page);
    }
    return selected_pages;
  }

  [[nodiscard]] auto BuildExpectedHzbMipChain(
    const TwoBoxShadowHzbResult& result) const
    -> std::vector<HarnessSingleChannelTextureSnapshot>
  {
    CHECK_F(result.hzb_before_mips.size() == result.hzb_after_mips.size(),
      "Stage 14 HZB snapshot chain size mismatch");
    CHECK_F(result.hzb_before_mips.size() == result.hzb_pool.mip_count,
      "Stage 14 HZB snapshot chain does not match HZB pool mip count");

    auto expected = std::vector<HarnessSingleChannelTextureSnapshot> {};
    expected.reserve(result.hzb_pool.mip_count);
    if (result.can_preserve_existing_hzb_contents) {
      expected = result.hzb_before_mips;
    } else {
      for (const auto& mip : result.hzb_before_mips) {
        expected.push_back(MakeFilledSnapshot(mip.width, mip.height, 0.0F));
      }
    }

    const auto selected_pages = ComputeSelectedDynamicPages(result);
    const auto& pool = result.merge.rasterization.initialization.physical_pool;
    const auto page_size = pool.page_size_texels;
    const auto tiles_per_axis = pool.tiles_per_axis;
    const auto low_mip_count = (std::min)(ComputeLocalPageMipCount(page_size),
      result.hzb_pool.mip_count);

    for (std::uint32_t mip_level = 0U; mip_level < low_mip_count; ++mip_level) {
      auto& destination = expected[mip_level];
      const auto destination_page_extent
        = (std::max)(1U, (page_size >> 1U) >> mip_level);
      const auto source_page_extent = destination_page_extent * 2U;

      for (const auto physical_page : selected_pages) {
        const auto in_slice_page_index
          = physical_page % (tiles_per_axis * tiles_per_axis);
        const auto tile_x = in_slice_page_index % tiles_per_axis;
        const auto tile_y = in_slice_page_index / tiles_per_axis;
        const auto destination_base_x = tile_x * destination_page_extent;
        const auto destination_base_y = tile_y * destination_page_extent;
        const auto source_base_x = tile_x * source_page_extent;
        const auto source_base_y = tile_y * source_page_extent;

        for (std::uint32_t local_y = 0U; local_y < destination_page_extent;
          ++local_y) {
          for (std::uint32_t local_x = 0U; local_x < destination_page_extent;
            ++local_x) {
            const auto source_x = source_base_x + local_x * 2U;
            const auto source_y = source_base_y + local_y * 2U;
            const auto reduced_depth = mip_level == 0U
              ? MaxReduce4(result.merge.dynamic_after.At(source_x, source_y),
                  result.merge.dynamic_after.At(source_x + 1U, source_y),
                  result.merge.dynamic_after.At(source_x, source_y + 1U),
                  result.merge.dynamic_after.At(source_x + 1U, source_y + 1U))
              : MaxReduce4(
                  SnapshotValue(expected[mip_level - 1U], source_x, source_y),
                  SnapshotValue(
                    expected[mip_level - 1U], source_x + 1U, source_y),
                  SnapshotValue(
                    expected[mip_level - 1U], source_x, source_y + 1U),
                  SnapshotValue(
                    expected[mip_level - 1U], source_x + 1U, source_y + 1U));

            destination
              .values[static_cast<std::size_t>(destination_base_y + local_y)
                  * destination.width
                + destination_base_x + local_x]
              = reduced_depth;
          }
        }
      }
    }

    for (std::uint32_t mip_level = low_mip_count;
      mip_level < result.hzb_pool.mip_count; ++mip_level) {
      auto& destination = expected[mip_level];
      const auto& source = expected[mip_level - 1U];
      for (std::uint32_t y = 0U; y < destination.height; ++y) {
        for (std::uint32_t x = 0U; x < destination.width; ++x) {
          const auto source_x = x * 2U;
          const auto source_y = y * 2U;
          destination
            .values[static_cast<std::size_t>(y) * destination.width + x]
            = MaxReduce4(SnapshotValue(source, source_x, source_y),
              SnapshotValue(source, source_x + 1U, source_y),
              SnapshotValue(source, source_x, source_y + 1U),
              SnapshotValue(source, source_x + 1U, source_y + 1U));
        }
      }
    }

    return expected;
  }

  auto ExpectStageStateFoldedAsArchitected(
    const TwoBoxShadowHzbResult& result) const -> std::vector<std::uint32_t>
  {
    const auto selected_pages = ComputeSelectedDynamicPages(result);

    for (std::uint32_t physical_page = 0U;
      physical_page < result.physical_metadata_before.size(); ++physical_page) {
      const auto& before = result.physical_metadata_before[physical_page];
      const auto& after = result.physical_metadata_after[physical_page];
      const auto was_selected
        = std::find(selected_pages.begin(), selected_pages.end(), physical_page)
        != selected_pages.end();

      if (was_selected) {
        EXPECT_EQ(result.dirty_flags_after[physical_page], 0U)
          << "physical_page=" << physical_page;
        EXPECT_FALSE(static_cast<bool>(after.is_dirty))
          << "physical_page=" << physical_page;
        EXPECT_FALSE(static_cast<bool>(after.view_uncached))
          << "physical_page=" << physical_page;
        EXPECT_EQ(static_cast<bool>(after.is_allocated),
          static_cast<bool>(before.is_allocated))
          << "physical_page=" << physical_page;
        EXPECT_EQ(static_cast<bool>(after.used_this_frame),
          static_cast<bool>(before.used_this_frame))
          << "physical_page=" << physical_page;
        EXPECT_EQ(static_cast<bool>(after.static_invalidated),
          static_cast<bool>(before.static_invalidated))
          << "physical_page=" << physical_page;
        EXPECT_EQ(static_cast<bool>(after.dynamic_invalidated),
          static_cast<bool>(before.dynamic_invalidated))
          << "physical_page=" << physical_page;
        EXPECT_EQ(after.age, before.age) << "physical_page=" << physical_page;
        EXPECT_EQ(after.owner_id, before.owner_id)
          << "physical_page=" << physical_page;
        EXPECT_EQ(after.owner_mip_level, before.owner_mip_level)
          << "physical_page=" << physical_page;
        EXPECT_EQ(after.owner_page, before.owner_page)
          << "physical_page=" << physical_page;
        EXPECT_EQ(after.last_touched_frame, before.last_touched_frame)
          << "physical_page=" << physical_page;
      } else {
        EXPECT_EQ(result.dirty_flags_after[physical_page],
          result.dirty_flags_before[physical_page])
          << "physical_page=" << physical_page;
        EXPECT_EQ(after, before) << "physical_page=" << physical_page;
      }
    }

    return selected_pages;
  }

  auto ExpectExactHzbChain(const TwoBoxShadowHzbResult& result,
    const std::vector<HarnessSingleChannelTextureSnapshot>& expected) const
    -> void
  {
    ASSERT_EQ(result.hzb_after_mips.size(), expected.size());
    for (std::size_t mip_level = 0U; mip_level < expected.size(); ++mip_level) {
      const auto& actual = result.hzb_after_mips[mip_level];
      const auto& expected_mip = expected[mip_level];
      ASSERT_EQ(actual.width, expected_mip.width) << "mip=" << mip_level;
      ASSERT_EQ(actual.height, expected_mip.height) << "mip=" << mip_level;
      ASSERT_EQ(actual.values.size(), expected_mip.values.size())
        << "mip=" << mip_level;
      for (std::size_t texel = 0U; texel < expected_mip.values.size();
        ++texel) {
        EXPECT_NEAR(
          actual.values[texel], expected_mip.values[texel], kDepthEpsilon)
          << "mip=" << mip_level << " texel=" << texel;
      }
    }
  }
};

NOLINT_TEST_F(VsmShadowHzbLiveSceneTest,
  DirectionalTwoBoxSceneBuildsShadowHzbFromMergedDepthAndClearsSelectedPageState)
{
  constexpr auto kWidth = 1024U;
  constexpr auto kHeight = 1024U;
  constexpr auto kSlot = Slot { 0U };
  constexpr auto kSequence = SequenceNumber { 551U };

  const auto sun_direction
    = glm::normalize(glm::vec3 { -0.40558F, 0.40558F, 0.819152F });
  const auto resolved_view
    = MakeLookAtResolvedView(glm::vec3 { -3.1F, 3.3F, 5.4F },
      glm::vec3 { 0.0F, 0.8F, 0.0F }, kWidth, kHeight);

  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);
  auto scene = CreateTwoBoxShadowScene(sun_direction, 4U);
  auto vsm_renderer = oxygen::renderer::vsm::VsmShadowRenderer(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    oxygen::observer_ptr { &renderer->GetStagingProvider() },
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    oxygen::ShadowQualityTier::kHigh);

  const auto result = RunTwoBoxShadowHzbStage(*renderer, scene, vsm_renderer,
    resolved_view, kWidth, kHeight, kSequence, kSlot, 0xE140ULL);

  ASSERT_FALSE(result.merge.rasterization.prepared_pages.empty());
  ASSERT_FALSE(result.can_preserve_existing_hzb_contents);
  ASSERT_FALSE(result.merge.rasterization.initialization.propagation.mapping
      .bridge.prepared_products.virtual_frame.directional_layouts.empty());
  EXPECT_EQ(result.page_table_after, result.merge.page_table_after);
  EXPECT_EQ(result.page_flags_after, result.merge.page_flags_after);

  const auto selected_pages = ExpectStageStateFoldedAsArchitected(result);
  ASSERT_FALSE(selected_pages.empty());

  const auto expected = BuildExpectedHzbMipChain(result);
  ExpectExactHzbChain(result, expected);
}

NOLINT_TEST_F(VsmShadowHzbLiveSceneTest,
  StableDirectionalScenePreservesExistingShadowHzbWhenNoPagesNeedRebuild)
{
  constexpr auto kWidth = 1024U;
  constexpr auto kHeight = 1024U;
  constexpr auto kSlot = Slot { 0U };
  constexpr auto kFirstSequence = SequenceNumber { 552U };
  constexpr auto kSecondSequence = SequenceNumber { 553U };

  const auto sun_direction
    = glm::normalize(glm::vec3 { -0.40558F, 0.40558F, 0.819152F });
  const auto resolved_view
    = MakeLookAtResolvedView(glm::vec3 { -3.1F, 3.3F, 5.4F },
      glm::vec3 { 0.0F, 0.8F, 0.0F }, kWidth, kHeight);

  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);
  auto scene = CreateTwoBoxShadowScene(sun_direction, 4U);
  auto vsm_renderer = oxygen::renderer::vsm::VsmShadowRenderer(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    oxygen::observer_ptr { &renderer->GetStagingProvider() },
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    oxygen::ShadowQualityTier::kHigh);

  const auto first = RunTwoBoxShadowHzbStage(*renderer, scene, vsm_renderer,
    resolved_view, kWidth, kHeight, kFirstSequence, kSlot, 0xE141ULL);
  const auto first_selected_pages = ExpectStageStateFoldedAsArchitected(first);
  ASSERT_FALSE(first_selected_pages.empty());
  vsm_renderer.GetCacheManager().PublishCurrentFrameHzbAvailability(
    first.hzb_pool.is_available);
  vsm_renderer.GetCacheManager().ExtractFrameData();

  const auto second = RunTwoBoxShadowHzbStage(*renderer, scene, vsm_renderer,
    resolved_view, kWidth, kHeight, kSecondSequence, kSlot, 0xE141ULL);
  ASSERT_TRUE(second.can_preserve_existing_hzb_contents);
  EXPECT_EQ(second.page_table_after, second.merge.page_table_after);
  EXPECT_EQ(second.page_flags_after, second.merge.page_flags_after);

  const auto selected_pages = ExpectStageStateFoldedAsArchitected(second);
  EXPECT_TRUE(selected_pages.empty());
  for (std::size_t mip_level = 0U; mip_level < second.hzb_before_mips.size();
    ++mip_level) {
    EXPECT_EQ(second.hzb_after_mips[mip_level].values,
      second.hzb_before_mips[mip_level].values)
      << "mip=" << mip_level;
  }
}

NOLINT_TEST_F(VsmShadowHzbLiveSceneTest,
  PagedSpotLightSceneBuildsLocalShadowHzbFromRealMergedDepth)
{
  constexpr auto kWidth = 1024U;
  constexpr auto kHeight = 1024U;
  constexpr auto kSlot = Slot { 0U };
  constexpr auto kSequence = SequenceNumber { 554U };

  const auto camera_eye = glm::vec3 { -2.4F, 2.8F, 4.8F };
  const auto camera_target = glm::vec3 { 0.1F, 0.9F, 0.1F };
  const auto sun_direction
    = glm::normalize(glm::vec3 { -0.40558F, 0.40558F, 0.819152F });
  const auto resolved_view
    = MakeLookAtResolvedView(camera_eye, camera_target, kWidth, kHeight);

  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);
  auto scene = CreateTwoBoxShadowScene(sun_direction, 4U);
  DisableDirectionalShadowCasts(scene);
  AttachSpotLightToTwoBoxScene(scene, camera_eye,
    PrimarySpotTargetForTwoBoxScene(scene), 18.0F, glm::radians(30.0F),
    glm::radians(44.0F));

  auto vsm_renderer = oxygen::renderer::vsm::VsmShadowRenderer(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    oxygen::observer_ptr { &renderer->GetStagingProvider() },
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    oxygen::ShadowQualityTier::kHigh);

  const auto result = RunTwoBoxShadowHzbStage(*renderer, scene, vsm_renderer,
    resolved_view, kWidth, kHeight, kSequence, kSlot, 0xE142ULL);

  ASSERT_FALSE(result.can_preserve_existing_hzb_contents);
  ASSERT_TRUE(result.merge.rasterization.initialization.propagation.mapping
      .bridge.prepared_products.virtual_frame.directional_layouts.empty());
  ASSERT_EQ(result.merge.rasterization.initialization.propagation.mapping.bridge
              .prepared_products.virtual_frame.local_light_layouts.size(),
    1U);
  EXPECT_GT(result.merge.rasterization.initialization.propagation.mapping.bridge
              .prepared_products.virtual_frame.local_light_layouts.front()
              .total_page_count,
    1U);
  EXPECT_EQ(result.page_table_after, result.merge.page_table_after);
  EXPECT_EQ(result.page_flags_after, result.merge.page_flags_after);

  const auto selected_pages = ExpectStageStateFoldedAsArchitected(result);
  ASSERT_FALSE(selected_pages.empty());

  const auto expected = BuildExpectedHzbMipChain(result);
  ExpectExactHzbChain(result, expected);
}

} // namespace
