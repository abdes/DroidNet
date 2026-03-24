//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include "VirtualShadowTestFixtures.h"

#include <array>
#include <span>
#include <string_view>

#include <Oxygen/Renderer/VirtualShadowMaps/VsmPageAllocationPlanner.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualRemapBuilder.h>

namespace {

using oxygen::renderer::vsm::BuildVirtualRemapTable;
using oxygen::renderer::vsm::PageCountPerLevel;
using oxygen::renderer::vsm::TryGetPageTableEntryIndex;
using oxygen::renderer::vsm::VsmAllocationAction;
using oxygen::renderer::vsm::VsmAllocationFailureReason;
using oxygen::renderer::vsm::VsmCacheDataState;
using oxygen::renderer::vsm::VsmCacheManagerSeam;
using oxygen::renderer::vsm::VsmExtractedCacheFrame;
using oxygen::renderer::vsm::VsmHzbPoolChangeResult;
using oxygen::renderer::vsm::VsmLocalLightDesc;
using oxygen::renderer::vsm::VsmPageAllocationPlanner;
using oxygen::renderer::vsm::VsmPageInitializationAction;
using oxygen::renderer::vsm::VsmPageRequest;
using oxygen::renderer::vsm::VsmPageRequestFlags;
using oxygen::renderer::vsm::VsmPhysicalPageIndex;
using oxygen::renderer::vsm::VsmPhysicalPagePoolManager;
using oxygen::renderer::vsm::VsmPhysicalPoolChangeResult;
using oxygen::renderer::vsm::VsmSinglePageLightDesc;
using oxygen::renderer::vsm::VsmVirtualAddressSpace;
using oxygen::renderer::vsm::VsmVirtualAddressSpaceConfig;
using oxygen::renderer::vsm::VsmVirtualAddressSpaceFrame;
using oxygen::renderer::vsm::VsmVirtualMapLayout;
using oxygen::renderer::vsm::VsmVirtualPageCoord;
using oxygen::renderer::vsm::VsmVirtualShadowMapId;
using oxygen::renderer::vsm::testing::VsmPhysicalPoolTestBase;

struct LocalLightSpec {
  std::string_view remap_key {};
  std::uint32_t level_count { 1 };
  std::uint32_t pages_per_level_x { 1 };
  std::uint32_t pages_per_level_y { 1 };
};

class VsmPageAllocationPlannerTest : public VsmPhysicalPoolTestBase {
protected:
  static auto MakeLocalFrame(const std::uint64_t frame_generation,
    const std::uint32_t first_virtual_id, std::span<const LocalLightSpec> specs,
    const char* debug_name) -> VsmVirtualAddressSpaceFrame
  {
    auto address_space = VsmVirtualAddressSpace {};
    address_space.BeginFrame(
      VsmVirtualAddressSpaceConfig {
        .first_virtual_id = first_virtual_id,
        .debug_name = debug_name,
      },
      frame_generation);

    for (const auto& spec : specs) {
      if (spec.level_count == 1 && spec.pages_per_level_x == 1
        && spec.pages_per_level_y == 1) {
        address_space.AllocateSinglePageLocalLight(VsmSinglePageLightDesc {
          .remap_key = std::string(spec.remap_key),
          .debug_name = std::string(spec.remap_key),
        });
      } else {
        address_space.AllocatePagedLocalLight(VsmLocalLightDesc {
          .remap_key = std::string(spec.remap_key),
          .level_count = spec.level_count,
          .pages_per_level_x = spec.pages_per_level_x,
          .pages_per_level_y = spec.pages_per_level_y,
          .debug_name = std::string(spec.remap_key),
        });
      }
    }

    return address_space.DescribeFrame();
  }

