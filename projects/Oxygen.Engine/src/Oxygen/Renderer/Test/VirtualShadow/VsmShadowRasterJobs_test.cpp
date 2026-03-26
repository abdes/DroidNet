// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>

#include <glm/mat4x4.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Renderer/VirtualShadowMaps/VsmShadowRasterJobs.h>

#include "VirtualShadowTestFixtures.h"

namespace {

using oxygen::Scissors;
using oxygen::renderer::vsm::BuildShadowRasterPageJobs;
using oxygen::renderer::vsm::VsmAllocationAction;
using oxygen::renderer::vsm::VsmPageAllocationDecision;
using oxygen::renderer::vsm::VsmPageAllocationFrame;
using oxygen::renderer::vsm::VsmPageAllocationPlan;
using oxygen::renderer::vsm::VsmPageAllocationSnapshot;
using oxygen::renderer::vsm::VsmPageRequest;
using oxygen::renderer::vsm::VsmPageRequestFlags;
using oxygen::renderer::vsm::VsmPageRequestProjection;
using oxygen::renderer::vsm::VsmPhysicalPageCoord;
using oxygen::renderer::vsm::VsmPhysicalPageIndex;
using oxygen::renderer::vsm::VsmPhysicalPoolSnapshot;
using oxygen::renderer::vsm::VsmProjectionData;
using oxygen::renderer::vsm::VsmProjectionLightType;
using oxygen::renderer::vsm::VsmVirtualPageCoord;
using oxygen::renderer::vsm::testing::VirtualShadowTest;

class VsmShadowRasterJobsTest : public VirtualShadowTest {
protected:
  [[nodiscard]] static auto MakePhysicalPool() -> VsmPhysicalPoolSnapshot
  {
    return VsmPhysicalPoolSnapshot {
      .page_size_texels = 128U,
      .tile_capacity = 512U,
      .tiles_per_axis = 16U,
      .slice_count = 2U,
      .is_available = true,
    };
  }

  [[nodiscard]] static auto MakeProjection(const std::uint32_t map_id,
    const std::uint32_t first_page_table_entry,
    const std::uint32_t pages_x = 2U, const std::uint32_t pages_y = 2U,
    const std::uint32_t level_count = 2U, const std::uint32_t map_pages_x = 0U,
    const std::uint32_t map_pages_y = 0U,
    const std::uint32_t page_offset_x = 0U,
    const std::uint32_t page_offset_y = 0U,
    const std::uint32_t cube_face_index
    = oxygen::renderer::vsm::kVsmInvalidCubeFaceIndex)
    -> VsmPageRequestProjection
  {
    return VsmPageRequestProjection {
      .projection = VsmProjectionData {
        .view_matrix = glm::mat4 { 1.0F },
        .projection_matrix = glm::mat4 { 1.0F },
        .view_origin_ws_pad = { 0.0F, 0.0F, 0.0F, 1.0F },
        .clipmap_corner_offset = { 0, 0 },
        .clipmap_level = 0U,
        .light_type = static_cast<std::uint32_t>(
          VsmProjectionLightType::kLocal),
      },
      .map_id = map_id,
      .first_page_table_entry = first_page_table_entry,
      .map_pages_x = map_pages_x == 0U ? pages_x : map_pages_x,
      .map_pages_y = map_pages_y == 0U ? pages_y : map_pages_y,
      .pages_x = pages_x,
      .pages_y = pages_y,
      .page_offset_x = page_offset_x,
      .page_offset_y = page_offset_y,
      .level_count = level_count,
      .coarse_level = level_count > 1U ? (level_count - 1U) : 0U,
      .cube_face_index = cube_face_index,
    };
  }

