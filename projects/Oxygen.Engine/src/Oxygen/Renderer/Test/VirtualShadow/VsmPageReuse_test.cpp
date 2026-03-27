//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>

#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Renderer/VirtualShadowMaps/VsmProjectionRouting.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmShaderTypes.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmShadowRenderer.h>

#include "VirtualShadowLiveSceneHarness.h"

namespace {

using oxygen::frame::SequenceNumber;
using oxygen::frame::Slot;
using oxygen::renderer::vsm::BuildPageRequests;
using oxygen::renderer::vsm::DecodePhysicalPageIndex;
using oxygen::renderer::vsm::IsMapped;
using oxygen::renderer::vsm::VsmAllocationAction;
using oxygen::renderer::vsm::VsmShaderPageFlagBits;
using oxygen::renderer::vsm::testing::TwoBoxShadowSceneData;
using oxygen::renderer::vsm::testing::VsmLiveSceneHarness;

[[nodiscard]] auto HasFlag(
  const oxygen::renderer::vsm::VsmShaderPageFlags flags,
  const VsmShaderPageFlagBits bit) -> bool
{
  return (flags.bits & static_cast<std::uint32_t>(bit)) != 0U;
}

[[nodiscard]] auto IsReuseAction(const VsmAllocationAction action) -> bool
{
  return action == VsmAllocationAction::kReuseExisting
    || action == VsmAllocationAction::kInitializeOnly;
}

auto MoveTallBox(
  TwoBoxShadowSceneData& scene_data, const glm::vec3& translation) -> void
{
  scene_data.world_matrices[1] = glm::translate(glm::mat4 { 1.0F }, translation)
    * glm::scale(glm::mat4 { 1.0F }, glm::vec3 { 0.8F, 3.2F, 0.8F });
  scene_data.draw_bounds[1]
    = glm::vec4 { translation.x, 1.6F, translation.z, 1.75F };
  scene_data.shadow_caster_bounds[0] = scene_data.draw_bounds[1];
  scene_data.rendered_items[1].world_bounding_sphere
    = scene_data.draw_bounds[1];

  ASSERT_TRUE(
    scene_data.tall_box_node.GetTransform().SetLocalPosition(translation));
  auto impl = scene_data.tall_box_node.GetImpl();
  ASSERT_TRUE(impl.has_value());
  impl->get().UpdateTransforms(*scene_data.scene);
  scene_data.scene->Update();
}

class VsmPageReuseLiveSceneTest : public VsmLiveSceneHarness { };

NOLINT_TEST_F(VsmPageReuseLiveSceneTest,
  StableDirectionalSceneReusesMultiPageRequestsWithoutFreshAllocation)
{
  constexpr auto kWidth = 256U;
  constexpr auto kHeight = 256U;
  constexpr auto kSlot = Slot { 0U };
  constexpr auto kFirstSequence = SequenceNumber { 81U };
  constexpr auto kSecondSequence = SequenceNumber { 82U };

  const auto camera_eye = glm::vec3 { -3.2F, 3.4F, 5.8F };
  const auto camera_target = glm::vec3 { 0.2F, 0.8F, 0.0F };
  const auto sun_direction
    = glm::normalize(glm::vec3 { -0.40558F, 0.40558F, 0.819152F });
  const auto resolved_view
    = MakeLookAtResolvedView(camera_eye, camera_target, kWidth, kHeight);

  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);
  auto scene = CreateTwoBoxShadowScene(sun_direction, 4U);
  auto vsm_renderer = oxygen::renderer::vsm::VsmShadowRenderer(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    oxygen::observer_ptr { &renderer->GetStagingProvider() },
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    oxygen::ShadowQualityTier::kHigh);

  const auto first_frame
    = RunTwoBoxLiveShellFrame(*renderer, scene, vsm_renderer, resolved_view,
      kWidth, kHeight, kFirstSequence, kSlot, 0x6001ULL);
  ASSERT_NE(first_frame.extracted_frame, nullptr);
  ASSERT_EQ(first_frame.virtual_frame.directional_layouts.size(), 1U);

  const auto reuse = RunTwoBoxReuseStage(*renderer, scene, vsm_renderer,
    resolved_view, kWidth, kHeight, kSecondSequence, kSlot, 0x6001ULL);
  const auto& frame = reuse.bridge.committed_frame;
  const auto& layout
    = reuse.bridge.prepared_products.virtual_frame.directional_layouts.front();

