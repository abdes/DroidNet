//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
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
using oxygen::renderer::vsm::IsMapped;
using oxygen::renderer::vsm::VsmShaderPageFlagBits;
using oxygen::renderer::vsm::VsmShaderPageFlags;
using oxygen::renderer::vsm::VsmShaderPageTableEntry;
using oxygen::renderer::vsm::VsmVirtualAddressSpaceFrame;
using oxygen::renderer::vsm::testing::TwoBoxPageFlagPropagationResult;
using oxygen::renderer::vsm::testing::TwoBoxShadowSceneData;
using oxygen::renderer::vsm::testing::VsmLiveSceneHarness;

constexpr auto kHierarchicalFlagMask
  = static_cast<std::uint32_t>(VsmShaderPageFlagBits::kAllocated)
  | static_cast<std::uint32_t>(VsmShaderPageFlagBits::kDynamicUncached)
  | static_cast<std::uint32_t>(VsmShaderPageFlagBits::kStaticUncached)
  | static_cast<std::uint32_t>(VsmShaderPageFlagBits::kDetailGeometry);

constexpr auto kMappedDescendantBit
  = static_cast<std::uint32_t>(VsmShaderPageFlagBits::kMappedDescendant);

constexpr auto kPropagationMask = kHierarchicalFlagMask | kMappedDescendantBit;

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

template <typename TLayout>
auto ApplyMappedMipPropagation(std::vector<VsmShaderPageFlags>& expected,
  const std::vector<VsmShaderPageTableEntry>& page_table, const TLayout& layout,
  const std::uint32_t level_count, const std::uint32_t pages_x,
  const std::uint32_t pages_y) -> void
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
      ASSERT_LT(child_index, page_table.size());
      const auto child_mapped = IsMapped(page_table[child_index])
        || (expected[child_index].bits & kMappedDescendantBit) != 0U;
      if (child_mapped) {
        expected[parent_index].bits |= kMappedDescendantBit;
      }
    }
  }
}

[[nodiscard]] auto BuildExpectedPropagationFlags(
  const VsmVirtualAddressSpaceFrame& virtual_frame,
  const std::vector<VsmShaderPageTableEntry>& page_table,
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

  for (const auto& layout : virtual_frame.local_light_layouts) {
    ApplyMappedMipPropagation(expected, page_table, layout, layout.level_count,
      layout.pages_per_level_x, layout.pages_per_level_y);
  }
  for (const auto& layout : virtual_frame.directional_layouts) {
    ApplyMappedMipPropagation(expected, page_table, layout,
      layout.clip_level_count, layout.pages_per_axis, layout.pages_per_axis);
  }
  return expected;
}

[[nodiscard]] auto CountMappedDescendantChanges(
  const std::vector<VsmShaderPageFlags>& before,
  const std::vector<VsmShaderPageFlags>& after) -> std::size_t
{
  EXPECT_EQ(before.size(), after.size());
  auto changed = std::size_t { 0U };
  const auto count = std::min(before.size(), after.size());
  for (std::size_t index = 0U; index < count; ++index) {
    if ((before[index].bits & kMappedDescendantBit)
      != (after[index].bits & kMappedDescendantBit)) {
      ++changed;
    }
  }
  return changed;
}

template <typename TLayout>
[[nodiscard]] auto FindMappedAncestorPairIndex(
  const std::vector<VsmShaderPageTableEntry>& page_table, const TLayout& layout,
  const std::uint32_t level_count, const std::uint32_t pages_x,
  const std::uint32_t pages_y) -> std::optional<std::uint32_t>
{
  if (level_count <= 1U || pages_x == 0U || pages_y == 0U) {
    return std::nullopt;
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
      if (child_index >= page_table.size()
        || parent_index >= page_table.size()) {
        continue;
      }
      if (IsMapped(page_table[child_index])
        && IsMapped(page_table[parent_index])) {
        return parent_index;
      }
    }
  }
  return std::nullopt;
}