  static auto MakeSeam(VsmPhysicalPagePoolManager& pool_manager,
    const VsmVirtualAddressSpaceFrame& current_frame,
    const VsmVirtualAddressSpaceFrame* previous_frame = nullptr)
    -> VsmCacheManagerSeam
  {
    return VsmCacheManagerSeam {
      .physical_pool = pool_manager.GetShadowPoolSnapshot(),
      .hzb_pool = pool_manager.GetHzbPoolSnapshot(),
      .current_frame = current_frame,
      .previous_to_current_remap = previous_frame != nullptr
        ? BuildVirtualRemapTable(*previous_frame, current_frame)
        : oxygen::renderer::vsm::VsmVirtualRemapTable {},
    };
  }

  static auto MakeSnapshot(VsmPhysicalPagePoolManager& pool_manager,
    const VsmVirtualAddressSpaceFrame& frame) -> VsmExtractedCacheFrame
  {
    const auto shadow_pool = pool_manager.GetShadowPoolSnapshot();
    return VsmExtractedCacheFrame {
      .frame_generation = frame.frame_generation,
      .pool_identity = shadow_pool.pool_identity,
      .pool_page_size_texels = shadow_pool.page_size_texels,
      .pool_tile_capacity = shadow_pool.tile_capacity,
      .pool_slice_count = shadow_pool.slice_count,
      .pool_depth_format = shadow_pool.depth_format,
      .pool_slice_roles = shadow_pool.slice_roles,
      .is_hzb_data_available = pool_manager.GetHzbPoolSnapshot().is_available,
      .virtual_frame = frame,
      .light_cache_entries = {},
      .page_table = std::vector<oxygen::renderer::vsm::VsmPageTableEntry>(
        frame.total_page_table_entry_count),
      .physical_pages = std::vector<oxygen::renderer::vsm::VsmPhysicalPageMeta>(
        shadow_pool.tile_capacity),
    };
  }

  static auto ResolvePageTableEntryIndex(
    const VsmVirtualAddressSpaceFrame& frame,
    const VsmVirtualShadowMapId map_id, const VsmVirtualPageCoord& page)
    -> std::uint32_t
  {
    for (const auto& layout : frame.local_light_layouts) {
      if (layout.id != map_id) {
        continue;
      }
      const auto index = TryGetPageTableEntryIndex(layout, page);
      EXPECT_TRUE(index.has_value());
      return *index;
    }

    ADD_FAILURE()
      << "page lookup only supports local-light layouts in this fixture";
    return 0U;
  }

  static auto AssignPreviousMapping(VsmExtractedCacheFrame& snapshot,
    const VsmVirtualShadowMapId map_id, const VsmVirtualPageCoord& page,
    const VsmPhysicalPageIndex physical_page) -> void
  {
    const auto entry_index
      = ResolvePageTableEntryIndex(snapshot.virtual_frame, map_id, page);
    snapshot.page_table[entry_index]
      = { .is_mapped = true, .physical_page = physical_page };
    snapshot.physical_pages[physical_page.value] = {
      .is_allocated = true,
      .is_dirty = false,
      .used_this_frame = false,
      .view_uncached = false,
      .static_invalidated = false,
      .dynamic_invalidated = false,
      .age = 1,
      .owner_id = map_id,
      .owner_mip_level = page.level,
      .owner_page = page,
      .last_touched_frame = snapshot.frame_generation,
    };
  }

