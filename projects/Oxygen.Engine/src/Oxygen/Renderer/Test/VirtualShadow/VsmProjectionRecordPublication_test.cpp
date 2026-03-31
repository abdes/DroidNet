//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Types/ViewHelpers.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmProjectionTypes.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmShadowRenderer.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualAddressSpaceTypes.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>

#include "VirtualShadowLiveSceneHarness.h"

namespace {

using oxygen::frame::SequenceNumber;
using oxygen::frame::Slot;
using oxygen::renderer::vsm::kVsmInvalidCubeFaceIndex;
using oxygen::renderer::vsm::kVsmInvalidLightIndex;
using oxygen::renderer::vsm::TotalPageCount;
using oxygen::renderer::vsm::VsmPageRequestProjection;
using oxygen::renderer::vsm::VsmProjectionLightType;
using oxygen::renderer::vsm::VsmShadowRenderer;
using oxygen::renderer::vsm::testing::VsmLiveSceneHarness;

constexpr auto kFloatTolerance = 1.0e-4F;

auto ExpectVec3Near(const glm::vec3& actual, const glm::vec3& expected) -> void
{
  EXPECT_NEAR(actual.x, expected.x, kFloatTolerance);
  EXPECT_NEAR(actual.y, expected.y, kFloatTolerance);
  EXPECT_NEAR(actual.z, expected.z, kFloatTolerance);
}

auto ExpectMat4Near(const glm::mat4& actual, const glm::mat4& expected) -> void
{
  for (glm::mat4::length_type column = 0; column < 4; ++column) {
    for (glm::vec4::length_type row = 0; row < 4; ++row) {
      EXPECT_NEAR(actual[column][row], expected[column][row], kFloatTolerance)
        << "matrix mismatch at column " << column << ", row " << row;
    }
  }
}

auto ExpectFiniteMat4(const glm::mat4& matrix) -> void
{
  for (glm::mat4::length_type column = 0; column < 4; ++column) {
    for (glm::vec4::length_type row = 0; row < 4; ++row) {
      EXPECT_TRUE(std::isfinite(matrix[column][row]))
        << "matrix contains a non-finite value at column " << column << ", row "
        << row;
    }
  }
}

class VsmProjectionRecordPublicationLiveSceneTest : public VsmLiveSceneHarness {
protected:
  auto FindProjectionRecord(
    const std::span<const VsmPageRequestProjection> records,
    const VsmProjectionLightType light_type, const std::uint32_t clipmap_level)
    -> const VsmPageRequestProjection*
  {
    const auto it = std::find_if(
      records.begin(), records.end(), [&](const auto& projection) {
        return projection.projection.light_type
          == static_cast<std::uint32_t>(light_type)
          && projection.projection.clipmap_level == clipmap_level;
      });
    return it != records.end() ? &(*it) : nullptr;
  }
};

NOLINT_TEST_F(VsmProjectionRecordPublicationLiveSceneTest,
  RealDirectionalClipmapScenePublishesSceneDerivedProjectionRecords)
{
  constexpr auto kWidth = 256U;
  constexpr auto kHeight = 256U;
  constexpr auto kSlot = Slot { 0U };
  constexpr auto kSequence = SequenceNumber { 61U };

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
    resolved_view, kWidth, kHeight, kSequence, kSlot, 0x4001ULL);
  ASSERT_NE(result.prepared_view, nullptr);
  ASSERT_NE(result.extracted_frame, nullptr);
  ASSERT_EQ(result.virtual_frame.directional_layouts.size(), 1U);
  EXPECT_TRUE(result.virtual_frame.local_light_layouts.empty());

  const auto& layout = result.virtual_frame.directional_layouts.front();
  ASSERT_EQ(result.extracted_frame->virtual_frame, result.virtual_frame);
  ASSERT_EQ(
    result.extracted_frame->projection_records.size(), layout.clip_level_count);
  EXPECT_EQ(result.extracted_frame->page_table.size(),
    result.virtual_frame.total_page_table_entry_count);
  EXPECT_EQ(
    result.virtual_frame.total_page_table_entry_count, TotalPageCount(layout));

