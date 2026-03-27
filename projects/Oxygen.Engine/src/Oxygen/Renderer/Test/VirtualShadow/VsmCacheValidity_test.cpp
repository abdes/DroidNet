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

using oxygen::renderer::vsm::VsmAllocationAction;
using oxygen::renderer::vsm::VsmCacheDataState;
using oxygen::renderer::vsm::VsmCacheManager;
using oxygen::renderer::vsm::VsmCacheManagerFrameConfig;
using oxygen::renderer::vsm::VsmPageRequest;
using oxygen::renderer::vsm::testing::LocalStageLightSpec;
using oxygen::renderer::vsm::testing::VsmStageCpuHarness;

class VsmCacheValidityTest : public VsmStageCpuHarness { };

NOLINT_TEST_F(VsmCacheValidityTest,
  BecomesAvailableOnlyAfterExtractionAndEnablesNextFrameReuse)
{
  auto pool_manager
    = oxygen::renderer::vsm::VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(pool_manager.EnsureShadowPool(
              MakeShadowPoolConfig("vsm-cache-validity.shadow")),
    oxygen::renderer::vsm::VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(
    pool_manager.EnsureHzbPool(MakeHzbPoolConfig("vsm-cache-validity.hzb")),
    oxygen::renderer::vsm::VsmHzbPoolChangeResult::kCreated);

  constexpr auto kPreviousLights = std::array {
    LocalStageLightSpec {
      .remap_key = "hero-local",
      .level_count = 2U,
      .pages_per_level_x = 2U,
      .pages_per_level_y = 2U,
    },
  };
  const auto previous_frame = MakeLocalFrame(
    41ULL, 160U, kPreviousLights, "vsm-cache-validity.previous");
  const auto previous_request = MakeSinglePageRequest(previous_frame);

  auto manager = VsmCacheManager(nullptr);
  manager.BeginFrame(MakeSeam(pool_manager, previous_frame),
    VsmCacheManagerFrameConfig { .debug_name = "vsm-cache-validity.previous" });
  manager.SetPageRequests(std::array { previous_request });
  static_cast<void>(manager.BuildPageAllocationPlan());
  static_cast<void>(manager.CommitPageAllocationFrame());

  EXPECT_EQ(manager.DescribeCacheDataState(), VsmCacheDataState::kUnavailable);
  EXPECT_FALSE(manager.IsCacheDataAvailable());

  manager.ExtractFrameData();

  EXPECT_EQ(manager.DescribeCacheDataState(), VsmCacheDataState::kAvailable);
  EXPECT_TRUE(manager.IsCacheDataAvailable());
  ASSERT_NE(manager.GetPreviousFrame(), nullptr);

  constexpr auto kCurrentLights = std::array {
    LocalStageLightSpec {
      .remap_key = "hero-local",
      .level_count = 2U,
      .pages_per_level_x = 2U,
      .pages_per_level_y = 2U,
    },
  };
  const auto current_frame
    = MakeLocalFrame(42ULL, 220U, kCurrentLights, "vsm-cache-validity.current");
  const auto current_request = VsmPageRequest {
    .map_id = current_frame.local_light_layouts[0].id,
    .page = {},
  };
  manager.BeginFrame(MakeSeam(pool_manager, current_frame, &previous_frame),
    VsmCacheManagerFrameConfig { .debug_name = "vsm-cache-validity.current" });
  manager.SetPageRequests(std::array { current_request });

  const auto& plan = manager.BuildPageAllocationPlan();
  ASSERT_EQ(plan.decisions.size(), 1U);
  EXPECT_EQ(plan.decisions[0].action, VsmAllocationAction::kInitializeOnly);
  EXPECT_EQ(plan.reused_page_count, 1U);
  EXPECT_EQ(plan.allocated_page_count, 0U);
}

} // namespace
