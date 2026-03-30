//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <glm/geometric.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Renderer/VirtualShadowMaps/VsmProjectionRouting.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmShaderTypes.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmShadowRenderer.h>

#include "VirtualShadowLiveSceneHarness.h"

namespace {

using oxygen::frame::SequenceNumber;
using oxygen::frame::Slot;
using oxygen::renderer::vsm::DecodePhysicalPageIndex;
using oxygen::renderer::vsm::IsMapped;
using oxygen::renderer::vsm::testing::TwoBoxAvailablePagePackingResult;
using oxygen::renderer::vsm::testing::VsmLiveSceneHarness;

[[nodiscard]] auto CollectUnallocatedPhysicalPages(
  const std::span<const oxygen::renderer::vsm::VsmPhysicalPageMeta> metadata)
  -> std::vector<std::uint32_t>
{
  auto pages = std::vector<std::uint32_t> {};
  pages.reserve(metadata.size());
  for (std::uint32_t index = 0U; index < metadata.size(); ++index) {
    if (!static_cast<bool>(metadata[index].is_allocated)) {
      pages.push_back(index);
    }
  }
  return pages;
}

[[nodiscard]] auto CollectMappedPhysicalPages(
  const std::span<const oxygen::renderer::vsm::VsmShaderPageTableEntry>
    page_table) -> std::vector<std::uint32_t>
{
  auto pages = std::vector<std::uint32_t> {};
  pages.reserve(page_table.size());
  for (const auto& entry : page_table) {
    if (IsMapped(entry)) {
      pages.push_back(DecodePhysicalPageIndex(entry).value);
    }
  }
  return pages;
}

auto ExpectAvailablePageStackMatchesMetadata(
  const TwoBoxAvailablePagePackingResult& result) -> void
{
  const auto expected
    = CollectUnallocatedPhysicalPages(result.physical_metadata);
  ASSERT_EQ(
    result.available_page_count, static_cast<std::uint32_t>(expected.size()));
  EXPECT_EQ(result.available_pages, expected);
  EXPECT_TRUE(std::is_sorted(
    result.available_pages.begin(), result.available_pages.end()));

  const auto mapped_pages = CollectMappedPhysicalPages(result.page_table);
  for (const auto page : mapped_pages) {
    EXPECT_EQ(std::find(result.available_pages.begin(),
                result.available_pages.end(), page),
      result.available_pages.end())
      << "mapped physical page " << page << " leaked into the available stack";
  }

  for (const auto page : result.available_pages) {
    ASSERT_LT(page, result.physical_metadata.size());
    EXPECT_FALSE(static_cast<bool>(result.physical_metadata[page].is_allocated))
      << "available stack published allocated page " << page;
  }
}

class VsmAvailablePagePackingLiveSceneTest : public VsmLiveSceneHarness { };

NOLINT_TEST_F(VsmAvailablePagePackingLiveSceneTest,
  StableDirectionalScenePacksAllUnallocatedPagesIntoAscendingStack)
{
  constexpr auto kWidth = 256U;
  constexpr auto kHeight = 256U;
  constexpr auto kSlot = Slot { 0U };
  constexpr auto kFirstSequence = SequenceNumber { 91U };
  constexpr auto kSecondSequence = SequenceNumber { 92U };

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
      kWidth, kHeight, kFirstSequence, kSlot, 0x7001ULL);
  ASSERT_NE(first_frame.extracted_frame, nullptr);
  ASSERT_EQ(first_frame.virtual_frame.directional_layouts.size(), 1U);

  const auto packed
    = RunTwoBoxAvailablePagePackingStage(*renderer, scene, vsm_renderer,
      resolved_view, kWidth, kHeight, kSecondSequence, kSlot, 0x7001ULL);
  const auto& frame = packed.bridge.committed_frame;
  const auto& layout
    = packed.bridge.prepared_products.virtual_frame.directional_layouts.front();

  EXPECT_TRUE(packed.bridge.bridge_committed_requests);
  EXPECT_GT(layout.pages_per_axis, 1U);
  EXPECT_GT(frame.plan.reused_page_count, 0U);
  EXPECT_EQ(frame.plan.allocated_page_count, 0U);
  EXPECT_GT(packed.available_page_count, 0U);
  ExpectAvailablePageStackMatchesMetadata(packed);
}

NOLINT_TEST_F(VsmAvailablePagePackingLiveSceneTest,
  DirectionalClipmapPanPacksDroppedPreviousPagesIntoAvailableStack)
{
  constexpr auto kWidth = 256U;
  constexpr auto kHeight = 256U;
  constexpr auto kSlot = Slot { 0U };
  constexpr auto kFirstSequence = SequenceNumber { 93U };
  constexpr auto kSecondSequence = SequenceNumber { 94U };

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
      kWidth, kHeight, kFirstSequence, kSlot, 0x7002ULL);
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

  const auto packed
    = RunTwoBoxAvailablePagePackingStage(*renderer, scene, vsm_renderer,
      second_view, kWidth, kHeight, kSecondSequence, kSlot, 0x7003ULL);
  const auto& frame = packed.bridge.committed_frame;
  const auto& second_layout
    = packed.bridge.prepared_products.virtual_frame.directional_layouts.front();

  EXPECT_GT(frame.plan.reused_page_count, 0U);
  EXPECT_GT(packed.available_page_count, 0U);
  EXPECT_NE(
    second_layout.page_grid_origin[0], first_layout.page_grid_origin[0]);
  ExpectAvailablePageStackMatchesMetadata(packed);

  auto previous_allocated_pages = std::vector<std::uint32_t> {};
  for (std::uint32_t index = 0U;
    index < first_frame.extracted_frame->physical_pages.size(); ++index) {
    if (static_cast<bool>(
          first_frame.extracted_frame->physical_pages[index].is_allocated)) {
      previous_allocated_pages.push_back(index);
    }
  }
  ASSERT_FALSE(previous_allocated_pages.empty());

  auto released_previous_pages = std::vector<std::uint32_t> {};
  for (const auto page : previous_allocated_pages) {
    ASSERT_LT(page, packed.physical_metadata.size());
    if (!static_cast<bool>(packed.physical_metadata[page].is_allocated)) {
      released_previous_pages.push_back(page);
    }
  }

  ASSERT_FALSE(released_previous_pages.empty())
    << "Directional clipmap pan did not release any previously allocated "
       "physical pages before Stage 7 packing";
  for (const auto page : released_previous_pages) {
    EXPECT_NE(std::find(packed.available_pages.begin(),
                packed.available_pages.end(), page),
      packed.available_pages.end())
      << "released previous physical page " << page
      << " is missing from the Stage 7 available stack";
  }
}