  for (std::uint32_t clip_index = 0U; clip_index < layout.clip_level_count;
    ++clip_index) {
    const auto projection_it
      = FindProjectionRecord(result.extracted_frame->projection_records,
        VsmProjectionLightType::kDirectional, clip_index);
    ASSERT_NE(projection_it, nullptr);

    const auto& projection = *projection_it;
    EXPECT_EQ(projection.map_id, layout.first_id + clip_index);
    EXPECT_EQ(projection.first_page_table_entry, layout.first_page_table_entry);
    EXPECT_EQ(projection.map_pages_x, layout.pages_per_axis);
    EXPECT_EQ(projection.map_pages_y, layout.pages_per_axis);
    EXPECT_EQ(projection.pages_x, layout.pages_per_axis);
    EXPECT_EQ(projection.pages_y, layout.pages_per_axis);
    EXPECT_EQ(projection.page_offset_x, 0U);
    EXPECT_EQ(projection.page_offset_y, 0U);
    EXPECT_EQ(projection.level_count, layout.clip_level_count);
    EXPECT_EQ(projection.coarse_level, 0U);
    EXPECT_EQ(projection.light_index, kVsmInvalidLightIndex);
    EXPECT_EQ(projection.cube_face_index, kVsmInvalidCubeFaceIndex);
    EXPECT_EQ(projection.projection.light_type,
      static_cast<std::uint32_t>(VsmProjectionLightType::kDirectional));
    EXPECT_EQ(projection.projection.clipmap_level, clip_index);
    EXPECT_EQ(projection.projection.clipmap_corner_offset,
      layout.page_grid_origin[clip_index]);
    EXPECT_NE(projection.map_id, 0U);
    EXPECT_GT(projection.pages_x, 1U);
    EXPECT_GT(projection.pages_y, 1U);
    EXPECT_NE(projection.projection.view_matrix, glm::mat4 { 1.0F });
    EXPECT_NE(projection.projection.projection_matrix, glm::mat4 { 1.0F });
    EXPECT_EQ(projection.projection.view_origin_ws_pad.w, 0.0F);
    EXPECT_GT(projection.projection.receiver_depth_range_pad.y,
      projection.projection.receiver_depth_range_pad.x);
    EXPECT_GE(projection.projection.receiver_depth_range_pad.x, 0.0F);
    EXPECT_EQ(projection.projection.receiver_depth_range_pad.z, 0.0F);
    EXPECT_EQ(projection.projection.receiver_depth_range_pad.w, 0.0F);

    ExpectFiniteMat4(projection.projection.view_matrix);
    ExpectFiniteMat4(projection.projection.projection_matrix);

    const auto light_origin
      = glm::vec3(glm::inverse(projection.projection.view_matrix)
        * glm::vec4 { 0.0F, 0.0F, 0.0F, 1.0F });
    ExpectVec3Near(
      glm::vec3 { projection.projection.view_origin_ws_pad }, light_origin);
  }
}