auto ExpectMappedMipPropagationMatchesCpuModel(
  const TwoBoxPageFlagPropagationResult& result) -> void
{
  const auto& virtual_frame
    = result.mapping.bridge.prepared_products.virtual_frame;
  const auto expected = BuildExpectedPropagationFlags(
    virtual_frame, result.mapping.page_table, result.mapping.page_flags);

  ASSERT_EQ(result.mapping.page_table.size(), result.page_table.size());
  EXPECT_EQ(result.page_table, result.mapping.page_table);

  ASSERT_EQ(expected.size(), result.page_flags.size());
  for (std::size_t index = 0U; index < expected.size(); ++index) {
    EXPECT_EQ(result.page_flags[index].bits & kPropagationMask,
      expected[index].bits & kPropagationMask)
      << "page_table_index=" << index;
  }
}

class VsmMappedMipPropagationLiveSceneTest : public VsmLiveSceneHarness { };

NOLINT_TEST_F(VsmMappedMipPropagationLiveSceneTest,
  DirectionalTwoBoxSceneMatchesCpuMappedDescendantPropagationAcrossClipLevels)
{
  constexpr auto kWidth = 256U;
  constexpr auto kHeight = 256U;
  constexpr auto kSlot = Slot { 0U };
  constexpr auto kSequence = SequenceNumber { 121U };

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
    vsm_renderer, resolved_view, kWidth, kHeight, kSequence, kSlot, 0xA001ULL);
  const auto& virtual_frame
    = propagated.mapping.bridge.prepared_products.virtual_frame;

  ASSERT_EQ(virtual_frame.directional_layouts.size(), 1U);
  EXPECT_TRUE(virtual_frame.local_light_layouts.empty());
  EXPECT_GT(virtual_frame.directional_layouts.front().pages_per_axis, 1U);
  EXPECT_GT(virtual_frame.directional_layouts.front().clip_level_count, 1U);
  EXPECT_GT(
    propagated.mapping.bridge.committed_frame.plan.allocated_page_count, 0U);

  const auto expected = BuildExpectedPropagationFlags(virtual_frame,
    propagated.mapping.page_table, propagated.mapping.page_flags);
  EXPECT_GT(
    CountMappedDescendantChanges(propagated.mapping.page_flags, expected), 0U);
  ExpectMappedMipPropagationMatchesCpuModel(propagated);
}