  static auto BuildPlannerResult(const VsmCacheManagerSeam& seam,
    const VsmExtractedCacheFrame* previous_frame,
    std::span<const VsmPageRequest> requests,
    const VsmCacheDataState cache_data_state = VsmCacheDataState::kAvailable)
    -> VsmPageAllocationPlanner::Result
  {
    const auto planner = VsmPageAllocationPlanner {};
    return planner.Build(seam, previous_frame, cache_data_state, requests);
  }
};

NOLINT_TEST_F(VsmPageAllocationPlannerTest,
  PlannerKeepsReusableMappingsWhenRemapAndOwnershipStayValid)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("phase3-shadow")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("phase3-hzb")),
    VsmHzbPoolChangeResult::kCreated);

  constexpr auto previous_specs
    = std::array { LocalLightSpec { .remap_key = "hero" } };
  constexpr auto current_specs
    = std::array { LocalLightSpec { .remap_key = "hero" } };
  const auto previous_frame = MakeLocalFrame(1ULL, 10U, previous_specs, "prev");
  const auto current_frame = MakeLocalFrame(2ULL, 20U, current_specs, "curr");

  auto previous_snapshot = MakeSnapshot(pool_manager, previous_frame);
  AssignPreviousMapping(previous_snapshot,
    previous_frame.local_light_layouts[0].id, VsmVirtualPageCoord {},
    VsmPhysicalPageIndex { .value = 3 });

  const auto seam = MakeSeam(pool_manager, current_frame, &previous_frame);
  const auto request = VsmPageRequest {
    .map_id = current_frame.local_light_layouts[0].id,
    .page = {},
    .flags = VsmPageRequestFlags::kRequired,
  };

  const auto result
    = BuildPlannerResult(seam, &previous_snapshot, std::span { &request, 1U });

  ASSERT_EQ(result.plan.decisions.size(), 1U);
  EXPECT_EQ(
    result.plan.decisions[0].action, VsmAllocationAction::kReuseExisting);
  EXPECT_EQ(result.plan.decisions[0].previous_physical_page,
    VsmPhysicalPageIndex { .value = 3 });
  EXPECT_EQ(result.plan.decisions[0].current_physical_page,
    VsmPhysicalPageIndex { .value = 3 });
  EXPECT_EQ(result.plan.reused_page_count, 1U);
  EXPECT_EQ(result.plan.allocated_page_count, 0U);
  EXPECT_EQ(result.plan.initialized_page_count, 0U);
  EXPECT_EQ(result.plan.evicted_page_count, 0U);
  EXPECT_EQ(result.plan.rejected_page_count, 0U);
  EXPECT_TRUE(result.plan.initialization_work.empty());

  const auto entry_index
    = ResolvePageTableEntryIndex(current_frame, request.map_id, request.page);
  EXPECT_TRUE(result.snapshot.page_table[entry_index].is_mapped);
  EXPECT_EQ(result.snapshot.page_table[entry_index].physical_page.value, 3U);
  EXPECT_EQ(result.snapshot.physical_pages[3].owner_id, request.map_id);
}

NOLINT_TEST_F(VsmPageAllocationPlannerTest,
  PlannerDoesNotReuseInvalidatedEntriesAndAllocatesFreshPagesDeterministically)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("phase3-shadow")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("phase3-hzb")),
    VsmHzbPoolChangeResult::kCreated);

  constexpr auto specs = std::array { LocalLightSpec { .remap_key = "hero" } };
  const auto previous_frame = MakeLocalFrame(1ULL, 10U, specs, "prev");
  const auto current_frame = MakeLocalFrame(2ULL, 20U, specs, "curr");

  auto previous_snapshot = MakeSnapshot(pool_manager, previous_frame);
  AssignPreviousMapping(previous_snapshot,
    previous_frame.local_light_layouts[0].id, VsmVirtualPageCoord {},
    VsmPhysicalPageIndex { .value = 3 });
  previous_snapshot.physical_pages[3].dynamic_invalidated = true;

  const auto seam = MakeSeam(pool_manager, current_frame, &previous_frame);
  const auto request = VsmPageRequest {
    .map_id = current_frame.local_light_layouts[0].id,
    .page = {},
    .flags = VsmPageRequestFlags::kRequired,
  };

  const auto result
    = BuildPlannerResult(seam, &previous_snapshot, std::span { &request, 1U });

  ASSERT_EQ(result.plan.decisions.size(), 2U);
  EXPECT_EQ(result.plan.decisions[0].action, VsmAllocationAction::kAllocateNew);
  EXPECT_EQ(result.plan.decisions[0].current_physical_page.value, 0U);
  EXPECT_EQ(result.plan.decisions[1].action, VsmAllocationAction::kEvict);
  EXPECT_EQ(result.plan.decisions[1].previous_physical_page.value, 3U);
  EXPECT_EQ(result.plan.reused_page_count, 0U);
  EXPECT_EQ(result.plan.allocated_page_count, 1U);
  EXPECT_EQ(result.plan.initialized_page_count, 1U);
  EXPECT_EQ(result.plan.evicted_page_count, 1U);
  EXPECT_EQ(result.plan.rejected_page_count, 0U);
  ASSERT_EQ(result.plan.initialization_work.size(), 1U);
  EXPECT_EQ(result.plan.initialization_work[0].physical_page.value, 0U);
  EXPECT_EQ(result.plan.initialization_work[0].action,
    VsmPageInitializationAction::kCopyStaticSlice);
}