NOLINT_TEST_F(VsmProjectionRecordPublicationLiveSceneTest,
  RealSingleCascadeDirectionalSceneCoversVisibleFloorReceiverDepthRange)
{
  constexpr auto kWidth = 256U;
  constexpr auto kHeight = 256U;
  constexpr auto kSlot = Slot { 0U };
  constexpr auto kSequence = SequenceNumber { 63U };

  const auto camera_eye = glm::vec3 { -3.2F, 3.4F, 5.8F };
  const auto camera_target = glm::vec3 { 0.2F, 0.8F, 0.0F };
  const auto sun_direction
    = glm::normalize(glm::vec3 { 0.40558F, -0.40558F, -0.819152F });
  const auto resolved_view
    = MakeLookAtResolvedView(camera_eye, camera_target, kWidth, kHeight);

  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);
  auto scene = CreateTwoBoxShadowScene(sun_direction, 1U);
  auto vsm_renderer
    = VsmShadowRenderer(oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      oxygen::observer_ptr { &renderer->GetStagingProvider() },
      oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
      oxygen::ShadowQualityTier::kHigh);

  const auto result = RunTwoBoxLiveShellFrame(*renderer, scene, vsm_renderer,
    resolved_view, kWidth, kHeight, kSequence, kSlot, 0x4003ULL);
  ASSERT_NE(result.extracted_frame, nullptr);
  ASSERT_NE(result.scene_depth_texture, nullptr);
  ASSERT_EQ(result.extracted_frame->projection_records.size(), 1U);

  const auto projection_it
    = FindProjectionRecord(result.extracted_frame->projection_records,
      VsmProjectionLightType::kDirectional, 0U);
  ASSERT_NE(projection_it, nullptr);

  const auto samples = ReadDepthTextureSamples(*result.scene_depth_texture,
    resolved_view, "stage-four.projection-records.single-cascade.floor-depth");

  auto farthest_visible_floor_depth = 0.0F;
  for (const auto& sample : samples) {
    if (std::abs(sample.world_position_ws.y) > 0.05F
      || sample.world_position_ws.x < -4.5F || sample.world_position_ws.x > 4.5F
      || sample.world_position_ws.z < -4.5F
      || sample.world_position_ws.z > 4.5F) {
      continue;
    }

    const auto receiver_view
      = resolved_view.ViewMatrix() * glm::vec4(sample.world_position_ws, 1.0F);
    farthest_visible_floor_depth
      = std::max(farthest_visible_floor_depth, -receiver_view.z);
  }

  ASSERT_GT(farthest_visible_floor_depth, 0.0F);
  EXPECT_GE(projection_it->projection.receiver_depth_range_pad.y + 1.0e-3F,
    farthest_visible_floor_depth);
}

