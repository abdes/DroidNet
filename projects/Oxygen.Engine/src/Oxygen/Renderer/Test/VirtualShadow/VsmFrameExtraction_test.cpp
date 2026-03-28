//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Renderer/Types/DrawMetadata.h>
#include <Oxygen/Renderer/Types/PassMask.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManager.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmProjectionTypes.h>

#include "VirtualShadowLiveSceneHarness.h"

namespace {

using oxygen::engine::DrawPrimitiveFlagBits;
using oxygen::engine::PassMaskBit;
using oxygen::frame::SequenceNumber;
using oxygen::frame::Slot;
using oxygen::renderer::vsm::VsmCacheBuildState;
using oxygen::renderer::vsm::VsmCacheDataState;
using oxygen::renderer::vsm::VsmExtractedCacheFrame;
using oxygen::renderer::vsm::VsmProjectionLightType;
using oxygen::renderer::vsm::testing::TwoBoxShadowSceneData;
using oxygen::renderer::vsm::testing::VsmLiveSceneHarness;

class VsmFrameExtractionLiveSceneTest : public VsmLiveSceneHarness {
protected:
  static constexpr auto kWidth = 256U;
  static constexpr auto kHeight = 256U;
  static constexpr auto kSlot = Slot { 0U };

  [[nodiscard]] static auto MakeDirectionalView(
    const glm::vec3 offset = glm::vec3 { 0.0F }) -> oxygen::ResolvedView
  {
    return MakeLookAtResolvedView(glm::vec3 { -3.2F, 3.4F, 5.8F } + offset,
      glm::vec3 { 0.2F, 0.8F, 0.0F } + offset, kWidth, kHeight);
  }

  [[nodiscard]] static auto MakeSunDirection() -> glm::vec3
  {
    return glm::normalize(glm::vec3 { -0.40558F, 0.40558F, 0.819152F });
  }

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

  static auto MarkTallBoxStaticShadowCaster(TwoBoxShadowSceneData& scene_data)
    -> void
  {
    scene_data.rendered_items[1].static_shadow_caster = true;
    scene_data.draw_records[1].primitive_flags
      |= static_cast<std::uint32_t>(DrawPrimitiveFlagBits::kStaticShadowCaster);
  }

  static auto DisableShortBoxShadowCaster(TwoBoxShadowSceneData& scene_data)
    -> void
  {
    scene_data.rendered_items[2].cast_shadows = false;
    scene_data.draw_records[2].flags.Unset(PassMaskBit::kShadowCaster);
    scene_data.draw_records[2].primitive_flags
      &= ~static_cast<std::uint32_t>(DrawPrimitiveFlagBits::kMainViewVisible);
    scene_data.shadow_caster_bounds[1].w = 0.0F;
  }

  [[nodiscard]] static auto CountMappedPages(
    const VsmExtractedCacheFrame& snapshot) -> std::size_t
  {
    return static_cast<std::size_t>(
      std::count_if(snapshot.page_table.begin(), snapshot.page_table.end(),
        [](const auto& entry) { return entry.is_mapped; }));
  }

  [[nodiscard]] static auto CountProjectionType(
    const VsmExtractedCacheFrame& snapshot, const VsmProjectionLightType type)
    -> std::size_t
  {
    return static_cast<std::size_t>(std::count_if(
      snapshot.projection_records.begin(), snapshot.projection_records.end(),
      [type](const auto& record) {
        return record.projection.light_type == static_cast<std::uint32_t>(type);
      }));
  }

  [[nodiscard]] static auto TryResolveOwnerLevelCount(
    const VsmExtractedCacheFrame& snapshot, const std::uint32_t owner_id)
    -> std::optional<std::uint32_t>
  {
    for (const auto& layout : snapshot.virtual_frame.local_light_layouts) {
      if (layout.id == owner_id) {
        return layout.level_count;
      }
    }

    for (const auto& layout : snapshot.virtual_frame.directional_layouts) {
      if (owner_id >= layout.first_id
        && owner_id < layout.first_id + layout.clip_level_count) {
        return layout.clip_level_count;
      }
    }

    return std::nullopt;
  }

  static auto ExpectMappedEntriesReferenceValidPhysicalPages(
    const VsmExtractedCacheFrame& snapshot) -> void
  {
    ASSERT_EQ(snapshot.page_table.size(),
      snapshot.virtual_frame.total_page_table_entry_count);
    ASSERT_EQ(snapshot.physical_pages.size(), snapshot.pool_tile_capacity);

    auto mapped_page_count = std::size_t { 0U };
    for (const auto& entry : snapshot.page_table) {
      if (!entry.is_mapped) {
        continue;
      }
      ++mapped_page_count;
      ASSERT_LT(entry.physical_page.value, snapshot.physical_pages.size());
      const auto& metadata = snapshot.physical_pages[entry.physical_page.value];
      EXPECT_TRUE(static_cast<bool>(metadata.is_allocated));
      EXPECT_NE(metadata.owner_id, 0U);
      const auto owner_level_count
        = TryResolveOwnerLevelCount(snapshot, metadata.owner_id);
      ASSERT_TRUE(owner_level_count.has_value())
        << "unresolved owner_id=" << metadata.owner_id;
      EXPECT_LT(metadata.owner_mip_level, *owner_level_count);
      EXPECT_EQ(metadata.owner_mip_level, metadata.owner_page.level);
    }

    EXPECT_GT(mapped_page_count, 0U);
  }