NOLINT_TEST_F(VsmPageAllocationPlannerTest,
  PlannerRejectsRequestsWhenRemapCompatibilityExplicitlyFails)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("phase3-shadow")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("phase3-hzb")),
    VsmHzbPoolChangeResult::kCreated);

  constexpr auto previous_specs
    = std::array { LocalLightSpec { .remap_key = "hero", .level_count = 1 } };
  constexpr auto current_specs = std::array {
    LocalLightSpec { .remap_key = "hero",
      .level_count = 2,
      .pages_per_level_x = 2,
      .pages_per_level_y = 2 },
  };
  const auto previous_frame = MakeLocalFrame(1ULL, 10U, previous_specs, "prev");
  const auto current_frame = MakeLocalFrame(2ULL, 20U, current_specs, "curr");

  auto previous_snapshot = MakeSnapshot(pool_manager, previous_frame);
  AssignPreviousMapping(previous_snapshot,
    previous_frame.local_light_layouts[0].id, VsmVirtualPageCoord {},
    VsmPhysicalPageIndex { .value = 1 });

  const auto seam = MakeSeam(pool_manager, current_frame, &previous_frame);
  const auto request = VsmPageRequest {
    .map_id = current_frame.local_light_layouts[0].id,
    .page = {},
    .flags = VsmPageRequestFlags::kRequired,
  };

  const auto result
    = BuildPlannerResult(seam, &previous_snapshot, std::span { &request, 1U });

  ASSERT_EQ(result.plan.decisions.size(), 2U);
  EXPECT_EQ(result.plan.decisions[0].action, VsmAllocationAction::kReject);
  EXPECT_EQ(result.plan.decisions[0].failure_reason,
    VsmAllocationFailureReason::kRemapRejected);
  EXPECT_EQ(result.plan.decisions[1].action, VsmAllocationAction::kEvict);
  EXPECT_EQ(result.plan.reused_page_count, 0U);
  EXPECT_EQ(result.plan.allocated_page_count, 0U);
  EXPECT_EQ(result.plan.evicted_page_count, 1U);
  EXPECT_EQ(result.plan.rejected_page_count, 1U);
}

NOLINT_TEST_F(VsmPageAllocationPlannerTest,
  PlannerConsumesAvailablePhysicalPagesDeterministicallyOnColdStart)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  auto pool_config = MakeShadowPoolConfig("phase3-shadow");
  pool_config.physical_tile_capacity = 4;
  pool_config.array_slice_count = 1;
  pool_config.slice_roles
    = { oxygen::renderer::vsm::VsmPhysicalPoolSliceRole::kDynamicDepth };
  ASSERT_EQ(pool_manager.EnsureShadowPool(pool_config),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("phase3-hzb")),
    VsmHzbPoolChangeResult::kCreated);

  constexpr auto specs = std::array {
    LocalLightSpec { .remap_key = "hero-a" },
    LocalLightSpec { .remap_key = "hero-b" },
  };
  const auto current_frame = MakeLocalFrame(1ULL, 30U, specs, "curr");
  const auto seam = MakeSeam(pool_manager, current_frame);
  const auto requests = std::array {
    VsmPageRequest {
      .map_id = current_frame.local_light_layouts[0].id,
      .page = {},
      .flags = VsmPageRequestFlags::kRequired,
    },
    VsmPageRequest {
      .map_id = current_frame.local_light_layouts[1].id,
      .page = {},
      .flags = VsmPageRequestFlags::kRequired,
    },
  };

  const auto result = BuildPlannerResult(
    seam, nullptr, requests, VsmCacheDataState::kUnavailable);

  ASSERT_EQ(result.plan.decisions.size(), 2U);
  EXPECT_EQ(result.plan.decisions[0].action, VsmAllocationAction::kAllocateNew);
  EXPECT_EQ(result.plan.decisions[0].current_physical_page.value, 0U);
  EXPECT_EQ(result.plan.decisions[1].action, VsmAllocationAction::kAllocateNew);
  EXPECT_EQ(result.plan.decisions[1].current_physical_page.value, 1U);
  EXPECT_EQ(result.plan.allocated_page_count, 2U);
  EXPECT_EQ(result.plan.initialized_page_count, 2U);
  ASSERT_EQ(result.plan.initialization_work.size(), 2U);
  EXPECT_EQ(result.plan.initialization_work[0].action,
    VsmPageInitializationAction::kClearDepth);
  EXPECT_EQ(result.plan.initialization_work[1].action,
    VsmPageInitializationAction::kClearDepth);
}

