//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <glm/geometric.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Renderer/VirtualShadowMaps/VsmProjectionTypes.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmShadowRenderer.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualAddressSpaceTypes.h>

#include "VirtualShadowLiveSceneHarness.h"

namespace {

using oxygen::frame::SequenceNumber;
using oxygen::frame::Slot;
using oxygen::renderer::vsm::TotalPageCount;
using oxygen::renderer::vsm::TryGetPageTableEntryIndex;
using oxygen::renderer::vsm::VsmProjectionLightType;
using oxygen::renderer::vsm::VsmShadowRenderer;
using oxygen::renderer::vsm::testing::VsmLiveSceneHarness;

class VsmVirtualAddressLiveSceneTest : public VsmLiveSceneHarness { };

NOLINT_TEST_F(VsmVirtualAddressLiveSceneTest,
  RealTwoBoxScenePublishesMultiPageDirectionalClipmapFrame)
{
  constexpr auto kWidth = 256U;
  constexpr auto kHeight = 256U;
  constexpr auto kSlot = Slot { 0U };
  constexpr auto kSequence = SequenceNumber { 41U };

  const auto camera_eye = glm::vec3 { -3.2F, 3.4F, 5.8F };
  const auto camera_target = glm::vec3 { 0.2F, 0.8F, 0.0F };
  const auto sun_direction
    = glm::normalize(glm::vec3 { -0.40558F, 0.40558F, 0.819152F });
  const auto resolved_view
    = MakeLookAtResolvedView(camera_eye, camera_target, kWidth, kHeight);

  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);
  auto scene = CreateTwoBoxShadowScene(sun_direction, 4U);
  auto vsm_renderer
    = VsmShadowRenderer(oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      oxygen::observer_ptr { &renderer->GetStagingProvider() },
      oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
      oxygen::ShadowQualityTier::kHigh);

  const auto result = RunTwoBoxLiveShellFrame(*renderer, scene, vsm_renderer,
    resolved_view, kWidth, kHeight, kSequence, kSlot, 0x2002ULL);
  ASSERT_NE(result.prepared_view, nullptr);
  ASSERT_NE(result.extracted_frame, nullptr);

  const auto& frame = result.virtual_frame;
  ASSERT_EQ(frame.directional_layouts.size(), 1U);
  EXPECT_TRUE(frame.local_light_layouts.empty());

  const auto& layout = frame.directional_layouts.front();
  EXPECT_EQ(layout.clip_level_count, 4U);
  EXPECT_GT(layout.pages_per_axis, 1U);
  EXPECT_FALSE(layout.remap_key.empty());
  EXPECT_GT(layout.first_id, 0U);
  EXPECT_EQ(layout.page_grid_origin.size(), layout.clip_level_count);
  EXPECT_EQ(layout.page_world_size.size(), layout.clip_level_count);
  EXPECT_EQ(layout.near_depth.size(), layout.clip_level_count);
  EXPECT_EQ(layout.far_depth.size(), layout.clip_level_count);
  EXPECT_EQ(layout.first_page_table_entry, 0U);
  EXPECT_EQ(frame.total_page_table_entry_count, TotalPageCount(layout));
  EXPECT_EQ(result.extracted_frame->virtual_frame, frame);

  for (std::uint32_t level = 0U; level < layout.clip_level_count; ++level) {
    EXPECT_GT(layout.page_world_size[level], 0.0F);
    EXPECT_LT(layout.near_depth[level], layout.far_depth[level]);
  }

  const auto first_corner = TryGetPageTableEntryIndex(
    layout, { .level = 0U, .page_x = 0U, .page_y = 0U });
  const auto far_corner = TryGetPageTableEntryIndex(layout,
    { .level = 0U,
      .page_x = layout.pages_per_axis - 1U,
      .page_y = layout.pages_per_axis - 1U });
  const auto high_level_corner = TryGetPageTableEntryIndex(layout,
    { .level = layout.clip_level_count - 1U, .page_x = 0U, .page_y = 0U });
  ASSERT_TRUE(first_corner.has_value());
  ASSERT_TRUE(far_corner.has_value());
  ASSERT_TRUE(high_level_corner.has_value());
  EXPECT_NE(*first_corner, *far_corner);
  EXPECT_NE(*first_corner, *high_level_corner);

  auto directional_projection_count = std::size_t { 0U };
  for (const auto& projection : result.extracted_frame->projection_records) {
    if (projection.projection.light_type
      != static_cast<std::uint32_t>(VsmProjectionLightType::kDirectional)) {
      continue;
    }
    ++directional_projection_count;
    EXPECT_EQ(
      projection.map_id, layout.first_id + projection.projection.clipmap_level);
    EXPECT_EQ(projection.level_count, layout.clip_level_count);
    EXPECT_GT(projection.pages_x, 1U);
    EXPECT_GT(projection.pages_y, 1U);
  }
  EXPECT_EQ(directional_projection_count, layout.clip_level_count);

  EXPECT_EQ(result.prepared_view->scene_primitive_history.size(), 2U);
  EXPECT_EQ(result.prepared_view->shadow_caster_bounds.size(), 2U);
  EXPECT_EQ(result.prepared_view->visible_receiver_bounds.size(), 1U);
}

} // namespace