  EXPECT_TRUE(reuse.bridge.bridge_committed_requests);
  EXPECT_TRUE(reuse.bridge.invalidation_inputs.Empty());
  EXPECT_TRUE(frame.snapshot.retained_local_light_layouts.empty());
  EXPECT_TRUE(frame.snapshot.retained_directional_layouts.empty());
  EXPECT_GT(layout.pages_per_axis, 1U);
  EXPECT_GT(frame.plan.reused_page_count, 0U);
  EXPECT_EQ(frame.plan.allocated_page_count, 0U);
  EXPECT_EQ(reuse.available_page_count, 0U);

  auto checked_reuse_count = std::size_t { 0U };
  for (const auto& decision : frame.plan.decisions) {
    ASSERT_TRUE(IsReuseAction(decision.action))
      << static_cast<std::uint32_t>(decision.action);

    const auto page_table_index
      = ResolvePageTableEntryIndex(layout, decision.request.page);
    ASSERT_LT(page_table_index, reuse.page_table.size());
    EXPECT_TRUE(IsMapped(reuse.page_table[page_table_index]));
    EXPECT_EQ(DecodePhysicalPageIndex(reuse.page_table[page_table_index]).value,
      decision.current_physical_page.value);
    EXPECT_TRUE(HasFlag(
      reuse.page_flags[page_table_index], VsmShaderPageFlagBits::kAllocated));
    if (decision.action == VsmAllocationAction::kInitializeOnly) {
      EXPECT_TRUE(HasFlag(reuse.page_flags[page_table_index],
        VsmShaderPageFlagBits::kDynamicUncached));
    } else {
      EXPECT_FALSE(HasFlag(reuse.page_flags[page_table_index],
        VsmShaderPageFlagBits::kDynamicUncached));
      EXPECT_FALSE(HasFlag(reuse.page_flags[page_table_index],
        VsmShaderPageFlagBits::kStaticUncached));
    }

    const auto& metadata
      = reuse.physical_metadata[decision.current_physical_page.value];
    EXPECT_TRUE(static_cast<bool>(metadata.is_allocated));
    EXPECT_TRUE(static_cast<bool>(metadata.used_this_frame));
    EXPECT_EQ(static_cast<bool>(metadata.view_uncached),
      decision.action == VsmAllocationAction::kInitializeOnly);
    EXPECT_EQ(metadata.owner_id, decision.request.map_id);
    EXPECT_EQ(metadata.owner_mip_level, decision.request.page.level);
    EXPECT_EQ(metadata.owner_page, decision.request.page);
    EXPECT_EQ(metadata.last_touched_frame, kSecondSequence.get());
    ++checked_reuse_count;
  }
  EXPECT_GT(checked_reuse_count, 4U);
}