NOLINT_TEST_F(VsmProjectionRecordPublicationLiveSceneTest,
  RealPagedSpotLightScenePublishesSceneDerivedLocalProjectionRecord)
{
  constexpr auto kWidth = 1024U;
  constexpr auto kHeight = 1024U;
  constexpr auto kSlot = Slot { 0U };
  constexpr auto kSequence = SequenceNumber { 62U };

  const auto camera_eye = glm::vec3 { 0.5F, 3.0F, 7.2F };
  const auto camera_target = glm::vec3 { 0.0F, 1.0F, 0.0F };
  const auto resolved_view
    = MakeLookAtResolvedView(camera_eye, camera_target, kWidth, kHeight);
  const auto sun_direction
    = glm::normalize(glm::vec3 { -0.40558F, 0.40558F, 0.819152F });
  const auto spot_position = glm::vec3 { 0.25F, 6.5F, 4.0F };
  const auto spot_target = glm::vec3 { 0.15F, 1.4F, 0.1F };

  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);
  auto scene = CreateTwoBoxShadowScene(sun_direction, 4U);
  auto sun_impl = scene.sun_node.GetImpl();
  ASSERT_TRUE(sun_impl.has_value());
  auto& sun_light
    = sun_impl->get().GetComponent<oxygen::scene::DirectionalLight>();
  sun_light.Common().casts_shadows = false;
  UpdateTransforms(*scene.scene, scene.sun_node);
  AttachSpotLightToTwoBoxScene(scene, spot_position, spot_target, 18.0F,
    glm::radians(20.0F), glm::radians(32.0F));

  auto vsm_renderer
    = VsmShadowRenderer(oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      oxygen::observer_ptr { &renderer->GetStagingProvider() },
      oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
      oxygen::ShadowQualityTier::kHigh);

  const auto result = RunTwoBoxLiveShellFrame(*renderer, scene, vsm_renderer,
    resolved_view, kWidth, kHeight, kSequence, kSlot, 0x4002ULL);
  ASSERT_NE(result.prepared_view, nullptr);
  ASSERT_NE(result.extracted_frame, nullptr);

  EXPECT_TRUE(result.virtual_frame.directional_layouts.empty());
  ASSERT_EQ(result.virtual_frame.local_light_layouts.size(), 1U);
  const auto& layout = result.virtual_frame.local_light_layouts.front();
  EXPECT_GT(layout.id, 0U);
  EXPECT_FALSE(layout.remap_key.empty());
  EXPECT_GT(layout.level_count, 1U);
  EXPECT_GT(layout.pages_per_level_x, 1U);
  EXPECT_GT(layout.pages_per_level_y, 1U);
  EXPECT_EQ(
    result.virtual_frame.total_page_table_entry_count, layout.total_page_count);

  ASSERT_EQ(result.prepared_view->directional_shadow_candidates.size(), 0U);
  ASSERT_EQ(result.prepared_view->positional_shadow_candidates.size(), 1U);
  ASSERT_EQ(result.prepared_view->positional_lights.size(), 1U);
  const auto candidate
    = result.prepared_view->positional_shadow_candidates.front();
  ASSERT_LT(
    candidate.light_index, result.prepared_view->positional_lights.size());
  const auto& light
    = result.prepared_view->positional_lights[candidate.light_index];

  const auto projection_it
    = FindProjectionRecord(result.extracted_frame->projection_records,
      VsmProjectionLightType::kLocal, 0U);
  ASSERT_NE(projection_it, nullptr);
  const auto& projection = *projection_it;

  EXPECT_EQ(result.extracted_frame->projection_records.size(), 1U);
  EXPECT_EQ(projection.map_id, layout.id);
  EXPECT_EQ(projection.first_page_table_entry, layout.first_page_table_entry);
  EXPECT_EQ(projection.map_pages_x, layout.pages_per_level_x);
  EXPECT_EQ(projection.map_pages_y, layout.pages_per_level_y);
  EXPECT_EQ(projection.pages_x, layout.pages_per_level_x);
  EXPECT_EQ(projection.pages_y, layout.pages_per_level_y);
  EXPECT_EQ(projection.page_offset_x, 0U);
  EXPECT_EQ(projection.page_offset_y, 0U);
  EXPECT_EQ(projection.level_count, layout.level_count);
  EXPECT_EQ(projection.coarse_level, 0U);
  EXPECT_EQ(projection.light_index, candidate.light_index);
  EXPECT_EQ(projection.cube_face_index, kVsmInvalidCubeFaceIndex);
  EXPECT_EQ(projection.projection.light_type,
    static_cast<std::uint32_t>(VsmProjectionLightType::kLocal));
  const auto kZeroOffset = glm::ivec2 { 0, 0 };
  EXPECT_EQ(projection.projection.clipmap_corner_offset, kZeroOffset);
  EXPECT_EQ(projection.projection.clipmap_level, 0U);
  EXPECT_EQ(projection.projection.view_origin_ws_pad.w, 0.0F);
  EXPECT_EQ(projection.projection.receiver_depth_range_pad, glm::vec4 { 0.0F });

  ExpectFiniteMat4(projection.projection.view_matrix);
  ExpectFiniteMat4(projection.projection.projection_matrix);
  ExpectVec3Near(glm::vec3 { projection.projection.view_origin_ws_pad },
    glm::vec3 { light.position_ws });

  const auto light_direction = glm::normalize(light.direction_ws);
  const auto light_position = glm::vec3 { light.position_ws };
  const auto world_up
    = std::abs(glm::dot(light_direction, glm::vec3 { 0.0F, 0.0F, 1.0F }))
      > 0.95F
    ? glm::vec3 { 1.0F, 0.0F, 0.0F }
    : glm::vec3 { 0.0F, 0.0F, 1.0F };
  const auto expected_view
    = glm::lookAtRH(light_position, light_position + light_direction, world_up);
  const auto expected_outer_cone_angle = std::clamp(
    2.0F * std::acos(std::clamp(light.outer_cone_cos, -1.0F, 1.0F)),
    glm::radians(1.0F), glm::radians(175.0F));
  const auto expected_projection
    = oxygen::MakeReversedZPerspectiveProjectionRH_ZO(
      expected_outer_cone_angle, 1.0F, 0.1F, std::max(light.range, 0.2F));

  ExpectMat4Near(projection.projection.view_matrix, expected_view);
  ExpectMat4Near(projection.projection.projection_matrix, expected_projection);

  const auto light_origin
    = glm::vec3(glm::inverse(projection.projection.view_matrix)
      * glm::vec4 { 0.0F, 0.0F, 0.0F, 1.0F });
  ExpectVec3Near(light_origin, light_position);
}

} // namespace
