//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Renderer/Types/DrawMetadata.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManagerTypes.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmPhysicalPagePoolTypes.h>

#include "VirtualShadowLiveSceneHarness.h"

namespace {

using oxygen::engine::DrawPrimitiveFlagBits;
using oxygen::engine::PassMaskBit;
using oxygen::frame::SequenceNumber;
using oxygen::frame::Slot;
using oxygen::renderer::vsm::VsmCacheInvalidationReason;
using oxygen::renderer::vsm::VsmCacheInvalidationScope;
using oxygen::renderer::vsm::VsmRenderedPageDirtyFlagBits;
using oxygen::renderer::vsm::testing::TwoBoxShadowSceneData;
using oxygen::renderer::vsm::testing::TwoBoxStaticDynamicMergeResult;
using oxygen::renderer::vsm::testing::VsmLiveSceneHarness;

class VsmStaticDynamicMergeLocalLiveSceneTest : public VsmLiveSceneHarness {
protected:
  static constexpr auto kDifferenceEpsilon = 1.0e-4F;

  struct MergeContractStats {
    std::size_t eligible_page_count { 0U };
    std::size_t sampled_texel_count { 0U };
    std::size_t static_shadow_sample_count { 0U };
    std::size_t static_dominates_dynamic_sample_count { 0U };
    std::size_t dynamic_dominates_static_sample_count { 0U };
  };

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

  static auto EnableShortBoxShadowCaster(TwoBoxShadowSceneData& scene_data)
    -> void
  {
    scene_data.rendered_items[2].cast_shadows = true;
    scene_data.draw_records[2].flags.Set(PassMaskBit::kShadowCaster);
    scene_data.draw_records[2].primitive_flags
      |= static_cast<std::uint32_t>(DrawPrimitiveFlagBits::kMainViewVisible);
    scene_data.shadow_caster_bounds[1] = scene_data.draw_bounds[2];
  }

  static auto MoveBox(TwoBoxShadowSceneData& scene_data,
    const std::size_t index, oxygen::scene::SceneNode& node,
    const glm::vec3& translation, const glm::vec3& scale) -> void
  {
    scene_data.world_matrices[index]
      = glm::translate(glm::mat4 { 1.0F }, translation)
      * glm::scale(glm::mat4 { 1.0F }, scale);

    const auto radius = scale.y > 2.0F ? 1.75F : 0.95F;
    scene_data.draw_bounds[index]
      = glm::vec4 { translation.x, scale.y * 0.5F, translation.z, radius };
    scene_data.shadow_caster_bounds[index - 1U] = scene_data.draw_bounds[index];
    scene_data.rendered_items[index].world_bounding_sphere
      = scene_data.draw_bounds[index];

    ASSERT_TRUE(node.GetTransform().SetLocalPosition(translation));
    auto impl = node.GetImpl();
    ASSERT_TRUE(impl.has_value());
    impl->get().UpdateTransforms(*scene_data.scene);
    scene_data.scene->Update();
  }

  static auto MoveShortBox(
    TwoBoxShadowSceneData& scene_data, const glm::vec3& translation) -> void
  {
    MoveBox(scene_data, 2U, scene_data.short_box_node, translation,
      glm::vec3 { 0.8F, 1.1F, 0.8F });
  }