NOLINT_TEST_F(VsmMappedMipPropagationLiveSceneTest,
  DirectionalAndSpotSceneMatchesCpuMappedDescendantPropagationAcrossMixedMapTypes)
{
  constexpr auto kWidth = 1024U;
  constexpr auto kHeight = 1024U;
  constexpr auto kSlot = Slot { 0U };
  constexpr auto kProbeSequence = SequenceNumber { 127U };
  constexpr auto kSequence = SequenceNumber { 128U };

  const auto camera_eye = glm::vec3 { -3.2F, 3.4F, 5.8F };
  const auto camera_target = glm::vec3 { 0.2F, 0.8F, 0.0F };
  const auto sun_direction
    = glm::normalize(glm::vec3 { -0.40558F, 0.40558F, 0.819152F });
  const auto resolved_view
    = MakeLookAtResolvedView(camera_eye, camera_target, kWidth, kHeight);

  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);
  auto scene = CreateTwoBoxShadowScene(sun_direction, 4U);

  auto probe_renderer = oxygen::renderer::vsm::VsmShadowRenderer(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    oxygen::observer_ptr { &renderer->GetStagingProvider() },
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    oxygen::ShadowQualityTier::kHigh);
  const auto depth_probe = RunTwoBoxPageRequestBridge(*renderer, scene,
    probe_renderer, resolved_view, kWidth, kHeight, kProbeSequence, kSlot,
    0xA006ULL, {}, false);
  ASSERT_NE(depth_probe.scene_depth_texture, nullptr);
  const auto probe_samples
    = ReadDepthTextureSamples(*depth_probe.scene_depth_texture, resolved_view,
      "stage-ten.mixed-map.target-probe");
  ASSERT_FALSE(probe_samples.empty());

  const auto target_it = std::min_element(probe_samples.begin(),
    probe_samples.end(), [&](const auto& lhs, const auto& rhs) {
      const auto lhs_delta = lhs.world_position_ws - camera_target;
      const auto rhs_delta = rhs.world_position_ws - camera_target;
      return glm::dot(lhs_delta, lhs_delta) < glm::dot(rhs_delta, rhs_delta);
    });
  ASSERT_NE(target_it, probe_samples.end());
  AttachSpotLightToTwoBoxScene(scene, camera_eye, target_it->world_position_ws,
    18.0F, glm::radians(30.0F), glm::radians(50.0F));

  auto vsm_renderer = oxygen::renderer::vsm::VsmShadowRenderer(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    oxygen::observer_ptr { &renderer->GetStagingProvider() },
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    oxygen::ShadowQualityTier::kHigh);
  const auto propagated = RunTwoBoxPageFlagPropagationStage(*renderer, scene,
    vsm_renderer, resolved_view, kWidth, kHeight, kSequence, kSlot, 0xA006ULL);
  const auto& virtual_frame
    = propagated.mapping.bridge.prepared_products.virtual_frame;

  ASSERT_EQ(virtual_frame.directional_layouts.size(), 1U);
  ASSERT_EQ(virtual_frame.local_light_layouts.size(), 1U);
  EXPECT_GT(virtual_frame.directional_layouts.front().clip_level_count, 1U);
  EXPECT_GT(virtual_frame.local_light_layouts.front().level_count, 1U);
  EXPECT_GT(
    propagated.mapping.bridge.committed_frame.plan.allocated_page_count, 0U);

  auto mapped_parent_index = FindMappedAncestorPairIndex(propagated.page_table,
    virtual_frame.directional_layouts.front(),
    virtual_frame.directional_layouts.front().clip_level_count,
    virtual_frame.directional_layouts.front().pages_per_axis,
    virtual_frame.directional_layouts.front().pages_per_axis);
  if (!mapped_parent_index.has_value()) {
    mapped_parent_index = FindMappedAncestorPairIndex(propagated.page_table,
      virtual_frame.local_light_layouts.front(),
      virtual_frame.local_light_layouts.front().level_count,
      virtual_frame.local_light_layouts.front().pages_per_level_x,
      virtual_frame.local_light_layouts.front().pages_per_level_y);
  }
  ASSERT_TRUE(mapped_parent_index.has_value());
  EXPECT_NE(
    propagated.page_flags[*mapped_parent_index].bits & kMappedDescendantBit,
    0U);

  const auto expected = BuildExpectedPropagationFlags(virtual_frame,
    propagated.mapping.page_table, propagated.mapping.page_flags);
  EXPECT_GT(
    CountMappedDescendantChanges(propagated.mapping.page_flags, expected), 0U);
  ExpectMappedMipPropagationMatchesCpuModel(propagated);
}

NOLINT_TEST_F(VsmMappedMipPropagationLiveSceneTest,
  AddedSpotLightsMatchCpuMappedDescendantPropagationAcrossMixedLocalLayouts)
{
  constexpr auto kWidth = 1024U;
  constexpr auto kHeight = 1024U;
  constexpr auto kSlot = Slot { 0U };
  constexpr auto kProbeSequence = SequenceNumber { 122U };
  constexpr auto kFirstSequence = SequenceNumber { 123U };
  constexpr auto kSecondSequence = SequenceNumber { 124U };

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
    0xA002ULL, {}, false);
  ASSERT_NE(depth_probe.scene_depth_texture, nullptr);
  const auto probe_samples
    = ReadDepthTextureSamples(*depth_probe.scene_depth_texture, resolved_view,
      "stage-ten.local-multi-light.target-probe");
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
      kWidth, kHeight, kFirstSequence, kSlot, 0xA003ULL);
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
      resolved_view, kWidth, kHeight, kSecondSequence, kSlot, 0xA003ULL);
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

  const auto expected = BuildExpectedPropagationFlags(virtual_frame,
    propagated.mapping.page_table, propagated.mapping.page_flags);
  EXPECT_GT(
    CountMappedDescendantChanges(propagated.mapping.page_flags, expected), 0U);
  ExpectMappedMipPropagationMatchesCpuModel(propagated);
}

