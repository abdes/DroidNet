//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Stage 1 — HZB Availability
//
// Covers the three states of IsHzbDataAvailable():
//   - cold start always yields false
//   - warm start after publishing HZB yields true
//   - warm start when the current seam has no HZB pool yields false even if the
//     previous frame had published HZB

#include <array>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManager.h>

#include "VirtualShadowStageCpuHarness.h"

namespace {

using oxygen::Format;
using oxygen::renderer::vsm::VsmCacheDataState;
using oxygen::renderer::vsm::VsmCacheManager;
using oxygen::renderer::vsm::VsmCacheManagerFrameConfig;
using oxygen::renderer::vsm::VsmHzbPoolChangeResult;
using oxygen::renderer::vsm::VsmPhysicalPagePoolManager;
using oxygen::renderer::vsm::VsmPhysicalPoolChangeResult;
using oxygen::renderer::vsm::testing::LocalStageLightSpec;
using oxygen::renderer::vsm::testing::VsmStageCpuHarness;

class VsmBeginFrameHzbAvailabilityTest : public VsmStageCpuHarness { };

// Cold start: no previous frame can supply HZB data, so IsHzbDataAvailable()
// must return false regardless of pool configuration.
NOLINT_TEST_F(VsmBeginFrameHzbAvailabilityTest, ColdStartReportsHzbUnavailable)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("hzb-cold.shadow")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("hzb-cold.hzb")),
    VsmHzbPoolChangeResult::kCreated);

  auto manager = VsmCacheManager(nullptr);
  manager.BeginFrame(MakeSeam(pool_manager, 1ULL, 10U, "hzb-cold"),
    VsmCacheManagerFrameConfig { .debug_name = "hzb-cold" });

  EXPECT_FALSE(manager.IsHzbDataAvailable());
}

// Warm start where HZB was marked available on the previous frame: the next
// BeginFrame with a live HZB pool must report IsHzbDataAvailable() == true.
NOLINT_TEST_F(VsmBeginFrameHzbAvailabilityTest,
  WarmStartWithPublishedHzbReportsHzbAvailable)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("hzb-warm.shadow")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("hzb-warm.hzb")),
    VsmHzbPoolChangeResult::kCreated);

  constexpr auto kLights
    = std::array { LocalStageLightSpec { .remap_key = "light-a" } };
  const auto prev_frame = MakeLocalFrame(1ULL, 40U, kLights, "hzb-warm.prev");
  const auto curr_frame = MakeLocalFrame(2ULL, 40U, kLights, "hzb-warm.curr");

  auto manager = VsmCacheManager(nullptr);
  manager.BeginFrame(MakeSeam(pool_manager, prev_frame),
    VsmCacheManagerFrameConfig { .debug_name = "hzb-warm.prev" });
  static_cast<void>(manager.BuildPageAllocationPlan());
  static_cast<void>(manager.CommitPageAllocationFrame());
  manager.PublishCurrentFrameHzbAvailability(true);
  manager.ExtractFrameData();

  manager.BeginFrame(MakeSeam(pool_manager, curr_frame, &prev_frame),
    VsmCacheManagerFrameConfig { .debug_name = "hzb-warm.curr" });

  EXPECT_TRUE(manager.IsCacheDataAvailable());
  EXPECT_TRUE(manager.IsHzbDataAvailable());
}

// Even when the previous frame had published HZB data, if the current seam
// carries no HZB pool then IsHzbDataAvailable() must return false.
NOLINT_TEST_F(
  VsmBeginFrameHzbAvailabilityTest, AbsentHzbPoolSuppressesHzbAvailability)
{
  auto pool_manager_with_hzb = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(pool_manager_with_hzb.EnsureShadowPool(
              MakeShadowPoolConfig("hzb-absent.shadow")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(
    pool_manager_with_hzb.EnsureHzbPool(MakeHzbPoolConfig("hzb-absent.hzb")),
    VsmHzbPoolChangeResult::kCreated);

  constexpr auto kLights
    = std::array { LocalStageLightSpec { .remap_key = "light-a" } };
  const auto prev_frame = MakeLocalFrame(1ULL, 40U, kLights, "hzb-absent.prev");
  const auto curr_frame = MakeLocalFrame(2ULL, 40U, kLights, "hzb-absent.curr");

  auto manager = VsmCacheManager(nullptr);
  manager.BeginFrame(MakeSeam(pool_manager_with_hzb, prev_frame),
    VsmCacheManagerFrameConfig { .debug_name = "hzb-absent.prev" });
  static_cast<void>(manager.BuildPageAllocationPlan());
  static_cast<void>(manager.CommitPageAllocationFrame());
  manager.PublishCurrentFrameHzbAvailability(true);
  manager.ExtractFrameData();

  // Next seam has no HZB pool — shadow pool only.
  auto no_hzb_pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(no_hzb_pool_manager.EnsureShadowPool(
              MakeShadowPoolConfig("hzb-absent.shadow")),
    VsmPhysicalPoolChangeResult::kCreated);

  manager.BeginFrame(MakeSeam(no_hzb_pool_manager, curr_frame, &prev_frame),
    VsmCacheManagerFrameConfig { .debug_name = "hzb-absent.curr" });

  EXPECT_EQ(manager.DescribeCacheDataState(), VsmCacheDataState::kAvailable);
  EXPECT_TRUE(manager.IsCacheDataAvailable());
  EXPECT_FALSE(manager.IsHzbDataAvailable());
}

// ── GAP-1: HZB pool geometry change suppresses HZB availability ──────────────
// These tests verify that ComputeHzbAvailability compares the HZB pool geometry
// captured at extraction time against the current seam. A change in any
// dimension or format must suppress HZB reuse even when the shadow pool is
// fully compatible and the previous frame published HZB as available.
//
// Test approach: use two separate pool managers seeded with the same shadow
// pool config (so pool_identity = 1 in both, passing the compatibility check).
// The second manager's HZB snapshot is then manually mutated on the seam to
// simulate the dimension/format change.

// Changing mip_count between frames suppresses HZB availability.
NOLINT_TEST_F(VsmBeginFrameHzbAvailabilityTest,
  HzbPoolMipCountChangeSuppressesHzbAvailability)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("hzb-mip.shadow")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("hzb-mip.hzb")),
    VsmHzbPoolChangeResult::kCreated);

  constexpr auto kLights
    = std::array { LocalStageLightSpec { .remap_key = "light-a" } };
  const auto prev_frame = MakeLocalFrame(1ULL, 40U, kLights, "hzb-mip.prev");
  const auto curr_frame = MakeLocalFrame(2ULL, 40U, kLights, "hzb-mip.curr");

  auto manager = VsmCacheManager(nullptr);
  manager.BeginFrame(MakeSeam(pool_manager, prev_frame),
    VsmCacheManagerFrameConfig { .debug_name = "hzb-mip.prev" });
  static_cast<void>(manager.BuildPageAllocationPlan());
  static_cast<void>(manager.CommitPageAllocationFrame());
  manager.PublishCurrentFrameHzbAvailability(true);
  manager.ExtractFrameData();

  // Simulate HZB pool rebuild with a different mip count.
  auto curr_seam = MakeSeam(pool_manager, curr_frame, &prev_frame);
  curr_seam.hzb_pool.mip_count -= 1U; // was 10 → now 9
  manager.BeginFrame(
    curr_seam, VsmCacheManagerFrameConfig { .debug_name = "hzb-mip.curr" });

  EXPECT_TRUE(manager.IsCacheDataAvailable());
  EXPECT_FALSE(manager.IsHzbDataAvailable());
}