NOLINT_TEST_F(VsmAvailablePagePackingLiveSceneTest,
  StablePagedSpotLightScenePacksOnlyUnusedPagesAfterLocalReuse)
{
  constexpr auto kWidth = 1024U;
  constexpr auto kHeight = 1024U;
  constexpr auto kSlot = Slot { 0U };
  constexpr auto kFirstSequence = SequenceNumber { 96U };
  constexpr auto kSecondSequence = SequenceNumber { 97U };

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
    oxygen::ShadowQualityTier::kHigh);

  const auto first_frame
    = PrimeTwoBoxExtractedFrame(*renderer, scene, vsm_renderer, resolved_view,
      kWidth, kHeight, kFirstSequence, kSlot, 0x7005ULL);
  ASSERT_EQ(first_frame.virtual_frame.directional_layouts.size(), 0U);
  ASSERT_EQ(first_frame.virtual_frame.local_light_layouts.size(), 1U);

  const auto packed
    = RunTwoBoxAvailablePagePackingStage(*renderer, scene, vsm_renderer,
      resolved_view, kWidth, kHeight, kSecondSequence, kSlot, 0x7005ULL);
  const auto& frame = packed.bridge.committed_frame;
  const auto& local_layout
    = packed.bridge.prepared_products.virtual_frame.local_light_layouts.front();

  EXPECT_GT(local_layout.total_page_count, 1U);
  EXPECT_GT(frame.plan.reused_page_count, 0U);
  EXPECT_EQ(frame.plan.allocated_page_count, 0U);
  EXPECT_GT(packed.available_page_count, 0U);
  ExpectAvailablePageStackMatchesMetadata(packed);

  auto local_mapped_pages = std::vector<std::uint32_t> {};
  for (std::uint32_t i = 0U; i < local_layout.total_page_count; ++i) {
    const auto page_table_index = local_layout.first_page_table_entry + i;
    ASSERT_LT(page_table_index, packed.page_table.size());
    if (IsMapped(packed.page_table[page_table_index])) {
      local_mapped_pages.push_back(
        DecodePhysicalPageIndex(packed.page_table[page_table_index]).value);
    }
  }

  ASSERT_FALSE(local_mapped_pages.empty());
  for (const auto page : local_mapped_pages) {
    EXPECT_EQ(std::find(packed.available_pages.begin(),
                packed.available_pages.end(), page),
      packed.available_pages.end())
      << "reused local physical page " << page
      << " leaked into the Stage 7 available stack";
  }
}

NOLINT_TEST_F(VsmAvailablePagePackingLiveSceneTest,
  StablePagedSpotLightSceneDoesNotWarnAboutInlineStagingPartitionReuse)
{
  constexpr auto kWidth = 1024U;
  constexpr auto kHeight = 1024U;
  constexpr auto kSlot = Slot { 0U };
  constexpr auto kFirstSequence = SequenceNumber { 98U };
  constexpr auto kSecondSequence = SequenceNumber { 99U };

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
  AttachSpotLightToTwoBoxScene(scene, spot_position,
    PrimarySpotTargetForTwoBoxScene(scene), 18.0F, glm::radians(30.0F),
    glm::radians(50.0F));

  auto vsm_renderer = oxygen::renderer::vsm::VsmShadowRenderer(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    oxygen::observer_ptr { &renderer->GetStagingProvider() },
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    oxygen::ShadowQualityTier::kHigh);

  testing::internal::CaptureStderr();
  const auto first_frame
    = PrimeTwoBoxExtractedFrame(*renderer, scene, vsm_renderer, resolved_view,
      kWidth, kHeight, kFirstSequence, kSlot, 0x7006ULL);
  ASSERT_NE(first_frame.extracted_frame, nullptr);
  const auto packed
    = RunTwoBoxAvailablePagePackingStage(*renderer, scene, vsm_renderer,
      resolved_view, kWidth, kHeight, kSecondSequence, kSlot, 0x7006ULL);
  const auto captured_stderr = testing::internal::GetCapturedStderr();

  ASSERT_EQ(first_frame.virtual_frame.local_light_layouts.size(), 1U);
  EXPECT_GT(packed.bridge.committed_frame.plan.reused_page_count, 0U);
  EXPECT_EQ(captured_stderr.find("Reusing staging buffer"), std::string::npos)
    << captured_stderr;
}

} // namespace