NOLINT_TEST_F(VsmMappedMipPropagationLiveSceneTest,
  StableDirectionalSceneMatchesCpuMappedDescendantPropagationAcrossReuseOnlyFrame)
{
  constexpr auto kWidth = 256U;
  constexpr auto kHeight = 256U;
  constexpr auto kSlot = Slot { 0U };
  constexpr auto kFirstSequence = SequenceNumber { 129U };
  constexpr auto kSecondSequence = SequenceNumber { 130U };

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
      kWidth, kHeight, kFirstSequence, kSlot, 0xA007ULL);
  ASSERT_NE(first_frame.extracted_frame, nullptr);

  const auto propagated
    = RunTwoBoxPageFlagPropagationStage(*renderer, scene, vsm_renderer,
      resolved_view, kWidth, kHeight, kSecondSequence, kSlot, 0xA007ULL);
  const auto& virtual_frame
    = propagated.mapping.bridge.prepared_products.virtual_frame;
  const auto& frame = propagated.mapping.bridge.committed_frame;

  ASSERT_EQ(virtual_frame.directional_layouts.size(), 1U);
  EXPECT_TRUE(virtual_frame.local_light_layouts.empty());
  EXPECT_GT(frame.plan.reused_page_count, 0U);
  EXPECT_EQ(frame.plan.allocated_page_count, 0U);

  const auto expected = BuildExpectedPropagationFlags(virtual_frame,
    propagated.mapping.page_table, propagated.mapping.page_flags);
  EXPECT_GT(
    CountMappedDescendantChanges(propagated.mapping.page_flags, expected), 0U);
  ExpectMappedMipPropagationMatchesCpuModel(propagated);
}

NOLINT_TEST_F(VsmMappedMipPropagationLiveSceneTest,
  MovedCasterDirectionalSceneMatchesCpuMappedDescendantPropagationAfterRefresh)
{
  constexpr auto kWidth = 256U;
  constexpr auto kHeight = 256U;
  constexpr auto kSlot = Slot { 0U };
  constexpr auto kFirstSequence = SequenceNumber { 125U };
  constexpr auto kSecondSequence = SequenceNumber { 126U };

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
      kWidth, kHeight, kFirstSequence, kSlot, 0xA004ULL);
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
    0xA005ULL, manual_invalidations);
  const auto& virtual_frame
    = propagated.mapping.bridge.prepared_products.virtual_frame;

  EXPECT_FALSE(propagated.mapping.bridge.invalidation_inputs
      .primitive_invalidations.empty());
  ASSERT_NE(propagated.mapping.bridge.metadata_seed_buffer, nullptr);
  ASSERT_EQ(virtual_frame.directional_layouts.size(), 1U);
  EXPECT_GT(virtual_frame.directional_layouts.front().pages_per_axis, 1U);
  EXPECT_GT(
    propagated.mapping.bridge.committed_frame.plan.allocated_page_count, 0U);

  const auto expected = BuildExpectedPropagationFlags(virtual_frame,
    propagated.mapping.page_table, propagated.mapping.page_flags);
  EXPECT_GT(
    CountMappedDescendantChanges(propagated.mapping.page_flags, expected), 0U);
  ExpectMappedMipPropagationMatchesCpuModel(propagated);
}

} // namespace