NOLINT_TEST_F(
  VsmPageAllocationPlannerTest, PlannerReportsAllocationExhaustionExplicitly)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  auto pool_config = MakeShadowPoolConfig("phase3-shadow");
  pool_config.physical_tile_capacity = 1;
  pool_config.array_slice_count = 1;
  pool_config.slice_roles
    = { oxygen::renderer::vsm::VsmPhysicalPoolSliceRole::kDynamicDepth };
  ASSERT_EQ(pool_manager.EnsureShadowPool(pool_config),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("phase3-hzb")),
    VsmHzbPoolChangeResult::kCreated);

  constexpr auto specs = std::array {
    LocalLightSpec { .remap_key = "hero-a" },
    LocalLightSpec { .remap_key = "hero-b" },
  };
  const auto current_frame = MakeLocalFrame(1ULL, 30U, specs, "curr");
  const auto seam = MakeSeam(pool_manager, current_frame);
  const auto requests = std::array {
    VsmPageRequest {
      .map_id = current_frame.local_light_layouts[0].id,
      .page = {},
      .flags = VsmPageRequestFlags::kRequired,
    },
    VsmPageRequest {
      .map_id = current_frame.local_light_layouts[1].id,
      .page = {},
      .flags = VsmPageRequestFlags::kRequired,
    },
  };

  const auto result = BuildPlannerResult(
    seam, nullptr, requests, VsmCacheDataState::kUnavailable);

  ASSERT_EQ(result.plan.decisions.size(), 2U);
  EXPECT_EQ(result.plan.decisions[0].action, VsmAllocationAction::kAllocateNew);
  EXPECT_EQ(result.plan.decisions[1].action, VsmAllocationAction::kReject);
  EXPECT_EQ(result.plan.decisions[1].failure_reason,
    VsmAllocationFailureReason::kNoAvailablePhysicalPages);
  EXPECT_EQ(result.plan.allocated_page_count, 1U);
  EXPECT_EQ(result.plan.rejected_page_count, 1U);
}

NOLINT_TEST_F(VsmPageAllocationPlannerTest,
  PlannerNeverMapsOnePreviousPageToMultipleCurrentRequests)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("phase3-shadow")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("phase3-hzb")),
    VsmHzbPoolChangeResult::kCreated);

  constexpr auto specs = std::array { LocalLightSpec { .remap_key = "hero" } };
  const auto previous_frame = MakeLocalFrame(1ULL, 10U, specs, "prev");
  const auto current_frame = MakeLocalFrame(2ULL, 20U, specs, "curr");

  auto previous_snapshot = MakeSnapshot(pool_manager, previous_frame);
  AssignPreviousMapping(previous_snapshot,
    previous_frame.local_light_layouts[0].id, VsmVirtualPageCoord {},
    VsmPhysicalPageIndex { .value = 2 });

  const auto seam = MakeSeam(pool_manager, current_frame, &previous_frame);
  const auto duplicate_request = VsmPageRequest {
    .map_id = current_frame.local_light_layouts[0].id,
    .page = {},
    .flags = VsmPageRequestFlags::kRequired,
  };
  const auto requests = std::array { duplicate_request, duplicate_request };

  const auto result = BuildPlannerResult(seam, &previous_snapshot, requests);

  ASSERT_EQ(result.plan.decisions.size(), 2U);
  EXPECT_EQ(
    result.plan.decisions[0].action, VsmAllocationAction::kReuseExisting);
  EXPECT_EQ(result.plan.decisions[1].action, VsmAllocationAction::kReject);
  EXPECT_EQ(result.plan.decisions[1].failure_reason,
    VsmAllocationFailureReason::kInvalidRequest);
  EXPECT_EQ(result.plan.reused_page_count, 1U);
  EXPECT_EQ(result.plan.rejected_page_count, 1U);
}

