//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManager.h>

#include "VirtualShadowStageCpuHarness.h"

namespace {

using oxygen::renderer::vsm::VsmCacheBuildState;
using oxygen::renderer::vsm::VsmCacheDataState;
using oxygen::renderer::vsm::VsmCacheManager;
using oxygen::renderer::vsm::VsmCacheManagerFrameConfig;
using oxygen::renderer::vsm::VsmPrimitiveIdentity;
using oxygen::renderer::vsm::testing::LocalStageLightSpec;
using oxygen::renderer::vsm::testing::VsmStageCpuHarness;

class VsmFrameExtractionTest : public VsmStageCpuHarness { };

NOLINT_TEST_F(VsmFrameExtractionTest,
  ExtractsCommittedProductsIntoPreviousFrameSnapshotForReuse)
{
  auto pool_manager
    = oxygen::renderer::vsm::VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(pool_manager.EnsureShadowPool(
              MakeShadowPoolConfig("vsm-frame-extraction.shadow")),
    oxygen::renderer::vsm::VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(
    pool_manager.EnsureHzbPool(MakeHzbPoolConfig("vsm-frame-extraction.hzb")),
    oxygen::renderer::vsm::VsmHzbPoolChangeResult::kCreated);

  constexpr auto kLights = std::array {
    LocalStageLightSpec {
      .remap_key = "hero-local",
      .level_count = 2U,
      .pages_per_level_x = 2U,
      .pages_per_level_y = 2U,
    },
  };
  const auto frame_shape
    = MakeLocalFrame(31ULL, 120U, kLights, "vsm-frame-extraction.frame");
  const auto request = MakeSinglePageRequest(frame_shape);

  auto manager = VsmCacheManager(nullptr);
  manager.BeginFrame(MakeSeam(pool_manager, frame_shape),
    VsmCacheManagerFrameConfig { .debug_name = "vsm-frame-extraction" });
  manager.SetPageRequests(std::array { request });
  static_cast<void>(manager.BuildPageAllocationPlan());
  static_cast<void>(manager.CommitPageAllocationFrame());
  manager.PublishProjectionRecords(std::array {
    MakeProjectionRecord(frame_shape.local_light_layouts[0].id,
      frame_shape.local_light_layouts[0].first_page_table_entry, 2U, 2U, 2U),
  });
  manager.PublishVisibleShadowPrimitives(std::array {
    VsmPrimitiveIdentity {
      .transform_index = 9U,
      .transform_generation = 4U,
      .submesh_index = 1U,
      .primitive_flags = 7U,
    },
  });

  manager.ExtractFrameData();

  EXPECT_EQ(manager.DescribeBuildState(), VsmCacheBuildState::kIdle);
  EXPECT_EQ(manager.DescribeCacheDataState(), VsmCacheDataState::kAvailable);
  EXPECT_EQ(manager.GetCurrentFrame(), nullptr);
  ASSERT_NE(manager.GetPreviousFrame(), nullptr);
  EXPECT_EQ(
    manager.GetPreviousFrame()->frame_generation, frame_shape.frame_generation);
  EXPECT_EQ(manager.GetPreviousFrame()->virtual_frame, frame_shape);
  EXPECT_EQ(manager.GetPreviousFrame()->projection_records.size(), 1U);
  EXPECT_EQ(manager.GetPreviousFrame()->visible_shadow_primitives.size(), 1U);
  EXPECT_EQ(manager.GetPreviousFrame()->page_table.size(),
    frame_shape.total_page_table_entry_count);
}

} // namespace
