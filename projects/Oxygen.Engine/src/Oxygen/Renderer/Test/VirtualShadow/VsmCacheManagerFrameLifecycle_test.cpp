//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManager.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmShaderTypes.h>

#include "VirtualShadowTestFixtures.h"

namespace {

using oxygen::renderer::vsm::VsmCacheBuildState;
using oxygen::renderer::vsm::VsmCacheDataState;
using oxygen::renderer::vsm::VsmCacheManager;
using oxygen::renderer::vsm::VsmCacheManagerFrameConfig;
using oxygen::renderer::vsm::VsmPageRequestProjection;
using oxygen::renderer::vsm::VsmPhysicalPagePoolManager;
using oxygen::renderer::vsm::VsmPhysicalPoolChangeResult;
using oxygen::renderer::vsm::VsmPrimitiveIdentity;
using oxygen::renderer::vsm::VsmProjectionData;
using oxygen::renderer::vsm::VsmProjectionLightType;
using oxygen::renderer::vsm::VsmRenderedPrimitiveHistoryRecord;
using oxygen::renderer::vsm::VsmStaticPrimitivePageFeedbackRecord;
using oxygen::renderer::vsm::testing::VsmCacheManagerTestBase;

class VsmCacheManagerFrameLifecycleTest : public VsmCacheManagerTestBase { };

[[nodiscard]] static auto MakeProjectionRecord(const std::uint32_t map_id,
  const std::uint32_t first_page_table_entry) -> VsmPageRequestProjection
{
  return VsmPageRequestProjection {
    .projection = VsmProjectionData {
      .view_matrix = glm::mat4 { 1.0F },
      .projection_matrix = glm::mat4 { 1.0F },
      .view_origin_ws_pad = { 0.0F, 0.0F, 0.0F, 0.0F },
      .clipmap_corner_offset = { 0, 0 },
      .clipmap_level = 0U,
      .light_type = static_cast<std::uint32_t>(VsmProjectionLightType::kLocal),
    },
    .map_id = map_id,
    .first_page_table_entry = first_page_table_entry,
    .map_pages_x = 1U,
    .map_pages_y = 1U,
    .pages_x = 1U,
    .pages_y = 1U,
    .page_offset_x = 0U,
    .page_offset_y = 0U,
    .level_count = 1U,
    .coarse_level = 0U,
  };
}

NOLINT_TEST_F(VsmCacheManagerFrameLifecycleTest,
  CacheManagerCommitAndExtractPublishCanonicalSnapshotShape)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(pool_manager.EnsureShadowPool(
              MakeShadowPoolConfig("phase2-frame-shadow-pool")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(
    pool_manager.EnsureHzbPool(MakeHzbPoolConfig("phase2-frame-hzb-pool")),
    oxygen::renderer::vsm::VsmHzbPoolChangeResult::kCreated);

  const auto seam = MakeSeam(pool_manager, 7ULL, 32U, "phase2-frame", 2U);
  auto manager = VsmCacheManager(nullptr);
  manager.BeginFrame(
    seam, VsmCacheManagerFrameConfig { .debug_name = "phase2-commit" });

  const auto& plan = manager.BuildPageAllocationPlan();
  EXPECT_TRUE(plan.decisions.empty());
  EXPECT_EQ(manager.DescribeBuildState(), VsmCacheBuildState::kPlanned);

  const auto& frame = manager.CommitPageAllocationFrame();
  const auto committed_snapshot = frame.snapshot;
  EXPECT_EQ(manager.DescribeBuildState(), VsmCacheBuildState::kReady);
  EXPECT_TRUE(frame.is_ready);
  EXPECT_EQ(
    frame.snapshot.frame_generation, seam.current_frame.frame_generation);
  EXPECT_EQ(frame.snapshot.pool_identity, seam.physical_pool.pool_identity);
  EXPECT_EQ(frame.snapshot.virtual_frame, seam.current_frame);
  EXPECT_EQ(frame.snapshot.page_table.size(),
    seam.current_frame.total_page_table_entry_count);
  EXPECT_EQ(
    frame.snapshot.physical_pages.size(), seam.physical_pool.tile_capacity);
  EXPECT_EQ(frame.snapshot.light_cache_entries.size(), 2U);
  ASSERT_NE(manager.GetCurrentFrame(), nullptr);
  EXPECT_EQ(manager.GetCurrentFrame()->snapshot, committed_snapshot);

  manager.ExtractFrameData();
  EXPECT_EQ(manager.DescribeBuildState(), VsmCacheBuildState::kIdle);
  EXPECT_EQ(manager.DescribeCacheDataState(), VsmCacheDataState::kAvailable);
  EXPECT_TRUE(manager.IsCacheDataAvailable());
  EXPECT_FALSE(manager.IsHzbDataAvailable());
  EXPECT_EQ(manager.GetCurrentFrame(), nullptr);
  ASSERT_NE(manager.GetPreviousFrame(), nullptr);
  EXPECT_EQ(*manager.GetPreviousFrame(), committed_snapshot);
}

NOLINT_TEST_F(VsmCacheManagerFrameLifecycleTest,
  ProjectionRecordsArePublishedIntoCurrentFrameAndRetainedOnExtraction)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(pool_manager.EnsureShadowPool(
              MakeShadowPoolConfig("phase-i-frame-shadow-pool")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(
    pool_manager.EnsureHzbPool(MakeHzbPoolConfig("phase-i-frame-hzb-pool")),
    oxygen::renderer::vsm::VsmHzbPoolChangeResult::kCreated);

  const auto seam = MakeSeam(pool_manager, 11ULL, 48U, "phase-i-frame", 1U);
  auto manager = VsmCacheManager(nullptr);
  manager.BeginFrame(
    seam, VsmCacheManagerFrameConfig { .debug_name = "phase-i-commit" });

  static_cast<void>(manager.BuildPageAllocationPlan());
  const auto& frame = manager.CommitPageAllocationFrame();
  EXPECT_TRUE(frame.snapshot.projection_records.empty());

  const auto projection_records = std::array<VsmPageRequestProjection, 1U> {
    MakeProjectionRecord(48U, 0U),
  };
  manager.PublishProjectionRecords(projection_records);

  ASSERT_NE(manager.GetCurrentFrame(), nullptr);
  EXPECT_EQ(manager.GetCurrentFrame()->snapshot.projection_records.size(), 1U);
  EXPECT_EQ(manager.GetCurrentFrame()->snapshot.projection_records[0],
    projection_records[0]);

  manager.ExtractFrameData();

  ASSERT_NE(manager.GetPreviousFrame(), nullptr);
  EXPECT_EQ(manager.GetPreviousFrame()->projection_records.size(), 1U);
  EXPECT_EQ(
    manager.GetPreviousFrame()->projection_records[0], projection_records[0]);
}

NOLINT_TEST_F(VsmCacheManagerFrameLifecycleTest,
  RenderedPrimitiveHistoryAndStaticFeedbackArePublishedIntoCurrentFrameAndRetainedOnExtraction)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(pool_manager.EnsureShadowPool(
              MakeShadowPoolConfig("phase-j-frame-shadow-pool")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(
    pool_manager.EnsureHzbPool(MakeHzbPoolConfig("phase-j-frame-hzb-pool")),
    oxygen::renderer::vsm::VsmHzbPoolChangeResult::kCreated);

  const auto seam = MakeSeam(pool_manager, 12ULL, 60U, "phase-j-frame", 1U);
  auto manager = VsmCacheManager(nullptr);
  manager.BeginFrame(
    seam, VsmCacheManagerFrameConfig { .debug_name = "phase-j-commit" });

  static_cast<void>(manager.BuildPageAllocationPlan());
  const auto& frame = manager.CommitPageAllocationFrame();
  EXPECT_TRUE(frame.snapshot.rendered_primitive_history.empty());
  EXPECT_TRUE(frame.snapshot.static_primitive_page_feedback.empty());

  const auto rendered_history
    = std::array<VsmRenderedPrimitiveHistoryRecord, 1U> {
        VsmRenderedPrimitiveHistoryRecord {
          .primitive = VsmPrimitiveIdentity {
            .transform_index = 21U,
            .transform_generation = 3U,
            .submesh_index = 0U,
          },
          .map_id = seam.current_frame.local_light_layouts[0].id,
        },
      };
  const auto static_feedback
    = std::array<VsmStaticPrimitivePageFeedbackRecord, 1U> {
        VsmStaticPrimitivePageFeedbackRecord {
          .primitive = rendered_history[0].primitive,
          .page_table_index
          = seam.current_frame.local_light_layouts[0].first_page_table_entry,
          .physical_page = { 0U },
          .map_id = seam.current_frame.local_light_layouts[0].id,
          .virtual_page = {},
          .valid = 1U,
        },
      };

  manager.PublishRenderedPrimitiveHistory(rendered_history);
  manager.PublishStaticPrimitivePageFeedback(static_feedback);

  ASSERT_NE(manager.GetCurrentFrame(), nullptr);
  EXPECT_EQ(
    manager.GetCurrentFrame()->snapshot.rendered_primitive_history.size(), 1U);
  EXPECT_EQ(manager.GetCurrentFrame()->snapshot.rendered_primitive_history[0],
    rendered_history[0]);
  EXPECT_EQ(
    manager.GetCurrentFrame()->snapshot.static_primitive_page_feedback.size(),
    1U);
  EXPECT_EQ(
    manager.GetCurrentFrame()->snapshot.static_primitive_page_feedback[0],
    static_feedback[0]);

  manager.ExtractFrameData();

  ASSERT_NE(manager.GetPreviousFrame(), nullptr);
  EXPECT_EQ(manager.GetPreviousFrame()->rendered_primitive_history.size(), 1U);
  EXPECT_EQ(manager.GetPreviousFrame()->rendered_primitive_history[0],
    rendered_history[0]);
  EXPECT_EQ(
    manager.GetPreviousFrame()->static_primitive_page_feedback.size(), 1U);
  EXPECT_EQ(manager.GetPreviousFrame()->static_primitive_page_feedback[0],
    static_feedback[0]);
}

} // namespace