NOLINT_TEST_F(
  VsmPageAllocationPlannerTest, PlannerOutputIsDeterministicAcrossRepeatedRuns)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("phase3-shadow")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("phase3-hzb")),
    VsmHzbPoolChangeResult::kCreated);

  constexpr auto previous_specs
    = std::array { LocalLightSpec { .remap_key = "hero-a" } };
  constexpr auto current_specs = std::array {
    LocalLightSpec { .remap_key = "hero-a" },
    LocalLightSpec { .remap_key = "hero-b" },
  };
  const auto previous_frame = MakeLocalFrame(1ULL, 10U, previous_specs, "prev");
  const auto current_frame = MakeLocalFrame(2ULL, 20U, current_specs, "curr");

  auto previous_snapshot = MakeSnapshot(pool_manager, previous_frame);
  AssignPreviousMapping(previous_snapshot,
    previous_frame.local_light_layouts[0].id, VsmVirtualPageCoord {},
    VsmPhysicalPageIndex { .value = 3 });

  const auto seam = MakeSeam(pool_manager, current_frame, &previous_frame);
  const auto requests = std::array {
    VsmPageRequest {
      .map_id = current_frame.local_light_layouts[0].id,
      .page = {},
      .flags = VsmPageRequestFlags::kRequired,
    },
    VsmPageRequest {
      .map_id = current_frame.local_light_layouts[1].id,
      .page = {},
      .flags = VsmPageRequestFlags::kRequired,
    },
  };

  const auto first = BuildPlannerResult(seam, &previous_snapshot, requests);
  const auto second = BuildPlannerResult(seam, &previous_snapshot, requests);

  EXPECT_EQ(first, second);
}

NOLINT_TEST_F(VsmPageAllocationPlannerTest,
  PlannerUsesClearDepthInitializationWhenOnlyStaticContentIsInvalidated)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("phase3-shadow")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("phase3-hzb")),
    VsmHzbPoolChangeResult::kCreated);

  constexpr auto specs = std::array { LocalLightSpec { .remap_key = "hero" } };
  const auto previous_frame = MakeLocalFrame(1ULL, 10U, specs, "prev");
  const auto current_frame = MakeLocalFrame(2ULL, 20U, specs, "curr");

  auto previous_snapshot = MakeSnapshot(pool_manager, previous_frame);
  AssignPreviousMapping(previous_snapshot,
    previous_frame.local_light_layouts[0].id, VsmVirtualPageCoord {},
    VsmPhysicalPageIndex { .value = 2 });
  previous_snapshot.physical_pages[2].static_invalidated = true;

  const auto seam = MakeSeam(pool_manager, current_frame, &previous_frame);
  const auto request = VsmPageRequest {
    .map_id = current_frame.local_light_layouts[0].id,
    .page = {},
    .flags = VsmPageRequestFlags::kStaticOnly,
  };

  const auto result
    = BuildPlannerResult(seam, &previous_snapshot, std::span { &request, 1U });

  ASSERT_EQ(result.plan.initialization_work.size(), 1U);
  EXPECT_EQ(result.plan.initialization_work[0].physical_page.value, 0U);
  EXPECT_EQ(result.plan.initialization_work[0].action,
    VsmPageInitializationAction::kClearDepth);
}

} // namespace