  static auto ExpectStaticFeedbackContracts(
    const VsmExtractedCacheFrame& snapshot) -> void
  {
    ASSERT_FALSE(snapshot.static_primitive_page_feedback.empty())
      << "generation=" << snapshot.frame_generation
      << " visible=" << snapshot.visible_shadow_primitives.size()
      << " rendered_history=" << snapshot.rendered_primitive_history.size();
    for (const auto& feedback : snapshot.static_primitive_page_feedback) {
      EXPECT_EQ(feedback.valid, 1U);
      ASSERT_LT(feedback.page_table_index, snapshot.page_table.size());
      EXPECT_TRUE(snapshot.page_table[feedback.page_table_index].is_mapped);
      EXPECT_EQ(snapshot.page_table[feedback.page_table_index].physical_page,
        feedback.physical_page);
    }
  }
};

NOLINT_TEST_F(VsmFrameExtractionLiveSceneTest,
  DirectionalTwoBoxLiveShellPublishesReusableExtractedSnapshot)
{
  constexpr auto kSequence = SequenceNumber { 411U };

  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);
  auto scene = CreateTwoBoxShadowScene(MakeSunDirection(), 4U);
  auto vsm_renderer = oxygen::renderer::vsm::VsmShadowRenderer(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    oxygen::observer_ptr { &renderer->GetStagingProvider() },
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    oxygen::ShadowQualityTier::kHigh);

  const auto result = RunTwoBoxLiveShellFrame(*renderer, scene, vsm_renderer,
    MakeDirectionalView(), kWidth, kHeight, kSequence, kSlot, 0xE160ULL);

  ASSERT_NE(result.extracted_frame, nullptr);
  const auto& snapshot = *result.extracted_frame;

  EXPECT_EQ(vsm_renderer.GetCacheManager().DescribeBuildState(),
    VsmCacheBuildState::kIdle);
  EXPECT_EQ(vsm_renderer.GetCacheManager().DescribeCacheDataState(),
    VsmCacheDataState::kAvailable);
  EXPECT_EQ(snapshot.frame_generation, kSequence.get());
  EXPECT_EQ(snapshot.virtual_frame, result.virtual_frame);
  EXPECT_EQ(snapshot.pool_page_size_texels, 128U);
  EXPECT_GT(snapshot.pool_tile_capacity, 0U);
  EXPECT_GT(snapshot.pool_slice_count, 0U);
  EXPECT_TRUE(snapshot.is_hzb_data_available);
  EXPECT_FALSE(snapshot.projection_records.empty());
  EXPECT_EQ(CountProjectionType(snapshot, VsmProjectionLightType::kDirectional),
    snapshot.virtual_frame.directional_layouts.front().clip_level_count);
  EXPECT_FALSE(snapshot.visible_shadow_primitives.empty());
  ExpectMappedEntriesReferenceValidPhysicalPages(snapshot);
}

NOLINT_TEST_F(VsmFrameExtractionLiveSceneTest,
  StaticDirectionalContinuityPersistsAcrossConsecutiveExtractedFrames)
{
  constexpr auto kFirstSequence = SequenceNumber { 421U };
  constexpr auto kSecondSequence = SequenceNumber { 422U };

  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);
  auto scene = CreateTwoBoxShadowScene(MakeSunDirection(), 4U);
  MarkTallBoxStaticShadowCaster(scene);
  DisableShortBoxShadowCaster(scene);
  auto vsm_renderer = oxygen::renderer::vsm::VsmShadowRenderer(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    oxygen::observer_ptr { &renderer->GetStagingProvider() },
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    oxygen::ShadowQualityTier::kHigh);

  const auto first_frame
    = RunTwoBoxLiveShellFrame(*renderer, scene, vsm_renderer,
      MakeDirectionalView(), kWidth, kHeight, kFirstSequence, kSlot, 0xE161ULL);
  ASSERT_NE(first_frame.extracted_frame, nullptr);
  ASSERT_FALSE(first_frame.extracted_frame->rendered_primitive_history.empty());
  ExpectStaticFeedbackContracts(*first_frame.extracted_frame);

  const auto second_frame = RunTwoBoxLiveShellFrame(*renderer, scene,
    vsm_renderer, MakeDirectionalView(), kWidth, kHeight, kSecondSequence,
    kSlot, 0xE161ULL);
  ASSERT_NE(second_frame.extracted_frame, nullptr);
  EXPECT_EQ(second_frame.extracted_frame->rendered_primitive_history.size(),
    first_frame.extracted_frame->rendered_primitive_history.size());
  EXPECT_EQ(second_frame.extracted_frame->static_primitive_page_feedback.size(),
    first_frame.extracted_frame->static_primitive_page_feedback.size());
  ExpectStaticFeedbackContracts(*second_frame.extracted_frame);
}

