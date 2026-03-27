//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Renderer/Passes/Vsm/VsmInvalidationPass.h>
#include <Oxygen/Renderer/Passes/Vsm/VsmPageManagementPass.h>
#include <Oxygen/Renderer/PreparedSceneFrame.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmPageAllocationSnapshotHelpers.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmShaderTypes.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmShadowRenderer.h>

#include "VirtualShadowLiveSceneHarness.h"

namespace {

using oxygen::engine::PreparedSceneFrame;
using oxygen::engine::VsmInvalidationPass;
using oxygen::engine::VsmInvalidationPassConfig;
using oxygen::engine::VsmInvalidationPassInput;
using oxygen::engine::VsmPageManagementFinalStage;
using oxygen::engine::VsmPageManagementPass;
using oxygen::engine::VsmPageManagementPassConfig;
using oxygen::frame::SequenceNumber;
using oxygen::frame::Slot;
using oxygen::renderer::vsm::DecodePhysicalPageIndex;
using oxygen::renderer::vsm::IsMapped;
using oxygen::renderer::vsm::VsmAllocationAction;
using oxygen::renderer::vsm::VsmPhysicalPageMeta;
using oxygen::renderer::vsm::VsmShaderPageFlagBits;
using oxygen::renderer::vsm::VsmShaderPageFlags;
using oxygen::renderer::vsm::VsmShaderPageTableEntry;
using oxygen::renderer::vsm::detail::TryResolvePageTableEntryIndex;
using oxygen::renderer::vsm::testing::TwoBoxNewPageMappingResult;
using oxygen::renderer::vsm::testing::TwoBoxShadowSceneData;
using oxygen::renderer::vsm::testing::VsmLiveSceneHarness;

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

struct FreshMappingContractSummary {
  std::size_t allocate_count { 0U };
  std::size_t reuse_count { 0U };
  std::size_t seeded_allocation_count { 0U };
};

auto ExpectFreshMappingsConsumeAvailablePrefix(
  const oxygen::renderer::vsm::VsmPageAllocationFrame& frame,
  const std::span<const VsmShaderPageTableEntry> page_table,
  const std::span<const VsmShaderPageFlags> page_flags,
  const std::span<const VsmPhysicalPageMeta> physical_metadata,
  const std::span<const std::uint32_t> available_pages,
  const std::span<const VsmPhysicalPageMeta> seed_metadata = {})
  -> FreshMappingContractSummary
{
  const auto allocated_bit
    = static_cast<std::uint32_t>(VsmShaderPageFlagBits::kAllocated);
  const auto dynamic_uncached_bit
    = static_cast<std::uint32_t>(VsmShaderPageFlagBits::kDynamicUncached);
  const auto static_uncached_bit
    = static_cast<std::uint32_t>(VsmShaderPageFlagBits::kStaticUncached);

  auto summary = FreshMappingContractSummary {};
  for (const auto& decision : frame.plan.decisions) {
    const auto page_table_index = TryResolvePageTableEntryIndex(
      frame.snapshot.virtual_frame, decision.request);
    EXPECT_TRUE(page_table_index.has_value());
    if (!page_table_index.has_value()) {
      continue;
    }
    EXPECT_LT(*page_table_index, page_table.size());
    if (*page_table_index >= page_table.size()) {
      continue;
    }

    if (decision.action == VsmAllocationAction::kAllocateNew) {
      EXPECT_LT(summary.allocate_count, available_pages.size());
      if (summary.allocate_count >= available_pages.size()) {
        continue;
      }
      const auto expected_physical_page
        = available_pages[summary.allocate_count];
      EXPECT_LT(expected_physical_page, physical_metadata.size());
      if (expected_physical_page >= physical_metadata.size()) {
        continue;
      }

      EXPECT_EQ(expected_physical_page, decision.current_physical_page.value);
      EXPECT_TRUE(IsMapped(page_table[*page_table_index]));
      EXPECT_EQ(DecodePhysicalPageIndex(page_table[*page_table_index]).value,
        expected_physical_page);
      EXPECT_NE(page_flags[*page_table_index].bits & allocated_bit, 0U);
      EXPECT_NE(page_flags[*page_table_index].bits & dynamic_uncached_bit, 0U);
      EXPECT_NE(page_flags[*page_table_index].bits & static_uncached_bit, 0U);

      const auto& final_meta = physical_metadata[expected_physical_page];
      EXPECT_TRUE(static_cast<bool>(final_meta.is_allocated));
      EXPECT_EQ(final_meta.owner_id, decision.request.map_id);
      EXPECT_EQ(final_meta.owner_mip_level, decision.request.page.level);
      EXPECT_EQ(final_meta.owner_page, decision.request.page);
      EXPECT_TRUE(static_cast<bool>(final_meta.view_uncached));

      if (!seed_metadata.empty()) {
        EXPECT_LT(expected_physical_page, seed_metadata.size());
        if (expected_physical_page >= seed_metadata.size()) {
          ++summary.allocate_count;
          continue;
        }
        const auto& snapshot_meta
          = frame.snapshot.physical_pages[expected_physical_page];
        const auto& seed_meta = seed_metadata[expected_physical_page];
        EXPECT_EQ(static_cast<bool>(final_meta.dynamic_invalidated),
          static_cast<bool>(snapshot_meta.dynamic_invalidated)
            || static_cast<bool>(seed_meta.dynamic_invalidated));
        EXPECT_EQ(static_cast<bool>(final_meta.static_invalidated),
          static_cast<bool>(snapshot_meta.static_invalidated)
            || static_cast<bool>(seed_meta.static_invalidated));
        if (static_cast<bool>(seed_meta.dynamic_invalidated)
          || static_cast<bool>(seed_meta.static_invalidated)) {
          ++summary.seeded_allocation_count;
        }
      }

      ++summary.allocate_count;
      continue;
    }

    if (IsReuseAction(decision.action)) {
      EXPECT_TRUE(IsMapped(page_table[*page_table_index]));
      EXPECT_EQ(DecodePhysicalPageIndex(page_table[*page_table_index]).value,
        decision.current_physical_page.value);
      ++summary.reuse_count;
    }
  }

  EXPECT_EQ(summary.allocate_count, frame.plan.allocated_page_count);
  EXPECT_EQ(summary.reuse_count, frame.plan.reused_page_count);
  return summary;
}

class VsmNewPageMappingLiveSceneTest : public VsmLiveSceneHarness { };

NOLINT_TEST_F(VsmNewPageMappingLiveSceneTest,
  MovedCasterDirectionalSceneMapsFreshPagesFromPackedAvailablePrefix)
{
  constexpr auto kWidth = 256U;
  constexpr auto kHeight = 256U;
  constexpr auto kSlot = Slot { 0U };
  constexpr auto kFirstSequence = SequenceNumber { 101U };
  constexpr auto kSecondSequence = SequenceNumber { 102U };

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
      kWidth, kHeight, kFirstSequence, kSlot, 0x8001ULL);
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