  [[nodiscard]] static auto MakeDecision(const std::uint32_t map_id,
    const VsmVirtualPageCoord& page, const VsmAllocationAction action,
    const std::uint32_t physical_page,
    const VsmPageRequestFlags flags = VsmPageRequestFlags::kRequired)
    -> VsmPageAllocationDecision
  {
    return VsmPageAllocationDecision {
      .request = VsmPageRequest {
        .map_id = map_id,
        .page = page,
        .flags = flags,
      },
      .action = action,
      .current_physical_page = VsmPhysicalPageIndex { .value = physical_page },
    };
  }
};

NOLINT_TEST_F(VsmShadowRasterJobsTest,
  BuildShadowRasterPageJobsBuildsDeterministicJobsForPreparedPages)
{
  auto frame = VsmPageAllocationFrame {};
  frame.snapshot = VsmPageAllocationSnapshot {};
  frame.plan = VsmPageAllocationPlan {
    .decisions
    = {
        MakeDecision(7U,
          VsmVirtualPageCoord { .level = 0U, .page_x = 1U, .page_y = 0U },
          VsmAllocationAction::kAllocateNew, 1U),
        MakeDecision(7U,
          VsmVirtualPageCoord { .level = 1U, .page_x = 0U, .page_y = 1U },
          VsmAllocationAction::kInitializeOnly, 18U,
          VsmPageRequestFlags::kRequired | VsmPageRequestFlags::kStaticOnly),
        MakeDecision(7U,
          VsmVirtualPageCoord { .level = 0U, .page_x = 0U, .page_y = 0U },
          VsmAllocationAction::kReuseExisting, 5U),
        MakeDecision(7U,
          VsmVirtualPageCoord { .level = 0U, .page_x = 0U, .page_y = 1U },
          VsmAllocationAction::kReject, 0U),
      },
  };
  frame.is_ready = true;

  const auto jobs = BuildShadowRasterPageJobs(
    frame, MakePhysicalPool(), std::array { MakeProjection(7U, 4U) });

  ASSERT_EQ(jobs.size(), 2U);

  EXPECT_EQ(jobs[0].page_table_index, 5U);
  EXPECT_EQ(jobs[0].map_id, 7U);
  EXPECT_EQ(jobs[0].virtual_page.level, 0U);
  EXPECT_EQ(jobs[0].physical_page.value, 1U);
  EXPECT_EQ(jobs[0].physical_coord,
    (VsmPhysicalPageCoord { .tile_x = 1U, .tile_y = 0U, .slice = 0U }));
  EXPECT_EQ(jobs[0].scissors.left, 128);
  EXPECT_EQ(jobs[0].scissors.top, 0);
  EXPECT_EQ(jobs[0].scissors.right, 256);
  EXPECT_EQ(jobs[0].scissors.bottom, 128);
  EXPECT_EQ(jobs[0].viewport.top_left_x, 128.0F);
  EXPECT_EQ(jobs[0].viewport.top_left_y, 0.0F);
  EXPECT_EQ(jobs[0].viewport.width, 128.0F);
  EXPECT_EQ(jobs[0].viewport.height, 128.0F);
  EXPECT_FALSE(jobs[0].static_only);
  EXPECT_EQ(jobs[0].projection_page.level, 0U);
  EXPECT_EQ(jobs[0].projection_page.page_x, 1U);
  EXPECT_EQ(jobs[0].projection_page.page_y, 0U);

  EXPECT_EQ(jobs[1].page_table_index, 10U);
  EXPECT_EQ(jobs[1].physical_page.value, 18U);
  EXPECT_EQ(jobs[1].physical_coord,
    (VsmPhysicalPageCoord { .tile_x = 2U, .tile_y = 1U, .slice = 0U }));
  EXPECT_EQ(jobs[1].scissors.left, 256);
  EXPECT_EQ(jobs[1].scissors.top, 128);
  EXPECT_EQ(jobs[1].scissors.right, 384);
  EXPECT_EQ(jobs[1].scissors.bottom, 256);
  EXPECT_TRUE(jobs[1].static_only);
  EXPECT_EQ(jobs[1].projection_page.level, 1U);
  EXPECT_EQ(jobs[1].projection_page.page_x, 0U);
  EXPECT_EQ(jobs[1].projection_page.page_y, 1U);
}

NOLINT_TEST_F(VsmShadowRasterJobsTest,
  BuildShadowRasterPageJobsSkipsPagesWithoutProjectionOrValidPhysicalCoord)
{
  auto frame = VsmPageAllocationFrame {};
  frame.snapshot = VsmPageAllocationSnapshot {};
  frame.plan = VsmPageAllocationPlan {
    .decisions
    = {
        MakeDecision(7U,
          VsmVirtualPageCoord { .level = 0U, .page_x = 0U, .page_y = 0U },
          VsmAllocationAction::kAllocateNew, 512U),
        MakeDecision(99U,
          VsmVirtualPageCoord { .level = 0U, .page_x = 0U, .page_y = 0U },
          VsmAllocationAction::kAllocateNew, 0U),
      },
  };
  frame.is_ready = true;

  const auto jobs = BuildShadowRasterPageJobs(frame, MakePhysicalPool(),
    std::array { MakeProjection(7U, 0U, 1U, 1U, 1U) });

  EXPECT_TRUE(jobs.empty());
}

NOLINT_TEST_F(VsmShadowRasterJobsTest,
  BuildShadowRasterPageJobsRoutesSharedMapPagesToMatchingFaceProjection)
{
  auto frame = VsmPageAllocationFrame {};
  frame.snapshot = VsmPageAllocationSnapshot {};
  frame.plan = VsmPageAllocationPlan {
    .decisions = {
      MakeDecision(7U,
        VsmVirtualPageCoord { .level = 0U, .page_x = 1U, .page_y = 0U },
        VsmAllocationAction::kAllocateNew, 3U),
    },
  };
  frame.is_ready = true;

  const auto jobs = BuildShadowRasterPageJobs(frame, MakePhysicalPool(),
    std::array {
      MakeProjection(7U, 4U, 1U, 1U, 1U, 2U, 1U, 0U, 0U, 0U),
      MakeProjection(7U, 4U, 1U, 1U, 1U, 2U, 1U, 1U, 0U, 1U),
    });

  ASSERT_EQ(jobs.size(), 1U);
  EXPECT_EQ(jobs[0].page_table_index, 5U);
  EXPECT_EQ(jobs[0].virtual_page,
    (VsmVirtualPageCoord { .level = 0U, .page_x = 1U, .page_y = 0U }));
  EXPECT_EQ(jobs[0].projection_page,
    (VsmVirtualPageCoord { .level = 0U, .page_x = 0U, .page_y = 0U }));
  EXPECT_EQ(jobs[0].projection.cube_face_index, 1U);
}

NOLINT_TEST_F(VsmShadowRasterJobsTest,
  BuildShadowRasterPageJobsRejectsOverlappingProjectionRoutes)
{
  auto frame = VsmPageAllocationFrame {};
  frame.snapshot = VsmPageAllocationSnapshot {};
  frame.plan = VsmPageAllocationPlan {
    .decisions = {
      MakeDecision(7U,
        VsmVirtualPageCoord { .level = 0U, .page_x = 0U, .page_y = 0U },
        VsmAllocationAction::kAllocateNew, 3U),
    },
  };
  frame.is_ready = true;

  const auto jobs = BuildShadowRasterPageJobs(frame, MakePhysicalPool(),
    std::array {
      MakeProjection(7U, 4U, 1U, 1U, 1U, 1U, 1U, 0U, 0U, 0U),
      MakeProjection(7U, 4U, 1U, 1U, 1U, 1U, 1U, 0U, 0U, 1U),
    });

  EXPECT_TRUE(jobs.empty());
}

NOLINT_TEST_F(
  VsmShadowRasterJobsTest, BuildShadowRasterPageJobsSkipsFramesThatAreNotReady)
{
  auto frame = VsmPageAllocationFrame {};
  frame.snapshot = VsmPageAllocationSnapshot {};
  frame.plan = VsmPageAllocationPlan {
    .decisions = {
      MakeDecision(7U,
        VsmVirtualPageCoord { .level = 0U, .page_x = 0U, .page_y = 0U },
        VsmAllocationAction::kAllocateNew, 0U),
    },
  };

  const auto jobs = BuildShadowRasterPageJobs(frame, MakePhysicalPool(),
    std::array { MakeProjection(7U, 0U, 1U, 1U, 1U) });

  EXPECT_TRUE(jobs.empty());
}

} // namespace