NOLINT_TEST_F(VsmFrameExtractionLiveSceneTest,
  PagedSpotLightLiveShellExtractsLocalProjectionAndMappedPages)
{
  constexpr auto kSequence = SequenceNumber { 431U };

  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);
  auto scene = CreateTwoBoxShadowScene(MakeSunDirection(), 1U);
  DisableDirectionalShadowCasts(scene);
  const auto camera_eye = glm::vec3 { -3.2F, 3.4F, 5.8F };
  AttachSpotLightToTwoBoxScene(scene, camera_eye,
    PrimarySpotTargetForTwoBoxScene(scene), 18.0F, glm::radians(30.0F),
    glm::radians(50.0F));
  auto vsm_renderer = oxygen::renderer::vsm::VsmShadowRenderer(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    oxygen::observer_ptr { &renderer->GetStagingProvider() },
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    oxygen::ShadowQualityTier::kHigh);

  const auto result = RunTwoBoxLiveShellFrame(*renderer, scene, vsm_renderer,
    MakeDirectionalView(), kWidth, kHeight, kSequence, kSlot, 0xE162ULL);

  ASSERT_NE(result.extracted_frame, nullptr);
  const auto& snapshot = *result.extracted_frame;
  ASSERT_EQ(snapshot.virtual_frame.local_light_layouts.size(), 1U);

  EXPECT_EQ(
    CountProjectionType(snapshot, VsmProjectionLightType::kDirectional), 0U);
  EXPECT_EQ(CountProjectionType(snapshot, VsmProjectionLightType::kLocal), 1U);
  EXPECT_FALSE(snapshot.visible_shadow_primitives.empty());
  EXPECT_TRUE(snapshot.is_hzb_data_available);
  ExpectMappedEntriesReferenceValidPhysicalPages(snapshot);

  const auto owner_id = snapshot.virtual_frame.local_light_layouts.front().id;
  EXPECT_TRUE(std::any_of(snapshot.physical_pages.begin(),
    snapshot.physical_pages.end(), [owner_id](const auto& metadata) {
      return static_cast<bool>(metadata.is_allocated)
        && metadata.owner_id == owner_id;
    }));
}

NOLINT_TEST_F(VsmFrameExtractionLiveSceneTest,
  NextBridgeBeginFrameFinalizesQueuedExtractionWithoutDroppingPreviousSnapshot)
{
  constexpr auto kFirstSequence = SequenceNumber { 441U };
  constexpr auto kSecondSequence = SequenceNumber { 442U };

  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);
  auto scene = CreateTwoBoxShadowScene(MakeSunDirection(), 4U);
  auto vsm_renderer = oxygen::renderer::vsm::VsmShadowRenderer(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    oxygen::observer_ptr { &renderer->GetStagingProvider() },
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    oxygen::ShadowQualityTier::kHigh);

  const auto first_frame
    = RunTwoBoxLiveShellFrame(*renderer, scene, vsm_renderer,
      MakeDirectionalView(), kWidth, kHeight, kFirstSequence, kSlot, 0xE163ULL);
  ASSERT_NE(first_frame.extracted_frame, nullptr);
  ASSERT_GT(CountMappedPages(*first_frame.extracted_frame), 0U);

  static_cast<void>(RunTwoBoxPageRequestBridge(*renderer, scene, vsm_renderer,
    MakeDirectionalView(glm::vec3 { 0.35F, 0.0F, 0.0F }), kWidth, kHeight,
    kSecondSequence, kSlot, 0xE163ULL));

  const auto* finalized_previous
    = vsm_renderer.GetCacheManager().GetPreviousFrame();
  ASSERT_NE(finalized_previous, nullptr);
  EXPECT_EQ(finalized_previous->frame_generation, kFirstSequence.get());
  EXPECT_EQ(finalized_previous->virtual_frame, first_frame.virtual_frame);
  EXPECT_EQ(vsm_renderer.GetCacheManager().DescribeCacheDataState(),
    VsmCacheDataState::kAvailable);
  EXPECT_EQ(vsm_renderer.GetCacheManager().DescribeBuildState(),
    VsmCacheBuildState::kReady);
  EXPECT_GT(CountMappedPages(*finalized_previous), 0U);
  ExpectMappedEntriesReferenceValidPhysicalPages(*finalized_previous);
}

} // namespace