  [[nodiscard]] static auto ExpectDynamicMergeContract(
    const TwoBoxStaticDynamicMergeResult& result) -> MergeContractStats
  {
    auto stats = MergeContractStats {};
    const auto& pool = result.rasterization.initialization.physical_pool;
    constexpr auto kGrid = 5U;

    for (const auto& job : result.rasterization.prepared_pages) {
      const auto physical_page = job.physical_page.value;
      if (physical_page >= result.rasterization.dirty_flags.size()
        || physical_page >= result.rasterization.physical_metadata.size()) {
        continue;
      }

      const auto& metadata
        = result.rasterization.physical_metadata[physical_page];
      if (result.rasterization.dirty_flags[physical_page] == 0U
        || !static_cast<bool>(metadata.is_allocated)
        || static_cast<bool>(metadata.static_invalidated)) {
        continue;
      }
      ++stats.eligible_page_count;

      for (std::uint32_t grid_y = 0U; grid_y < kGrid; ++grid_y) {
        for (std::uint32_t grid_x = 0U; grid_x < kGrid; ++grid_x) {
          const auto x = job.physical_coord.tile_x * pool.page_size_texels
            + ((2U * grid_x + 1U) * pool.page_size_texels) / (2U * kGrid);
          const auto y = job.physical_coord.tile_y * pool.page_size_texels
            + ((2U * grid_y + 1U) * pool.page_size_texels) / (2U * kGrid);
          const auto dynamic_before = result.dynamic_before.At(x, y);
          const auto static_before = result.static_before.At(x, y);
          const auto dynamic_after = result.dynamic_after.At(x, y);
          const auto static_after = result.static_after.At(x, y);

          ++stats.sampled_texel_count;
          if (static_before < 1.0F - kDifferenceEpsilon) {
            ++stats.static_shadow_sample_count;
          }
          if (static_before + kDifferenceEpsilon < dynamic_before) {
            ++stats.static_dominates_dynamic_sample_count;
          }
          if (dynamic_before + kDifferenceEpsilon < static_before) {
            ++stats.dynamic_dominates_static_sample_count;
          }

          EXPECT_NEAR(
            dynamic_after, std::min(dynamic_before, static_before), 1.0e-4F)
            << "physical_page=" << physical_page << " sample=(" << x << "," << y
            << ")";
          EXPECT_NEAR(static_after, static_before, 1.0e-4F)
            << "physical_page=" << physical_page << " sample=(" << x << "," << y
            << ")";
        }
      }
    }

    return stats;
  }

  [[nodiscard]] static auto ExpectNoStaleStaticMergeContract(
    const TwoBoxStaticDynamicMergeResult& result) -> MergeContractStats
  {
    auto stats = MergeContractStats {};
    const auto& pool = result.rasterization.initialization.physical_pool;
    constexpr auto kGrid = 5U;

    for (const auto& job : result.rasterization.prepared_pages) {
      const auto physical_page = job.physical_page.value;
      if (physical_page >= result.rasterization.dirty_flags.size()
        || physical_page >= result.rasterization.physical_metadata.size()) {
        continue;
      }

      const auto& metadata
        = result.rasterization.physical_metadata[physical_page];
      if (result.rasterization.dirty_flags[physical_page] == 0U
        || !static_cast<bool>(metadata.is_allocated)) {
        continue;
      }
      ++stats.eligible_page_count;

      for (std::uint32_t grid_y = 0U; grid_y < kGrid; ++grid_y) {
        for (std::uint32_t grid_x = 0U; grid_x < kGrid; ++grid_x) {
          const auto x = job.physical_coord.tile_x * pool.page_size_texels
            + ((2U * grid_x + 1U) * pool.page_size_texels) / (2U * kGrid);
          const auto y = job.physical_coord.tile_y * pool.page_size_texels
            + ((2U * grid_y + 1U) * pool.page_size_texels) / (2U * kGrid);
          const auto dynamic_before = result.dynamic_before.At(x, y);
          const auto static_before = result.static_before.At(x, y);
          const auto dynamic_after = result.dynamic_after.At(x, y);
          const auto static_after = result.static_after.At(x, y);

          ++stats.sampled_texel_count;
          if (static_before < 1.0F - kDifferenceEpsilon) {
            ++stats.static_shadow_sample_count;
          }
          if (static_before + kDifferenceEpsilon < dynamic_before) {
            ++stats.static_dominates_dynamic_sample_count;
          }
          if (dynamic_before + kDifferenceEpsilon < static_before) {
            ++stats.dynamic_dominates_static_sample_count;
          }

          EXPECT_NEAR(dynamic_after, dynamic_before, 1.0e-4F)
            << "physical_page=" << physical_page << " sample=(" << x << "," << y
            << ")";
          EXPECT_NEAR(static_after, static_before, 1.0e-4F)
            << "physical_page=" << physical_page << " sample=(" << x << "," << y
            << ")";
        }
      }
    }

    return stats;
  }