NOLINT_TEST_F(VsmPageReuseLiveSceneTest,
  DirectionalClipmapPanReusesTranslatedPreviousPhysicalPages)
{
  constexpr auto kWidth = 256U;
  constexpr auto kHeight = 256U;
  constexpr auto kSlot = Slot { 0U };
  constexpr auto kFirstSequence = SequenceNumber { 83U };
  constexpr auto kSecondSequence = SequenceNumber { 84U };

  const auto camera_eye = glm::vec3 { -3.2F, 3.4F, 5.8F };
  const auto camera_target = glm::vec3 { 0.2F, 0.8F, 0.0F };
  const auto sun_direction
    = glm::normalize(glm::vec3 { -0.40558F, 0.40558F, 0.819152F });
  const auto first_view
    = MakeLookAtResolvedView(camera_eye, camera_target, kWidth, kHeight);

  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);
  auto scene = CreateTwoBoxShadowScene(sun_direction, 4U);
  auto vsm_renderer = oxygen::renderer::vsm::VsmShadowRenderer(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    oxygen::observer_ptr { &renderer->GetStagingProvider() },
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    oxygen::ShadowQualityTier::kHigh);

  const auto first_frame
    = RunTwoBoxLiveShellFrame(*renderer, scene, vsm_renderer, first_view,
      kWidth, kHeight, kFirstSequence, kSlot, 0x6002ULL);
  ASSERT_NE(first_frame.extracted_frame, nullptr);
  ASSERT_EQ(first_frame.virtual_frame.directional_layouts.size(), 1U);
  const auto& first_layout
    = first_frame.virtual_frame.directional_layouts.front();

  const auto first_projection
    = std::find_if(first_frame.extracted_frame->projection_records.begin(),
      first_frame.extracted_frame->projection_records.end(),
      [](const auto& record) {
        return record.projection.light_type
          == static_cast<std::uint32_t>(
            oxygen::renderer::vsm::VsmProjectionLightType::kDirectional);
      });
  ASSERT_NE(
    first_projection, first_frame.extracted_frame->projection_records.end());

  const auto light_space_page_shift_ws
    = glm::vec3(glm::inverse(first_projection->projection.view_matrix)
      * glm::vec4 { first_layout.page_world_size[0] * 2.5F, 0.0F, 0.0F, 0.0F });
  const auto second_view
    = MakeLookAtResolvedView(camera_eye + light_space_page_shift_ws,
      camera_target + light_space_page_shift_ws, kWidth, kHeight);

  const auto reuse = RunTwoBoxReuseStage(*renderer, scene, vsm_renderer,
    second_view, kWidth, kHeight, kSecondSequence, kSlot, 0x6003ULL);
  const auto& frame = reuse.bridge.committed_frame;
  const auto& remap
    = reuse.bridge.prepared_products.seam.previous_to_current_remap;
  const auto& second_layout
    = reuse.bridge.prepared_products.virtual_frame.directional_layouts.front();

  EXPECT_GT(frame.plan.reused_page_count, 0U);
  ASSERT_FALSE(remap.entries.empty());
  EXPECT_NE(
    second_layout.page_grid_origin[0], first_layout.page_grid_origin[0]);

  const auto translated_reuse = std::find_if(frame.plan.decisions.begin(),
    frame.plan.decisions.end(), [&](const auto& decision) {
      if (!IsReuseAction(decision.action)) {
        return false;
      }
      const auto level = decision.request.page.level;
      return level < remap.entries.size()
        && remap.entries[level].page_offset != glm::ivec2 { 0, 0 };
    });
  ASSERT_NE(translated_reuse, frame.plan.decisions.end());

  const auto level = translated_reuse->request.page.level;
  const auto& remap_entry = remap.entries[level];
  const auto& previous_meta
    = first_frame.extracted_frame
        ->physical_pages[translated_reuse->current_physical_page.value];
  EXPECT_EQ(translated_reuse->request.map_id, second_layout.first_id + level);
  EXPECT_EQ(static_cast<std::int64_t>(translated_reuse->request.page.page_x),
    static_cast<std::int64_t>(previous_meta.owner_page.page_x)
      - remap_entry.page_offset.x);
  EXPECT_EQ(static_cast<std::int64_t>(translated_reuse->request.page.page_y),
    static_cast<std::int64_t>(previous_meta.owner_page.page_y)
      - remap_entry.page_offset.y);

  const auto page_table_index
    = ResolvePageTableEntryIndex(second_layout, translated_reuse->request.page);
  EXPECT_TRUE(IsMapped(reuse.page_table[page_table_index]));
  EXPECT_EQ(DecodePhysicalPageIndex(reuse.page_table[page_table_index]).value,
    translated_reuse->current_physical_page.value);
  EXPECT_TRUE(HasFlag(
    reuse.page_flags[page_table_index], VsmShaderPageFlagBits::kAllocated));
}

