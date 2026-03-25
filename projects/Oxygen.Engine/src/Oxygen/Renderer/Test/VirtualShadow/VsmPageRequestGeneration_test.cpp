//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include "VirtualShadowTestFixtures.h"

#include <array>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <Oxygen/Renderer/VirtualShadowMaps/VsmPageRequestGeneration.h>

namespace {

using oxygen::renderer::vsm::BuildPageRequests;
using oxygen::renderer::vsm::kVsmInvalidLightIndex;
using oxygen::renderer::vsm::TryProjectWorldToPage;
using oxygen::renderer::vsm::VsmPageRequestFlags;
using oxygen::renderer::vsm::VsmPageRequestGenerationOptions;
using oxygen::renderer::vsm::VsmPageRequestProjection;
using oxygen::renderer::vsm::VsmProjectionData;
using oxygen::renderer::vsm::VsmProjectionLightType;
using oxygen::renderer::vsm::VsmVisiblePixelSample;
using oxygen::renderer::vsm::testing::VirtualShadowTest;

class VsmPageRequestGenerationTest : public VirtualShadowTest {
protected:
  [[nodiscard]] static auto MakeProjection(const std::uint32_t map_id,
    const std::uint32_t first_page_table_entry, const std::uint32_t level_count,
    const std::uint32_t coarse_level,
    const std::uint32_t light_index = kVsmInvalidLightIndex,
    const std::uint32_t map_pages_x = 4U, const std::uint32_t map_pages_y = 4U,
    const std::uint32_t pages_x = 4U, const std::uint32_t pages_y = 4U,
    const std::uint32_t page_offset_x = 0U,
    const std::uint32_t page_offset_y = 0U,
    const std::uint32_t cube_face_index
    = oxygen::renderer::vsm::kVsmInvalidCubeFaceIndex,
    const glm::mat4& view_matrix = glm::lookAtRH(glm::vec3 { 0.0F, 0.0F, 0.0F },
      glm::vec3 { 0.0F, 0.0F, -1.0F }, glm::vec3 { 0.0F, 1.0F, 0.0F }),
    const glm::mat4& projection_matrix = glm::perspectiveRH_ZO(
      glm::radians(90.0F), 1.0F, 0.1F, 100.0F)) -> VsmPageRequestProjection
  {
    return VsmPageRequestProjection {
      .projection = VsmProjectionData {
        .view_matrix = view_matrix,
        .projection_matrix = projection_matrix,
        .view_origin_ws_pad = glm::vec4 { 0.0F, 0.0F, 0.0F, 0.0F },
        .clipmap_corner_offset = { 0, 0 },
        .clipmap_level = 0U,
        .light_type
        = static_cast<std::uint32_t>(VsmProjectionLightType::kLocal),
      },
      .map_id = map_id,
      .first_page_table_entry = first_page_table_entry,
      .map_pages_x = map_pages_x,
      .map_pages_y = map_pages_y,
      .pages_x = pages_x,
      .pages_y = pages_y,
      .page_offset_x = page_offset_x,
      .page_offset_y = page_offset_y,
      .level_count = level_count,
      .coarse_level = coarse_level,
      .light_index = light_index,
      .cube_face_index = cube_face_index,
    };
  }
};

NOLINT_TEST_F(VsmPageRequestGenerationTest,
  VisibleWorldSampleProjectsIntoFineAndCoarseRequests)
{
  const auto projection = MakeProjection(7U, 100U, 4U, 3U);

  const auto requests = BuildPageRequests(std::array { projection },
    std::array { VsmVisiblePixelSample {
      .world_position_ws = { 0.0F, 0.0F, -5.0F },
    } });

  ASSERT_THAT(requests, ::testing::SizeIs(2));
  EXPECT_EQ(requests[0].map_id, 7U);
  EXPECT_EQ(requests[0].page.level, 0U);
  EXPECT_EQ(requests[0].page.page_x, 2U);
  EXPECT_EQ(requests[0].page.page_y, 2U);
  EXPECT_EQ(requests[0].flags, VsmPageRequestFlags::kRequired);

  EXPECT_EQ(requests[1].map_id, 7U);
  EXPECT_EQ(requests[1].page.level, 3U);
  EXPECT_EQ(requests[1].page.page_x, 2U);
  EXPECT_EQ(requests[1].page.page_y, 2U);
  EXPECT_EQ(requests[1].flags,
    VsmPageRequestFlags::kRequired | VsmPageRequestFlags::kCoarse);
}

NOLINT_TEST_F(VsmPageRequestGenerationTest,
  LightGridPruningSkipsLocalLightsThatDoNotAffectTheSample)
{
  const auto requests
    = BuildPageRequests(std::array { MakeProjection(11U, 0U, 1U, 0U, 3U),
                          MakeProjection(12U, 16U, 1U, 0U, 5U) },
      std::array {
        VsmVisiblePixelSample { .world_position_ws = { 0.0F, 0.0F, -5.0F },
          .affecting_local_light_indices = { 5U } } });

  ASSERT_THAT(requests, ::testing::SizeIs(1));
  EXPECT_EQ(requests[0].map_id, 12U);
}

NOLINT_TEST_F(VsmPageRequestGenerationTest,
  DisablingLightGridPruningKeepsAllLocalLightRequests)
{
  const auto requests
    = BuildPageRequests(std::array { MakeProjection(11U, 0U, 1U, 0U, 3U),
                          MakeProjection(12U, 16U, 1U, 0U, 5U) },
      std::array {
        VsmVisiblePixelSample { .world_position_ws = { 0.0F, 0.0F, -5.0F },
          .affecting_local_light_indices = { 5U } } },
      VsmPageRequestGenerationOptions {
        .enable_coarse_pages = true,
        .enable_light_grid_pruning = false,
      });

  ASSERT_THAT(requests, ::testing::SizeIs(2));
  EXPECT_EQ(requests[0].map_id, 11U);
  EXPECT_EQ(requests[1].map_id, 12U);
}

NOLINT_TEST_F(
  VsmPageRequestGenerationTest, CoarseRequestsCanBeDisabledPerGenerationRun)
{
  const auto requests
    = BuildPageRequests(std::array { MakeProjection(7U, 100U, 4U, 3U) },
      std::array {
        VsmVisiblePixelSample { .world_position_ws = { 0.0F, 0.0F, -5.0F } } },
      VsmPageRequestGenerationOptions {
        .enable_coarse_pages = false,
        .enable_light_grid_pruning = true,
      });

  ASSERT_THAT(requests, ::testing::SizeIs(1));
  EXPECT_EQ(requests[0].page.level, 0U);
  EXPECT_EQ(requests[0].flags, VsmPageRequestFlags::kRequired);
}

NOLINT_TEST_F(VsmPageRequestGenerationTest,
  MalformedProjectionsAreIgnoredAndDuplicateSamplesCoalesce)
{
  auto malformed = MakeProjection(15U, 0U, 1U, 0U);
  malformed.pages_x = 0U;

  const auto requests = BuildPageRequests(
    std::array { malformed, MakeProjection(16U, 0U, 2U, 1U) },
    std::array {
      VsmVisiblePixelSample { .world_position_ws = { 0.0F, 0.0F, -5.0F } },
      VsmVisiblePixelSample { .world_position_ws = { 0.0F, 0.0F, -5.0F } } });

  ASSERT_THAT(requests, ::testing::SizeIs(2));
  EXPECT_EQ(requests[0].map_id, 16U);
  EXPECT_EQ(requests[1].map_id, 16U);
}

NOLINT_TEST_F(VsmPageRequestGenerationTest,
  WorldPositionsOutsideTheProjectionFrustumProduceNoRequests)
{
  const auto page = TryProjectWorldToPage(
    MakeProjection(7U, 0U, 1U, 0U), glm::vec3 { 50.0F, 0.0F, -5.0F });

  EXPECT_FALSE(page.has_value());
}

NOLINT_TEST_F(VsmPageRequestGenerationTest,
  SharedMapProjectionRoutesProduceDistinctGlobalPageRequests)
{
  const auto face0_view = glm::lookAtRH(glm::vec3 { 0.0F, 0.0F, 0.0F },
    glm::vec3 { 0.0F, 0.0F, -1.0F }, glm::vec3 { 0.0F, 1.0F, 0.0F });
  const auto face1_view = glm::lookAtRH(glm::vec3 { 0.0F, 0.0F, 0.0F },
    glm::vec3 { 1.0F, 0.0F, 0.0F }, glm::vec3 { 0.0F, 1.0F, 0.0F });

  const auto requests = BuildPageRequests(
    std::array {
      MakeProjection(7U, 100U, 1U, 0U, kVsmInvalidLightIndex, 8U, 4U, 4U, 4U,
        0U, 0U, 0U, face0_view),
      MakeProjection(7U, 100U, 1U, 0U, kVsmInvalidLightIndex, 8U, 4U, 4U, 4U,
        4U, 0U, 1U, face1_view),
    },
    std::array {
      VsmVisiblePixelSample { .world_position_ws = { 0.0F, 0.0F, -5.0F } },
      VsmVisiblePixelSample { .world_position_ws = { 5.0F, 0.0F, 0.0F } },
    },
    VsmPageRequestGenerationOptions {
      .enable_coarse_pages = false,
      .enable_light_grid_pruning = true,
    });

  ASSERT_THAT(requests, ::testing::SizeIs(2));
  EXPECT_EQ(requests[0].map_id, 7U);
  EXPECT_EQ(requests[0].page.level, 0U);
  EXPECT_EQ(requests[0].page.page_x, 2U);
  EXPECT_EQ(requests[0].page.page_y, 2U);
  EXPECT_EQ(requests[1].map_id, 7U);
  EXPECT_EQ(requests[1].page.level, 0U);
  EXPECT_EQ(requests[1].page.page_x, 6U);
  EXPECT_EQ(requests[1].page.page_y, 2U);
}

} // namespace