  static auto ExpectValidExtractedStaticFeedback(
    const oxygen::renderer::vsm::VsmExtractedCacheFrame& frame) -> void
  {
    ASSERT_FALSE(frame.static_primitive_page_feedback.empty())
      << "generation=" << frame.frame_generation
      << " visible=" << frame.visible_shadow_primitives.size()
      << " rendered_history=" << frame.rendered_primitive_history.size();
    for (const auto& feedback : frame.static_primitive_page_feedback) {
      EXPECT_EQ(feedback.valid, 1U);
      ASSERT_LT(feedback.page_table_index, frame.page_table.size());
      EXPECT_TRUE(frame.page_table[feedback.page_table_index].is_mapped);
      EXPECT_EQ(frame.page_table[feedback.page_table_index].physical_page,
        feedback.physical_page);
    }
  }
};

NOLINT_TEST_F(VsmStaticDynamicMergeLocalLiveSceneTest,
  StableSpotLightStaticContinuityPersistsAcrossFullyReusedLiveFrame)
{
  constexpr auto kWidth = 1024U;
  constexpr auto kHeight = 1024U;
  constexpr auto kSlot = Slot { 0U };
  constexpr auto kFirstSequence = SequenceNumber { 418U };
  constexpr auto kSecondSequence = SequenceNumber { 419U };

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
  MarkTallBoxStaticShadowCaster(scene);
  DisableShortBoxShadowCaster(scene);
  AttachSpotLightToTwoBoxScene(scene, camera_eye,
    PrimarySpotTargetForTwoBoxScene(scene), 18.0F, glm::radians(30.0F),
    glm::radians(44.0F));

  auto vsm_renderer = oxygen::renderer::vsm::VsmShadowRenderer(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    oxygen::observer_ptr { &renderer->GetStagingProvider() },
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    oxygen::ShadowQualityTier::kHigh);

  const auto first_frame
    = RunTwoBoxLiveShellFrame(*renderer, scene, vsm_renderer, resolved_view,
      kWidth, kHeight, kFirstSequence, kSlot, 0xD134AULL);
  ASSERT_NE(first_frame.extracted_frame, nullptr);
  ASSERT_TRUE(first_frame.virtual_frame.directional_layouts.empty());
  ASSERT_EQ(first_frame.virtual_frame.local_light_layouts.size(), 1U);
  ASSERT_FALSE(first_frame.extracted_frame->rendered_primitive_history.empty());
  ExpectValidExtractedStaticFeedback(*first_frame.extracted_frame);

  const auto second_frame
    = RunTwoBoxLiveShellFrame(*renderer, scene, vsm_renderer, resolved_view,
      kWidth, kHeight, kSecondSequence, kSlot, 0xD134AULL);
  ASSERT_NE(second_frame.extracted_frame, nullptr);
  ASSERT_TRUE(second_frame.virtual_frame.directional_layouts.empty());
  ASSERT_EQ(second_frame.virtual_frame.local_light_layouts.size(), 1U);
  EXPECT_EQ(second_frame.extracted_frame->rendered_primitive_history.size(),
    first_frame.extracted_frame->rendered_primitive_history.size());
  EXPECT_EQ(second_frame.extracted_frame->static_primitive_page_feedback.size(),
    first_frame.extracted_frame->static_primitive_page_feedback.size());
  ExpectValidExtractedStaticFeedback(*second_frame.extracted_frame);
}

NOLINT_TEST_F(VsmStaticDynamicMergeLocalLiveSceneTest,
  DynamicOnlyInvalidatedSpotLightSceneSatisfiesStaticIntoDynamicMergeContractOnDirtyPages)
{
  constexpr auto kWidth = 1024U;
  constexpr auto kHeight = 1024U;
  constexpr auto kSlot = Slot { 0U };
  constexpr auto kFirstSequence = SequenceNumber { 421U };
  constexpr auto kSecondSequence = SequenceNumber { 422U };
  constexpr auto kThirdSequence = SequenceNumber { 423U };

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
  MarkTallBoxStaticShadowCaster(scene);
  DisableShortBoxShadowCaster(scene);
  AttachSpotLightToTwoBoxScene(scene, camera_eye,
    PrimarySpotTargetForTwoBoxScene(scene), 18.0F, glm::radians(30.0F),
    glm::radians(44.0F));

  auto vsm_renderer = oxygen::renderer::vsm::VsmShadowRenderer(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    oxygen::observer_ptr { &renderer->GetStagingProvider() },
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    oxygen::ShadowQualityTier::kHigh);

  const auto first_frame
    = RunTwoBoxLiveShellFrame(*renderer, scene, vsm_renderer, resolved_view,
      kWidth, kHeight, kFirstSequence, kSlot, 0xD134ULL);
  ASSERT_NE(first_frame.extracted_frame, nullptr);
  ASSERT_TRUE(first_frame.virtual_frame.directional_layouts.empty());
  ASSERT_EQ(first_frame.virtual_frame.local_light_layouts.size(), 1U);

  const auto second_frame
    = RunTwoBoxLiveShellFrame(*renderer, scene, vsm_renderer, resolved_view,
      kWidth, kHeight, kSecondSequence, kSlot, 0xD134ULL);
  ASSERT_NE(second_frame.extracted_frame, nullptr);
  ASSERT_EQ(second_frame.virtual_frame.local_light_layouts.size(), 1U);
  ASSERT_FALSE(
    second_frame.extracted_frame->static_primitive_page_feedback.empty());
  ASSERT_FALSE(
    second_frame.extracted_frame->rendered_primitive_history.empty());

  EnableShortBoxShadowCaster(scene);
  MoveShortBox(scene, glm::vec3 { 1.20F, 0.0F, -0.10F });
  const auto local_key
    = second_frame.virtual_frame.local_light_layouts.front().remap_key;
  vsm_renderer.GetCacheManager().InvalidateLocalLights({ local_key },
    VsmCacheInvalidationScope::kDynamicOnly,
    VsmCacheInvalidationReason::kTargetedInvalidate);

  const auto result
    = RunTwoBoxStaticDynamicMergeStage(*renderer, scene, vsm_renderer,
      resolved_view, kWidth, kHeight, kThirdSequence, kSlot, 0xD135ULL);

  ASSERT_FALSE(result.rasterization.prepared_pages.empty());
  EXPECT_EQ(
    result.page_table_after, result.rasterization.initialization.page_table);
  EXPECT_EQ(
    result.page_flags_after, result.rasterization.initialization.page_flags);
  EXPECT_EQ(
    result.physical_metadata_after, result.rasterization.physical_metadata);
  EXPECT_EQ(result.static_after.values, result.static_before.values);
  const auto merge_candidates
    = BuildStaticMergeCandidateLogicalPages(result.rasterization.prepared_pages,
      result.rasterization.initialization.physical_pool);
  EXPECT_TRUE(merge_candidates.empty());
  EXPECT_EQ(result.dynamic_after.values, result.dynamic_before.values);

  const auto stats = ExpectDynamicMergeContract(result);
  EXPECT_EQ(stats.eligible_page_count, 0U);
  EXPECT_EQ(stats.sampled_texel_count, 0U);
  EXPECT_EQ(stats.static_shadow_sample_count, 0U);
}

NOLINT_TEST_F(VsmStaticDynamicMergeLocalLiveSceneTest,
  StaticInvalidatedSpotLightSceneLeavesDynamicSliceUnchangedAcrossDirtyPages)
{
  constexpr auto kWidth = 1024U;
  constexpr auto kHeight = 1024U;
  constexpr auto kSlot = Slot { 0U };
  constexpr auto kFirstSequence = SequenceNumber { 424U };
  constexpr auto kSecondSequence = SequenceNumber { 425U };
  constexpr auto kThirdSequence = SequenceNumber { 426U };

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
  MarkTallBoxStaticShadowCaster(scene);
  DisableShortBoxShadowCaster(scene);
  AttachSpotLightToTwoBoxScene(scene, camera_eye,
    PrimarySpotTargetForTwoBoxScene(scene), 18.0F, glm::radians(30.0F),
    glm::radians(44.0F));

  auto vsm_renderer = oxygen::renderer::vsm::VsmShadowRenderer(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    oxygen::observer_ptr { &renderer->GetStagingProvider() },
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    oxygen::ShadowQualityTier::kHigh);

  const auto first_frame
    = RunTwoBoxLiveShellFrame(*renderer, scene, vsm_renderer, resolved_view,
      kWidth, kHeight, kFirstSequence, kSlot, 0xD135AULL);
  ASSERT_NE(first_frame.extracted_frame, nullptr);
  ASSERT_TRUE(first_frame.virtual_frame.directional_layouts.empty());
  ASSERT_EQ(first_frame.virtual_frame.local_light_layouts.size(), 1U);

  const auto second_frame
    = RunTwoBoxLiveShellFrame(*renderer, scene, vsm_renderer, resolved_view,
      kWidth, kHeight, kSecondSequence, kSlot, 0xD135AULL);
  ASSERT_NE(second_frame.extracted_frame, nullptr);
  ASSERT_TRUE(second_frame.virtual_frame.directional_layouts.empty());
  ASSERT_EQ(second_frame.virtual_frame.local_light_layouts.size(), 1U);
  ASSERT_FALSE(
    second_frame.extracted_frame->static_primitive_page_feedback.empty());
  ASSERT_FALSE(
    second_frame.extracted_frame->rendered_primitive_history.empty());

  const auto previous_tall_bounds = scene.draw_bounds[1];
  const auto local_key
    = second_frame.virtual_frame.local_light_layouts.front().remap_key;
  const auto previous_tall_history = std::find_if(
    second_frame.extracted_frame->rendered_primitive_history.begin(),
    second_frame.extracted_frame->rendered_primitive_history.end(),
    [&](const auto& record) {
      return record.primitive.transform_index
        == scene.rendered_items[1].transform_handle.get();
    });
  ASSERT_NE(previous_tall_history,
    second_frame.extracted_frame->rendered_primitive_history.end());

  EnableShortBoxShadowCaster(scene);
  MoveShortBox(scene, glm::vec3 { 1.20F, 0.0F, -0.10F });
  vsm_renderer.GetCacheManager().InvalidateLocalLights({ local_key },
    VsmCacheInvalidationScope::kDynamicOnly,
    VsmCacheInvalidationReason::kTargetedInvalidate);

  const auto manual_invalidations = std::array {
    oxygen::renderer::vsm::VsmPrimitiveInvalidationRecord {
      .primitive = previous_tall_history->primitive,
      .world_bounding_sphere = previous_tall_bounds,
      .scope = VsmCacheInvalidationScope::kStaticOnly,
      .is_removed = false,
    },
  };

  const auto result = RunTwoBoxStaticDynamicMergeStage(*renderer, scene,
    vsm_renderer, resolved_view, kWidth, kHeight, kThirdSequence, kSlot,
    0xD135BULL, manual_invalidations);
  const auto& committed_frame = result.rasterization.initialization.propagation
                                  .mapping.bridge.committed_frame;
  const auto& primitive_invalidations
    = result.rasterization.initialization.propagation.mapping.bridge
        .invalidation_inputs.primitive_invalidations;
  const auto& invalidation_work_items
    = result.rasterization.initialization.propagation.mapping.bridge
        .invalidation_work_items;

  EXPECT_EQ(
    result.page_table_after, result.rasterization.initialization.page_table);
  EXPECT_EQ(
    result.page_flags_after, result.rasterization.initialization.page_flags);
  EXPECT_EQ(
    result.physical_metadata_after, result.rasterization.physical_metadata);
  EXPECT_EQ(result.static_after.values, result.static_before.values);
  ASSERT_FALSE(result.rasterization.prepared_pages.empty())
    << "generation=" << committed_frame.snapshot.frame_generation
    << " invalidations=" << primitive_invalidations.size()
    << " invalidation_work_items=" << invalidation_work_items.size()
    << " reused=" << committed_frame.plan.reused_page_count
    << " allocated=" << committed_frame.plan.allocated_page_count
    << " initialized=" << committed_frame.plan.initialized_page_count
    << " dirty_pages="
    << std::count_if(result.rasterization.dirty_flags.begin(),
         result.rasterization.dirty_flags.end(),
         [](const std::uint32_t flags) { return flags != 0U; })
    << " dynamic_invalidated_pages="
    << std::count_if(result.rasterization.physical_metadata.begin(),
         result.rasterization.physical_metadata.end(),
         [](const auto& meta) {
           return static_cast<bool>(meta.dynamic_invalidated);
         })
    << " static_invalidated_pages="
    << std::count_if(result.rasterization.physical_metadata.begin(),
         result.rasterization.physical_metadata.end(), [](const auto& meta) {
           return static_cast<bool>(meta.static_invalidated);
         });
  ASSERT_GE(primitive_invalidations.size(), 1U);
  EXPECT_TRUE(std::any_of(primitive_invalidations.begin(),
    primitive_invalidations.end(), [&](const auto& invalidation) {
      return invalidation.primitive == previous_tall_history->primitive
        && invalidation.scope == VsmCacheInvalidationScope::kStaticOnly;
    }));
  EXPECT_FALSE(result.rasterization.static_page_feedback.empty());

  const auto stats = ExpectNoStaleStaticMergeContract(result);
  EXPECT_GT(stats.eligible_page_count, 0U);
  EXPECT_GT(stats.sampled_texel_count, 0U);
  EXPECT_GT(stats.static_shadow_sample_count, 0U);
  EXPECT_GT(stats.static_dominates_dynamic_sample_count, 0U);
}

} // namespace