NOLINT_TEST_F(VsmPageReuseLiveSceneTest,
  MovedCasterBuildsRealInvalidationSeedAndLeavesRefreshPagesUnmapped)
{
  constexpr auto kWidth = 256U;
  constexpr auto kHeight = 256U;
  constexpr auto kSlot = Slot { 0U };
  constexpr auto kFirstSequence = SequenceNumber { 85U };
  constexpr auto kSecondSequence = SequenceNumber { 86U };

  const auto camera_eye = glm::vec3 { -3.2F, 3.4F, 5.8F };
  const auto camera_target = glm::vec3 { 0.2F, 0.8F, 0.0F };
  const auto sun_direction
    = glm::normalize(glm::vec3 { -0.40558F, 0.40558F, 0.819152F });
  const auto resolved_view
    = MakeLookAtResolvedView(camera_eye, camera_target, kWidth, kHeight);

  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);
  auto scene = CreateTwoBoxShadowScene(sun_direction, 4U);
  auto vsm_renderer = oxygen::renderer::vsm::VsmShadowRenderer(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    oxygen::observer_ptr { &renderer->GetStagingProvider() },
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    oxygen::ShadowQualityTier::kHigh);

  const auto first_frame
    = RunTwoBoxLiveShellFrame(*renderer, scene, vsm_renderer, resolved_view,
      kWidth, kHeight, kFirstSequence, kSlot, 0x6004ULL);
  ASSERT_NE(first_frame.extracted_frame, nullptr);

  MoveTallBox(scene, glm::vec3 { 2.2F, 0.0F, -1.1F });

  const auto moved_history = std::find_if(
    first_frame.extracted_frame->rendered_primitive_history.begin(),
    first_frame.extracted_frame->rendered_primitive_history.end(),
    [&](const auto& record) {
      return record.primitive.transform_index
        == scene.rendered_items[1].transform_handle.get();
    });
  ASSERT_NE(moved_history,
    first_frame.extracted_frame->rendered_primitive_history.end());
  const auto manual_invalidations = std::array {
    oxygen::renderer::vsm::VsmPrimitiveInvalidationRecord {
      .primitive = moved_history->primitive,
      .world_bounding_sphere = scene.draw_bounds[1],
      .scope = oxygen::renderer::vsm::VsmCacheInvalidationScope::kDynamicOnly,
      .is_removed = false,
    },
  };

  const auto reuse
    = RunTwoBoxReuseStage(*renderer, scene, vsm_renderer, resolved_view, kWidth,
      kHeight, kSecondSequence, kSlot, 0x6005ULL, manual_invalidations);
  const auto& frame = reuse.bridge.committed_frame;
  const auto& layout
    = reuse.bridge.prepared_products.virtual_frame.directional_layouts.front();

  EXPECT_FALSE(
    reuse.bridge.invalidation_inputs.primitive_invalidations.empty());
  ASSERT_NE(reuse.bridge.metadata_seed_buffer, nullptr);
  EXPECT_GT(frame.plan.allocated_page_count, 0U);

  const auto allocate_it = std::find_if(frame.plan.decisions.begin(),
    frame.plan.decisions.end(), [](const auto& decision) {
      return decision.action == VsmAllocationAction::kAllocateNew;
    });
  ASSERT_NE(allocate_it, frame.plan.decisions.end());

  const auto allocate_index
    = ResolvePageTableEntryIndex(layout, allocate_it->request.page);
  EXPECT_FALSE(IsMapped(reuse.page_table[allocate_index]));
  EXPECT_EQ(reuse.page_flags[allocate_index].bits, 0U);
}