// Changing format between frames suppresses HZB availability.
NOLINT_TEST_F(VsmBeginFrameHzbAvailabilityTest,
  HzbPoolFormatChangeSuppressesHzbAvailability)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("hzb-fmt.shadow")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("hzb-fmt.hzb")),
    VsmHzbPoolChangeResult::kCreated);

  constexpr auto kLights
    = std::array { LocalStageLightSpec { .remap_key = "light-a" } };
  const auto prev_frame = MakeLocalFrame(1ULL, 40U, kLights, "hzb-fmt.prev");
  const auto curr_frame = MakeLocalFrame(2ULL, 40U, kLights, "hzb-fmt.curr");

  auto manager = VsmCacheManager(nullptr);
  manager.BeginFrame(MakeSeam(pool_manager, prev_frame),
    VsmCacheManagerFrameConfig { .debug_name = "hzb-fmt.prev" });
  static_cast<void>(manager.BuildPageAllocationPlan());
  static_cast<void>(manager.CommitPageAllocationFrame());
  manager.PublishCurrentFrameHzbAvailability(true);
  manager.ExtractFrameData();

  // Simulate HZB pool rebuild with a different format (R32Float → R16Float).
  auto curr_seam = MakeSeam(pool_manager, curr_frame, &prev_frame);
  curr_seam.hzb_pool.format = oxygen::Format::kR16Float;
  manager.BeginFrame(
    curr_seam, VsmCacheManagerFrameConfig { .debug_name = "hzb-fmt.curr" });

  EXPECT_TRUE(manager.IsCacheDataAvailable());
  EXPECT_FALSE(manager.IsHzbDataAvailable());
}

// Changing pool extent (width or height) between frames suppresses HZB
// availability. Width/height are changed directly on the seam to isolate
// the geometry check from any shadow-pool compatibility check.
NOLINT_TEST_F(VsmBeginFrameHzbAvailabilityTest,
  HzbPoolExtentChangeSuppressesHzbAvailability)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("hzb-ext.shadow")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("hzb-ext.hzb")),
    VsmHzbPoolChangeResult::kCreated);

  constexpr auto kLights
    = std::array { LocalStageLightSpec { .remap_key = "light-a" } };
  const auto prev_frame = MakeLocalFrame(1ULL, 40U, kLights, "hzb-ext.prev");
  const auto curr_frame = MakeLocalFrame(2ULL, 40U, kLights, "hzb-ext.curr");

  auto manager = VsmCacheManager(nullptr);
  manager.BeginFrame(MakeSeam(pool_manager, prev_frame),
    VsmCacheManagerFrameConfig { .debug_name = "hzb-ext.prev" });
  static_cast<void>(manager.BuildPageAllocationPlan());
  static_cast<void>(manager.CommitPageAllocationFrame());
  manager.PublishCurrentFrameHzbAvailability(true);
  manager.ExtractFrameData();

  // Simulate HZB pool resize: double the width to represent a resolution scale
  // change that recreated the HZB pool at a larger extent.
  auto curr_seam = MakeSeam(pool_manager, curr_frame, &prev_frame);
  curr_seam.hzb_pool.width *= 2U;
  manager.BeginFrame(
    curr_seam, VsmCacheManagerFrameConfig { .debug_name = "hzb-ext.curr" });

  EXPECT_TRUE(manager.IsCacheDataAvailable());
  EXPECT_FALSE(manager.IsHzbDataAvailable());
}

} // namespace
