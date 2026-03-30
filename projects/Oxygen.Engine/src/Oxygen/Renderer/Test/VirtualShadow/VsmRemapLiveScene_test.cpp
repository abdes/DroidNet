//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Renderer/VirtualShadowMaps/VsmProjectionTypes.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmShadowRenderer.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualAddressSpaceTypes.h>

#include "VirtualShadowLiveSceneHarness.h"

namespace {

using oxygen::frame::SequenceNumber;
using oxygen::frame::Slot;
using oxygen::renderer::vsm::VsmProjectionLightType;
using oxygen::renderer::vsm::VsmReuseRejectionReason;
using oxygen::renderer::vsm::VsmShadowRenderer;
using oxygen::renderer::vsm::testing::VsmLiveSceneHarness;

class VsmRemapLiveSceneTest : public VsmLiveSceneHarness { };

NOLINT_TEST_F(VsmRemapLiveSceneTest,
  RealTwoBoxSceneBuildsDirectionalClipmapRemapWithPagePanOffsets)
{
  constexpr auto kWidth = 256U;
  constexpr auto kHeight = 256U;
  constexpr auto kSlot = Slot { 0U };
  constexpr auto kFirstSequence = SequenceNumber { 51U };
  constexpr auto kSecondSequence = SequenceNumber { 52U };

  const auto camera_eye = glm::vec3 { -3.2F, 3.4F, 5.8F };
  const auto camera_target = glm::vec3 { 0.2F, 0.8F, 0.0F };
  const auto sun_direction
    = glm::normalize(glm::vec3 { -0.40558F, 0.40558F, 0.819152F });
  const auto first_view
    = MakeLookAtResolvedView(camera_eye, camera_target, kWidth, kHeight);

  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);
  auto scene = CreateTwoBoxShadowScene(sun_direction, 4U);
  auto vsm_renderer
    = VsmShadowRenderer(oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      oxygen::observer_ptr { &renderer->GetStagingProvider() },
      oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
      oxygen::ShadowQualityTier::kHigh);

  const auto first_result
    = RunTwoBoxLiveShellFrame(*renderer, scene, vsm_renderer, first_view,
      kWidth, kHeight, kFirstSequence, kSlot, 0x3001ULL);
  ASSERT_NE(first_result.extracted_frame, nullptr);
  ASSERT_EQ(first_result.virtual_frame.directional_layouts.size(), 1U);

  const auto& first_layout
    = first_result.virtual_frame.directional_layouts.front();
  ASSERT_FALSE(first_layout.page_world_size.empty());
  ASSERT_FALSE(first_result.extracted_frame->projection_records.empty());

  const auto first_directional_projection
    = std::find_if(first_result.extracted_frame->projection_records.begin(),
      first_result.extracted_frame->projection_records.end(),
      [](const auto& projection) {
        return projection.projection.light_type
          == static_cast<std::uint32_t>(VsmProjectionLightType::kDirectional);
      });
  ASSERT_NE(first_directional_projection,
    first_result.extracted_frame->projection_records.end());

  const auto light_space_page_shift_ws = glm::vec3(
    glm::inverse(first_directional_projection->projection.view_matrix)
    * glm::vec4 { first_layout.page_world_size[0] * 2.5F, 0.0F, 0.0F, 0.0F });
  const auto second_view
    = MakeLookAtResolvedView(camera_eye + light_space_page_shift_ws,
      camera_target + light_space_page_shift_ws, kWidth, kHeight);

  const auto second_result
    = RunTwoBoxLiveShellFrame(*renderer, scene, vsm_renderer, second_view,
      kWidth, kHeight, kSecondSequence, kSlot, 0x3002ULL);
  ASSERT_NE(second_result.extracted_frame, nullptr);
  ASSERT_EQ(second_result.virtual_frame.directional_layouts.size(), 1U);

  const auto& second_layout
    = second_result.virtual_frame.directional_layouts.front();
  EXPECT_NE(
    second_layout.page_grid_origin[0], first_layout.page_grid_origin[0]);

  const auto remap = vsm_renderer.GetVirtualAddressSpace().BuildRemapTable(
    first_result.virtual_frame);
  ASSERT_EQ(remap.entries.size(), first_layout.clip_level_count);

  auto non_zero_offset_count = std::size_t { 0U };
  for (std::uint32_t level = 0U; level < first_layout.clip_level_count;
    ++level) {
    const auto& entry = remap.entries[level];
    EXPECT_EQ(entry.previous_id, first_layout.first_id + level);
    EXPECT_EQ(entry.current_id, second_layout.first_id + level);
    EXPECT_EQ(entry.rejection_reason, VsmReuseRejectionReason::kNone);
    EXPECT_EQ(entry.page_offset,
      second_layout.page_grid_origin[level]
        - first_layout.page_grid_origin[level]);
    if (entry.page_offset != glm::ivec2 { 0, 0 }) {
      ++non_zero_offset_count;
    }
  }
  EXPECT_GT(non_zero_offset_count, 0U);
}

} // namespace