  const auto mapped = RunTwoBoxNewPageMappingStage(*renderer, scene,
    vsm_renderer, resolved_view, kWidth, kHeight, kSecondSequence, kSlot,
    0x8002ULL, manual_invalidations);
  const auto& frame = mapped.bridge.committed_frame;
  const auto& layout
    = mapped.bridge.prepared_products.virtual_frame.directional_layouts.front();

  EXPECT_FALSE(
    mapped.bridge.invalidation_inputs.primitive_invalidations.empty());
  ASSERT_NE(mapped.bridge.metadata_seed_buffer, nullptr);
  EXPECT_GT(layout.pages_per_axis, 1U);
  EXPECT_GT(frame.plan.allocated_page_count, 0U);
  EXPECT_GE(mapped.available_page_count, frame.plan.allocated_page_count);

  const auto summary = ExpectFreshMappingsConsumeAvailablePrefix(frame,
    mapped.page_table, mapped.page_flags, mapped.physical_metadata,
    mapped.available_pages, mapped.seed_metadata);
  EXPECT_GT(summary.allocate_count, 0U);
}

NOLINT_TEST_F(VsmNewPageMappingLiveSceneTest,
  AddedSpotLightScenePublishesMixedReuseAndFreshMappingsAcrossLocalLights)
{
  constexpr auto kWidth = 1024U;
  constexpr auto kHeight = 1024U;
  constexpr auto kSlot = Slot { 0U };
  constexpr auto kFirstSequence = SequenceNumber { 104U };
  constexpr auto kSecondSequence = SequenceNumber { 105U };

  const auto camera_eye = glm::vec3 { -3.2F, 3.4F, 5.8F };
  const auto camera_target = glm::vec3 { 0.2F, 0.8F, 0.0F };
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
  const auto first_target = PrimarySpotTargetForTwoBoxScene(scene);
  const auto second_target = SecondarySpotTargetForTwoBoxScene(scene);

  AttachSpotLightToTwoBoxScene(scene, camera_eye, first_target, 18.0F,
    glm::radians(30.0F), glm::radians(50.0F));
  auto vsm_renderer = oxygen::renderer::vsm::VsmShadowRenderer(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    oxygen::observer_ptr { &renderer->GetStagingProvider() },
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    oxygen::ShadowQualityTier::kHigh);

  const auto first_frame
    = PrimeTwoBoxExtractedFrame(*renderer, scene, vsm_renderer, resolved_view,
      kWidth, kHeight, kFirstSequence, kSlot, 0x8004ULL);
  ASSERT_EQ(first_frame.virtual_frame.directional_layouts.size(), 0U);
  ASSERT_EQ(first_frame.virtual_frame.local_light_layouts.size(), 1U);

  AttachAdditionalSpotLightToTwoBoxScene(scene,
    camera_eye + glm::vec3 { 2.6F, 1.0F, -0.6F }, second_target, 20.0F,
    glm::radians(28.0F), glm::radians(46.0F));

  const auto mapped
    = RunTwoBoxNewPageMappingStage(*renderer, scene, vsm_renderer,
      resolved_view, kWidth, kHeight, kSecondSequence, kSlot, 0x8004ULL);
  const auto& frame = mapped.bridge.committed_frame;
  const auto& current_frame = mapped.bridge.prepared_products.virtual_frame;

  ASSERT_TRUE(current_frame.directional_layouts.empty());
  ASSERT_EQ(current_frame.local_light_layouts.size(), 2U);
  EXPECT_GT(frame.plan.reused_page_count, 0U);
  EXPECT_GT(frame.plan.allocated_page_count, 0U);
  EXPECT_GE(mapped.available_page_count, frame.plan.allocated_page_count);

  const auto summary = ExpectFreshMappingsConsumeAvailablePrefix(frame,
    mapped.page_table, mapped.page_flags, mapped.physical_metadata,
    mapped.available_pages, mapped.seed_metadata);
  EXPECT_GT(summary.allocate_count, 0U);
  EXPECT_GT(summary.reuse_count, 0U);

  const auto second_light_id = current_frame.local_light_layouts.back().id;
  EXPECT_TRUE(std::any_of(frame.plan.decisions.begin(),
    frame.plan.decisions.end(), [&](const auto& decision) {
      return decision.action == VsmAllocationAction::kAllocateNew
        && decision.request.map_id == second_light_id;
    }));
}