NOLINT_TEST_F(VsmPageReuseLiveSceneTest,
  RetainedPagedSpotLightRemainsMappedAcrossReuseStageWhenLightBecomesUnreferenced)
{
  constexpr auto kWidth = 1024U;
  constexpr auto kHeight = 1024U;
  constexpr auto kSlot = Slot { 0U };
  constexpr auto kFirstSequence = SequenceNumber { 87U };
  constexpr auto kSecondSequence = SequenceNumber { 88U };

  const auto camera_eye = glm::vec3 { -3.2F, 3.4F, 5.8F };
  const auto camera_target = glm::vec3 { 0.2F, 0.8F, 0.0F };
  const auto spot_position = camera_eye;
  const auto sun_direction
    = glm::normalize(glm::vec3 { -0.40558F, 0.40558F, 0.819152F });
  const auto resolved_view
    = MakeLookAtResolvedView(camera_eye, camera_target, kWidth, kHeight);

  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);
  auto scene = CreateTwoBoxShadowScene(sun_direction, 4U);
  auto sun_impl = scene.sun_node.GetImpl();
  ASSERT_TRUE(sun_impl.has_value());
  auto& sun_light
    = sun_impl->get().GetComponent<oxygen::scene::DirectionalLight>();
  sun_light.Common().casts_shadows = false;
  UpdateTransforms(*scene.scene, scene.sun_node);
  const auto spot_target = PrimarySpotTargetForTwoBoxScene(scene);
  AttachSpotLightToTwoBoxScene(scene, spot_position, spot_target, 18.0F,
    glm::radians(30.0F), glm::radians(50.0F));
  auto vsm_renderer = oxygen::renderer::vsm::VsmShadowRenderer(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    oxygen::observer_ptr { &renderer->GetStagingProvider() },
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    oxygen::ShadowQualityTier::kHigh,
    oxygen::renderer::vsm::VsmCacheManagerConfig {
      .retained_unreferenced_frame_count = 1U,
      .debug_name = "VsmPageReuse.RetentionRenderer",
    });

  const auto first_frame
    = PrimeTwoBoxExtractedFrame(*renderer, scene, vsm_renderer, resolved_view,
      kWidth, kHeight, kFirstSequence, kSlot, 0x6006ULL);
  ASSERT_EQ(first_frame.virtual_frame.directional_layouts.size(), 0U);
  ASSERT_EQ(first_frame.virtual_frame.local_light_layouts.size(), 1U);
  ASSERT_NE(first_frame.prepared_view, nullptr);
  ASSERT_EQ(first_frame.prepared_view->positional_lights.size(), 1U);
  const auto expected_spot_direction
    = glm::normalize(spot_target - spot_position);
  const auto actual_spot_direction = glm::normalize(glm::vec3 {
    first_frame.prepared_view->positional_lights.front().direction_ws });
  EXPECT_GT(glm::dot(actual_spot_direction, expected_spot_direction), 0.999F);
  ASSERT_NE(first_frame.extracted_frame, nullptr);
  const auto& first_local_layout
    = first_frame.virtual_frame.local_light_layouts.front();
  EXPECT_GT(first_local_layout.total_page_count, 1U);

  const auto local_mapped_count
    = std::count_if(first_frame.extracted_frame->page_table.begin()
        + first_local_layout.first_page_table_entry,
      first_frame.extracted_frame->page_table.begin()
        + first_local_layout.first_page_table_entry
        + first_local_layout.total_page_count,
      [](const auto& entry) { return entry.is_mapped; });
  ASSERT_GT(local_mapped_count, 0);
  ASSERT_EQ(
    first_frame.extracted_frame->retained_local_light_layouts.size(), 0U);
  ASSERT_EQ(
    first_frame.extracted_frame->virtual_frame.local_light_layouts.size(), 1U);

  scene.spot_node = {};

  const auto reuse
    = RunTwoBoxReuseStage(*renderer, scene, vsm_renderer, resolved_view, kWidth,
      kHeight, kSecondSequence, kSlot, 0x6007ULL, {}, false);
  const auto& frame = reuse.bridge.committed_frame;

  EXPECT_TRUE(
    reuse.bridge.prepared_products.virtual_frame.local_light_layouts.empty());
  ASSERT_EQ(frame.snapshot.retained_local_light_layouts.size(), 1U);

  const auto& retained_layout
    = frame.snapshot.retained_local_light_layouts.front();
  ASSERT_GT(retained_layout.total_page_count, 1U);
  ASSERT_LE(
    retained_layout.first_page_table_entry + retained_layout.total_page_count,
    frame.snapshot.page_table.size());

  auto retained_mapped_count = std::size_t { 0U };
  for (std::uint32_t i = 0U; i < retained_layout.total_page_count; ++i) {
    const auto page_table_index = retained_layout.first_page_table_entry + i;
    const auto& snapshot_entry = frame.snapshot.page_table[page_table_index];
    if (!snapshot_entry.is_mapped) {
      EXPECT_FALSE(IsMapped(reuse.page_table[page_table_index]));
      continue;
    }

    ++retained_mapped_count;
    EXPECT_TRUE(IsMapped(reuse.page_table[page_table_index]));
    EXPECT_EQ(DecodePhysicalPageIndex(reuse.page_table[page_table_index]).value,
      snapshot_entry.physical_page.value);
    EXPECT_TRUE(HasFlag(
      reuse.page_flags[page_table_index], VsmShaderPageFlagBits::kAllocated));
    EXPECT_EQ(
      reuse.physical_metadata[snapshot_entry.physical_page.value].owner_id,
      retained_layout.id);
  }

  EXPECT_GT(retained_mapped_count, 0U);
}

} // namespace
