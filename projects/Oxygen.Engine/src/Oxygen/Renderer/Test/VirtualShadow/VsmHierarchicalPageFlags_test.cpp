//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Renderer/VirtualShadowMaps/VsmShaderTypes.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmShadowRenderer.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualAddressSpaceTypes.h>

#include "VirtualShadowLiveSceneHarness.h"

namespace {

using oxygen::frame::SequenceNumber;
using oxygen::frame::Slot;
using oxygen::renderer::vsm::VsmClipmapLayout;
using oxygen::renderer::vsm::VsmShaderPageFlagBits;
using oxygen::renderer::vsm::VsmShaderPageFlags;
using oxygen::renderer::vsm::VsmShaderPageTableEntry;
using oxygen::renderer::vsm::VsmVirtualAddressSpaceFrame;
using oxygen::renderer::vsm::VsmVirtualMapLayout;
using oxygen::renderer::vsm::testing::TwoBoxPageFlagPropagationResult;
using oxygen::renderer::vsm::testing::TwoBoxShadowSceneData;
using oxygen::renderer::vsm::testing::VsmLiveSceneHarness;

constexpr auto kHierarchicalFlagMask
  = static_cast<std::uint32_t>(VsmShaderPageFlagBits::kAllocated)
  | static_cast<std::uint32_t>(VsmShaderPageFlagBits::kDynamicUncached)
  | static_cast<std::uint32_t>(VsmShaderPageFlagBits::kStaticUncached)
  | static_cast<std::uint32_t>(VsmShaderPageFlagBits::kDetailGeometry);

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

template <typename TLayout>
auto ApplyHierarchicalPropagation(std::vector<VsmShaderPageFlags>& expected,
  const TLayout& layout, const std::uint32_t level_count,
  const std::uint32_t pages_x, const std::uint32_t pages_y) -> void
{
  if (level_count <= 1U || pages_x == 0U || pages_y == 0U) {
    return;
  }

  const auto pages_per_level = pages_x * pages_y;
  for (std::uint32_t target_level = 1U; target_level < level_count;
    ++target_level) {
    for (std::uint32_t linear_index = 0U; linear_index < pages_per_level;
      ++linear_index) {
      const auto child_index = layout.first_page_table_entry
        + (target_level - 1U) * pages_per_level + linear_index;
      const auto parent_index = layout.first_page_table_entry
        + target_level * pages_per_level + linear_index;
      ASSERT_LT(child_index, expected.size());
      ASSERT_LT(parent_index, expected.size());
      expected[parent_index].bits
        |= expected[child_index].bits & kHierarchicalFlagMask;
    }
  }
}

[[nodiscard]] auto BuildExpectedHierarchicalFlags(
  const VsmVirtualAddressSpaceFrame& virtual_frame,
  const std::vector<VsmShaderPageFlags>& stage_eight_flags)
  -> std::vector<VsmShaderPageFlags>
{
  auto expected = stage_eight_flags;
  for (const auto& layout : virtual_frame.local_light_layouts) {
    ApplyHierarchicalPropagation(expected, layout, layout.level_count,
      layout.pages_per_level_x, layout.pages_per_level_y);
  }
  for (const auto& layout : virtual_frame.directional_layouts) {
    ApplyHierarchicalPropagation(expected, layout, layout.clip_level_count,
      layout.pages_per_axis, layout.pages_per_axis);
  }
  return expected;
}

[[nodiscard]] auto CountHierarchicalFlagChanges(
  const std::vector<VsmShaderPageFlags>& before,
  const std::vector<VsmShaderPageFlags>& after) -> std::size_t
{
  EXPECT_EQ(before.size(), after.size());
  auto changed = std::size_t { 0U };
  const auto count = std::min(before.size(), after.size());
  for (std::size_t index = 0U; index < count; ++index) {
    if ((before[index].bits & kHierarchicalFlagMask)
      != (after[index].bits & kHierarchicalFlagMask)) {
      ++changed;
    }
  }
  return changed;
}

auto ExpectHierarchicalPropagationMatchesCpuModel(
  const TwoBoxPageFlagPropagationResult& result) -> void
{
  const auto& virtual_frame
    = result.mapping.bridge.prepared_products.virtual_frame;
  const auto expected
    = BuildExpectedHierarchicalFlags(virtual_frame, result.mapping.page_flags);

  ASSERT_EQ(result.mapping.page_table.size(), result.page_table.size());
  EXPECT_EQ(result.page_table, result.mapping.page_table);

  ASSERT_EQ(expected.size(), result.page_flags.size());
  for (std::size_t index = 0U; index < expected.size(); ++index) {
    EXPECT_EQ(result.page_flags[index].bits & kHierarchicalFlagMask,
      expected[index].bits & kHierarchicalFlagMask)
      << "page_table_index=" << index;
  }
}

class VsmHierarchicalPageFlagsLiveSceneTest : public VsmLiveSceneHarness { };

NOLINT_TEST_F(VsmHierarchicalPageFlagsLiveSceneTest,
  DirectionalTwoBoxSceneMatchesCpuHierarchicalPropagationAcrossClipLevels)
{
  constexpr auto kWidth = 256U;
  constexpr auto kHeight = 256U;
  constexpr auto kSlot = Slot { 0U };
  constexpr auto kSequence = SequenceNumber { 111U };

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

  const auto propagated = RunTwoBoxPageFlagPropagationStage(*renderer, scene,
    vsm_renderer, resolved_view, kWidth, kHeight, kSequence, kSlot, 0x9001ULL);
  const auto& virtual_frame
    = propagated.mapping.bridge.prepared_products.virtual_frame;

  ASSERT_EQ(virtual_frame.directional_layouts.size(), 1U);
  EXPECT_TRUE(virtual_frame.local_light_layouts.empty());
  EXPECT_GT(virtual_frame.directional_layouts.front().pages_per_axis, 1U);
  EXPECT_GT(virtual_frame.directional_layouts.front().clip_level_count, 1U);
  EXPECT_GT(
    propagated.mapping.bridge.committed_frame.plan.allocated_page_count, 0U);

  const auto expected = BuildExpectedHierarchicalFlags(
    virtual_frame, propagated.mapping.page_flags);
  EXPECT_GT(
    CountHierarchicalFlagChanges(propagated.mapping.page_flags, expected), 0U);
  ExpectHierarchicalPropagationMatchesCpuModel(propagated);
}

NOLINT_TEST_F(VsmHierarchicalPageFlagsLiveSceneTest,
  AddedSpotLightsMatchCpuHierarchicalPropagationAcrossMixedLocalLayouts)
{
  constexpr auto kWidth = 1024U;
  constexpr auto kHeight = 1024U;
  constexpr auto kSlot = Slot { 0U };
  constexpr auto kProbeSequence = SequenceNumber { 112U };
  constexpr auto kFirstSequence = SequenceNumber { 113U };
  constexpr auto kSecondSequence = SequenceNumber { 114U };

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

  auto probe_renderer = oxygen::renderer::vsm::VsmShadowRenderer(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    oxygen::observer_ptr { &renderer->GetStagingProvider() },
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    oxygen::ShadowQualityTier::kHigh);
  const auto depth_probe = RunTwoBoxPageRequestBridge(*renderer, scene,
    probe_renderer, resolved_view, kWidth, kHeight, kProbeSequence, kSlot,
    0x9002ULL, {}, false);
  ASSERT_NE(depth_probe.scene_depth_texture, nullptr);
  const auto probe_samples
    = ReadDepthTextureSamples(*depth_probe.scene_depth_texture, resolved_view,
      "stage-nine.local-multi-light.target-probe");
  ASSERT_FALSE(probe_samples.empty());

  const auto first_target_it = std::min_element(probe_samples.begin(),
    probe_samples.end(), [&](const auto& lhs, const auto& rhs) {
      const auto lhs_delta = lhs.world_position_ws - camera_target;
      const auto rhs_delta = rhs.world_position_ws - camera_target;
      return glm::dot(lhs_delta, lhs_delta) < glm::dot(rhs_delta, rhs_delta);
    });
  ASSERT_NE(first_target_it, probe_samples.end());
  const auto first_target = first_target_it->world_position_ws;
  const auto second_target_it = std::max_element(probe_samples.begin(),
    probe_samples.end(), [&](const auto& lhs, const auto& rhs) {
      const auto lhs_delta = lhs.world_position_ws - first_target;
      const auto rhs_delta = rhs.world_position_ws - first_target;
      return glm::dot(lhs_delta, lhs_delta) < glm::dot(rhs_delta, rhs_delta);
    });
  ASSERT_NE(second_target_it, probe_samples.end());
  const auto second_target = second_target_it->world_position_ws;

  AttachSpotLightToTwoBoxScene(scene, camera_eye, first_target, 18.0F,
    glm::radians(30.0F), glm::radians(50.0F));
  auto vsm_renderer = oxygen::renderer::vsm::VsmShadowRenderer(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    oxygen::observer_ptr { &renderer->GetStagingProvider() },
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    oxygen::ShadowQualityTier::kHigh);

  const auto first_frame
    = RunTwoBoxPageRequestBridge(*renderer, scene, vsm_renderer, resolved_view,
      kWidth, kHeight, kFirstSequence, kSlot, 0x9003ULL);
  ASSERT_TRUE(
    first_frame.prepared_products.virtual_frame.directional_layouts.empty());
  ASSERT_EQ(
    first_frame.prepared_products.virtual_frame.local_light_layouts.size(), 1U);
  vsm_renderer.GetCacheManager().ExtractFrameData();

  AttachAdditionalSpotLightToTwoBoxScene(scene,
    camera_eye + glm::vec3 { 2.6F, 1.0F, -0.6F }, second_target, 20.0F,
    glm::radians(28.0F), glm::radians(46.0F));

  const auto propagated
    = RunTwoBoxPageFlagPropagationStage(*renderer, scene, vsm_renderer,
      resolved_view, kWidth, kHeight, kSecondSequence, kSlot, 0x9003ULL);
  const auto& virtual_frame
    = propagated.mapping.bridge.prepared_products.virtual_frame;
  const auto& frame = propagated.mapping.bridge.committed_frame;

  ASSERT_TRUE(virtual_frame.directional_layouts.empty());
  ASSERT_EQ(virtual_frame.local_light_layouts.size(), 2U);
  for (const auto& layout : virtual_frame.local_light_layouts) {
    EXPECT_GT(layout.level_count, 1U);
    EXPECT_GT(layout.pages_per_level_x * layout.pages_per_level_y, 1U);
  }
  EXPECT_GT(frame.plan.reused_page_count, 0U);
  EXPECT_GT(frame.plan.allocated_page_count, 0U);

  const auto expected = BuildExpectedHierarchicalFlags(
    virtual_frame, propagated.mapping.page_flags);
  EXPECT_GT(
    CountHierarchicalFlagChanges(propagated.mapping.page_flags, expected), 0U);
  ExpectHierarchicalPropagationMatchesCpuModel(propagated);
}

NOLINT_TEST_F(VsmHierarchicalPageFlagsLiveSceneTest,
  MovedCasterDirectionalSceneMatchesCpuHierarchicalPropagationAfterRefresh)
{
  constexpr auto kWidth = 256U;
  constexpr auto kHeight = 256U;
  constexpr auto kSlot = Slot { 0U };
  constexpr auto kFirstSequence = SequenceNumber { 115U };
  constexpr auto kSecondSequence = SequenceNumber { 116U };

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
      kWidth, kHeight, kFirstSequence, kSlot, 0x9004ULL);
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

  const auto propagated = RunTwoBoxPageFlagPropagationStage(*renderer, scene,
    vsm_renderer, resolved_view, kWidth, kHeight, kSecondSequence, kSlot,
    0x9005ULL, manual_invalidations);
  const auto& virtual_frame
    = propagated.mapping.bridge.prepared_products.virtual_frame;

  EXPECT_FALSE(propagated.mapping.bridge.invalidation_inputs
      .primitive_invalidations.empty());
  ASSERT_NE(propagated.mapping.bridge.metadata_seed_buffer, nullptr);
  ASSERT_EQ(virtual_frame.directional_layouts.size(), 1U);
  EXPECT_GT(virtual_frame.directional_layouts.front().pages_per_axis, 1U);
  EXPECT_GT(
    propagated.mapping.bridge.committed_frame.plan.allocated_page_count, 0U);

  const auto expected = BuildExpectedHierarchicalFlags(
    virtual_frame, propagated.mapping.page_flags);
  EXPECT_GT(
    CountHierarchicalFlagChanges(propagated.mapping.page_flags, expected), 0U);
  ExpectHierarchicalPropagationMatchesCpuModel(propagated);
}

} // namespace