NOLINT_TEST_F(VsmNewPageMappingLiveSceneTest,
  SharedRecorderInvalidationSeedRemainsCompatibleWithFreshMappingPass)
{
  constexpr auto kWidth = 256U;
  constexpr auto kHeight = 256U;
  constexpr auto kSlot = Slot { 0U };
  constexpr auto kFirstSequence = SequenceNumber { 106U };
  constexpr auto kSecondSequence = SequenceNumber { 107U };

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
      kWidth, kHeight, kFirstSequence, kSlot, 0x8005ULL);
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

  const auto bridge
    = RunTwoBoxPageRequestBridge(*renderer, scene, vsm_renderer, resolved_view,
      kWidth, kHeight, kSecondSequence, kSlot, 0x8006ULL, manual_invalidations);
  const auto* previous_frame
    = vsm_renderer.GetCacheManager().GetPreviousFrame();
  ASSERT_NE(previous_frame, nullptr);
  ASSERT_FALSE(bridge.invalidation_work_items.empty());

  auto invalidation_pass
    = VsmInvalidationPass(oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      std::make_shared<VsmInvalidationPassConfig>(VsmInvalidationPassConfig {
        .debug_name = "stage-eight.shared-recorder.invalidation",
      }));
  invalidation_pass.SetInput(VsmInvalidationPassInput {
    .previous_projection_records = previous_frame->projection_records,
    .previous_page_table_entries
    = BuildShaderPageTableEntries(previous_frame->page_table),
    .previous_physical_page_metadata = previous_frame->physical_pages,
    .invalidation_work_items = bridge.invalidation_work_items,
  });

  auto page_management_pass = VsmPageManagementPass(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    std::make_shared<VsmPageManagementPassConfig>(VsmPageManagementPassConfig {
      .final_stage = VsmPageManagementFinalStage::kAllocateNewPages,
      .debug_name = "stage-eight.shared-recorder.allocate",
    }));

  auto offscreen = renderer->BeginOffscreenFrame(
    { .frame_slot = kSlot, .frame_sequence = SequenceNumber { 1U } });
  auto prepared_frame = PreparedSceneFrame {};
  offscreen.SetCurrentView(kTestViewId, resolved_view, prepared_frame);
  auto& render_context = offscreen.GetRenderContext();

  auto frame_with_seed = bridge.committed_frame;
  {
    auto recorder = AcquireRecorder("stage-eight.shared-recorder");
    ASSERT_NE(recorder.get(), nullptr);
    RunPass(invalidation_pass, render_context, *recorder);
    frame_with_seed.physical_page_meta_seed_buffer
      = invalidation_pass.GetCurrentOutputPhysicalMetadataBuffer();
    page_management_pass.SetFrameInput(frame_with_seed);
    RunPass(page_management_pass, render_context, *recorder);
  }
  WaitForQueueIdle();

  ASSERT_NE(frame_with_seed.physical_page_meta_seed_buffer, nullptr);
  const auto available_count_buffer
    = page_management_pass.GetAvailablePageCountBuffer();
  ASSERT_NE(available_count_buffer, nullptr);
  const auto available_count = ReadBufferAs<std::uint32_t>(
    available_count_buffer, 1U, "stage-eight.shared-recorder.available-count");
  ASSERT_EQ(available_count.size(), 1U);

  auto available_pages = std::vector<std::uint32_t> {};
  if (available_count[0] > 0U) {
    available_pages
      = ReadBufferAs<std::uint32_t>(frame_with_seed.physical_page_list_buffer,
        available_count[0], "stage-eight.shared-recorder.available-pages");
  }

  const auto page_table
    = ReadBufferAs<VsmShaderPageTableEntry>(frame_with_seed.page_table_buffer,
      frame_with_seed.snapshot.page_table.size(),
      "stage-eight.shared-recorder.page-table");
  const auto page_flags
    = ReadBufferAs<VsmShaderPageFlags>(frame_with_seed.page_flags_buffer,
      frame_with_seed.snapshot.page_table.size(),
      "stage-eight.shared-recorder.page-flags");
  const auto physical_metadata = ReadBufferAs<VsmPhysicalPageMeta>(
    frame_with_seed.physical_page_meta_buffer,
    frame_with_seed.snapshot.physical_pages.size(),
    "stage-eight.shared-recorder.physical-meta");
  const auto seed_metadata = ReadBufferAs<VsmPhysicalPageMeta>(
    frame_with_seed.physical_page_meta_seed_buffer,
    frame_with_seed.snapshot.physical_pages.size(),
    "stage-eight.shared-recorder.seed-meta");

  const auto summary
    = ExpectFreshMappingsConsumeAvailablePrefix(frame_with_seed, page_table,
      page_flags, physical_metadata, available_pages, seed_metadata);
  EXPECT_GT(summary.allocate_count, 0U);
}

} // namespace
