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

using oxygen::renderer::vsm::VsmCacheManager;
using oxygen::renderer::vsm::VsmCacheManagerFrameConfig;
using oxygen::renderer::vsm::VsmHzbPoolChangeResult;
using oxygen::renderer::vsm::VsmPhysicalPagePoolManager;
using oxygen::renderer::vsm::VsmPhysicalPoolChangeResult;
using oxygen::renderer::vsm::testing::LocalStageLightSpec;
using oxygen::renderer::vsm::testing::VsmStageCpuHarness;

class VsmProjectionRecordPublicationTest : public VsmStageCpuHarness { };

NOLINT_TEST_F(VsmProjectionRecordPublicationTest,
  PublishesMultiPageProjectionRecordsIntoCommittedCurrentFrame)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(pool_manager.EnsureShadowPool(
              MakeShadowPoolConfig("vsm-projection-records.shadow")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(
    pool_manager.EnsureHzbPool(MakeHzbPoolConfig("vsm-projection-records.hzb")),
    VsmHzbPoolChangeResult::kCreated);

  constexpr auto kLights = std::array {
    LocalStageLightSpec {
      .remap_key = "hero-local",
      .level_count = 2U,
      .pages_per_level_x = 2U,
      .pages_per_level_y = 2U,
    },
    LocalStageLightSpec {
      .remap_key = "rim-local",
      .level_count = 2U,
      .pages_per_level_x = 2U,
      .pages_per_level_y = 2U,
    },
  };
  const auto frame_shape
    = MakeLocalFrame(21ULL, 80U, kLights, "vsm-projection-records.frame");

  auto manager = VsmCacheManager(nullptr);
  manager.BeginFrame(MakeSeam(pool_manager, frame_shape),
    VsmCacheManagerFrameConfig { .debug_name = "vsm-projection-records" });
  static_cast<void>(manager.BuildPageAllocationPlan());
  static_cast<void>(manager.CommitPageAllocationFrame());

  const auto projection_records = std::array {
    MakeProjectionRecord(frame_shape.local_light_layouts[0].id,
      frame_shape.local_light_layouts[0].first_page_table_entry, 2U, 2U, 2U),
    MakeProjectionRecord(frame_shape.local_light_layouts[1].id,
      frame_shape.local_light_layouts[1].first_page_table_entry, 2U, 2U, 2U),
  };
  manager.PublishProjectionRecords(projection_records);

  ASSERT_NE(manager.GetCurrentFrame(), nullptr);
  ASSERT_EQ(manager.GetCurrentFrame()->snapshot.projection_records.size(), 2U);
  EXPECT_EQ(manager.GetCurrentFrame()->snapshot.projection_records[0],
    projection_records[0]);
  EXPECT_EQ(manager.GetCurrentFrame()->snapshot.projection_records[1],
    projection_records[1]);
}

} // namespace
