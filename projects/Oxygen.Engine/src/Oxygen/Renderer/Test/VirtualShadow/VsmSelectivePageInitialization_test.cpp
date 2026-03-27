//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Renderer/Passes/Vsm/VsmPageInitializationPass.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManager.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmPhysicalPageAddressing.h>

#include "VirtualShadowStageGpuHarness.h"

namespace {

using oxygen::renderer::vsm::TryConvertToCoord;
using oxygen::renderer::vsm::VsmCacheInvalidationReason;
using oxygen::renderer::vsm::VsmCacheInvalidationScope;
using oxygen::renderer::vsm::VsmCacheManager;
using oxygen::renderer::vsm::VsmCacheManagerFrameConfig;
using oxygen::renderer::vsm::VsmPageAllocationFrame;
using oxygen::renderer::vsm::VsmPageInitializationAction;
using oxygen::renderer::vsm::VsmPageRequest;
using oxygen::renderer::vsm::VsmPhysicalPageIndex;
using oxygen::renderer::vsm::VsmVirtualPageCoord;
using oxygen::renderer::vsm::testing::VsmStageGpuHarness;

class VsmSelectivePageInitializationTest : public VsmStageGpuHarness { };

[[nodiscard]] auto MakePagedRequests(const std::uint32_t map_id)
  -> std::array<VsmPageRequest, 2>
{
  return std::array {
    VsmPageRequest {
      .map_id = map_id,
      .page = VsmVirtualPageCoord { .level = 0U, .page_x = 0U, .page_y = 0U },
    },
    VsmPageRequest {
      .map_id = map_id,
      .page = VsmVirtualPageCoord { .level = 0U, .page_x = 1U, .page_y = 0U },
    },
  };
}

[[nodiscard]] auto FindUntouchedPhysicalPage(
  const VsmPageAllocationFrame& frame) -> VsmPhysicalPageIndex
{
  for (std::uint32_t candidate = 0U;; ++candidate) {
    auto used = false;
    for (const auto& work_item : frame.plan.initialization_work) {
      if (work_item.physical_page.value == candidate) {
        used = true;
        break;
      }
    }
    if (!used) {
      return VsmPhysicalPageIndex { .value = candidate };
    }
  }
}

NOLINT_TEST_F(VsmSelectivePageInitializationTest,
  ClearsOnlyTargetedDynamicPagesWithoutTouchingNeighbors)
{
  auto pool_manager
    = oxygen::renderer::vsm::VsmPhysicalPagePoolManager(&Backend());
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(oxygen::renderer::vsm::VsmPhysicalPoolConfig {
      .page_size_texels = 128U,
      .physical_tile_capacity = 256U,
      .array_slice_count = 1U,
      .depth_format = oxygen::Format::kDepth32,
      .slice_roles
      = { oxygen::renderer::vsm::VsmPhysicalPoolSliceRole::kDynamicDepth },
      .debug_name = "vsm-selective-page-initialization.clear-shadow",
    }),
    oxygen::renderer::vsm::VsmPhysicalPoolChangeResult::kCreated);

  auto manager = VsmCacheManager(&Backend());
  const auto virtual_frame = MakeSinglePageLocalFrame(
    1ULL, 10U, "vsm-selective-page-initialization.clear");
  const auto request = VsmPageRequest {
    .map_id = virtual_frame.local_light_layouts[0].id,
    .page = {},
  };

  manager.BeginFrame(MakeSeam(pool_manager, virtual_frame),
    VsmCacheManagerFrameConfig {
      .debug_name = "vsm-selective-page-initialization.clear" });
  manager.SetPageRequests({ &request, 1U });
  const auto& frame = CommitFrame(manager);

  ASSERT_EQ(frame.plan.initialization_work.size(), 1U);
  EXPECT_EQ(frame.plan.initialization_work[0].action,
    VsmPageInitializationAction::kClearDepth);

  const auto shadow_pool = pool_manager.GetShadowPoolSnapshot();
  ASSERT_NE(shadow_pool.shadow_texture, nullptr);
  const auto coord = TryConvertToCoord(
    frame.plan.initialization_work[0].physical_page, shadow_pool.tile_capacity,
    shadow_pool.tiles_per_axis, shadow_pool.slice_count);
  ASSERT_TRUE(coord.has_value());
  const auto neighbor_tile_x
    = (coord->tile_x + 1U) % shadow_pool.tiles_per_axis;

  SeedShadowPageValue(shadow_pool.shadow_texture, shadow_pool.page_size_texels,
    coord->tile_x, coord->tile_y, 0U, 0.25F,
    "vsm-selective-page-initialization.clear-target");
  SeedShadowPageValue(shadow_pool.shadow_texture, shadow_pool.page_size_texels,
    neighbor_tile_x, coord->tile_y, 0U, 0.25F,
    "vsm-selective-page-initialization.clear-neighbor");

  ExecuteInitializationPass(
    oxygen::engine::VsmPageInitializationPassInput {
      .frame = frame, .physical_pool = shadow_pool },
    "vsm-selective-page-initialization.clear-execute");

  const auto target_x = coord->tile_x * shadow_pool.page_size_texels
    + shadow_pool.page_size_texels / 2U;
  const auto target_y = coord->tile_y * shadow_pool.page_size_texels
    + shadow_pool.page_size_texels / 2U;
  const auto neighbor_x = neighbor_tile_x * shadow_pool.page_size_texels
    + shadow_pool.page_size_texels / 2U;

  EXPECT_FLOAT_EQ(
    ReadShadowDepthTexel(shadow_pool.shadow_texture, target_x, target_y, 0U,
      "vsm-selective-page-initialization.clear-target-readback"),
    1.0F);
  EXPECT_FLOAT_EQ(
    ReadShadowDepthTexel(shadow_pool.shadow_texture, neighbor_x, target_y, 0U,
      "vsm-selective-page-initialization.clear-neighbor-readback"),
    0.25F);
}

NOLINT_TEST_F(VsmSelectivePageInitializationTest,
  CopiesStaticSliceIntoDynamicSliceWhenOnlyDynamicStateWasInvalidated)
{
  auto pool_manager
    = oxygen::renderer::vsm::VsmPhysicalPagePoolManager(&Backend());
  ASSERT_EQ(pool_manager.EnsureShadowPool(MakeShadowPoolConfig(
              "vsm-selective-page-initialization.copy-shadow")),
    oxygen::renderer::vsm::VsmPhysicalPoolChangeResult::kCreated);

  auto manager = VsmCacheManager(&Backend());
  const auto previous_frame = MakeSinglePageLocalFrame(
    1ULL, 10U, "vsm-selective-page-initialization.copy-prev");
  const auto previous_request = VsmPageRequest {
    .map_id = previous_frame.local_light_layouts[0].id,
    .page = {},
  };

  manager.BeginFrame(MakeSeam(pool_manager, previous_frame),
    VsmCacheManagerFrameConfig {
      .debug_name = "vsm-selective-page-initialization.copy-prev" });
  manager.SetPageRequests({ &previous_request, 1U });
  static_cast<void>(CommitFrame(manager));
  manager.ExtractFrameData();

  manager.InvalidateLocalLights({ "local-0" },
    VsmCacheInvalidationScope::kDynamicOnly,
    VsmCacheInvalidationReason::kTargetedInvalidate);

  const auto current_frame = MakeSinglePageLocalFrame(
    2ULL, 20U, "vsm-selective-page-initialization.copy-current");
  const auto current_request = VsmPageRequest {
    .map_id = current_frame.local_light_layouts[0].id,
    .page = {},
  };

  manager.BeginFrame(MakeSeam(pool_manager, current_frame, &previous_frame),
    VsmCacheManagerFrameConfig {
      .debug_name = "vsm-selective-page-initialization.copy-current" });
  manager.SetPageRequests({ &current_request, 1U });
  const auto& frame = CommitFrame(manager);

  ASSERT_EQ(frame.plan.initialization_work.size(), 1U);
  EXPECT_EQ(frame.plan.initialization_work[0].action,
    VsmPageInitializationAction::kCopyStaticSlice);

  const auto shadow_pool = pool_manager.GetShadowPoolSnapshot();
  ASSERT_NE(shadow_pool.shadow_texture, nullptr);
  const auto coord = TryConvertToCoord(
    frame.plan.initialization_work[0].physical_page, shadow_pool.tile_capacity,
    shadow_pool.tiles_per_axis, shadow_pool.slice_count);
  ASSERT_TRUE(coord.has_value());
  const auto neighbor_tile_x
    = (coord->tile_x + 1U) % shadow_pool.tiles_per_axis;

  SeedShadowPageValue(shadow_pool.shadow_texture, shadow_pool.page_size_texels,
    coord->tile_x, coord->tile_y, 0U, 0.20F,
    "vsm-selective-page-initialization.copy-dynamic-target");
  SeedShadowPageValue(shadow_pool.shadow_texture, shadow_pool.page_size_texels,
    neighbor_tile_x, coord->tile_y, 0U, 0.20F,
    "vsm-selective-page-initialization.copy-dynamic-neighbor");
  SeedShadowPageValue(shadow_pool.shadow_texture, shadow_pool.page_size_texels,
    coord->tile_x, coord->tile_y, 1U, 0.75F,
    "vsm-selective-page-initialization.copy-static-target");

  ExecuteInitializationPass(
    oxygen::engine::VsmPageInitializationPassInput {
      .frame = frame, .physical_pool = shadow_pool },
    "vsm-selective-page-initialization.copy-execute");

  const auto target_x = coord->tile_x * shadow_pool.page_size_texels
    + shadow_pool.page_size_texels / 2U;
  const auto target_y = coord->tile_y * shadow_pool.page_size_texels
    + shadow_pool.page_size_texels / 2U;
  const auto neighbor_x = neighbor_tile_x * shadow_pool.page_size_texels
    + shadow_pool.page_size_texels / 2U;

  EXPECT_FLOAT_EQ(
    ReadShadowDepthTexel(shadow_pool.shadow_texture, target_x, target_y, 0U,
      "vsm-selective-page-initialization.copy-target-readback"),
    0.75F);
  EXPECT_FLOAT_EQ(
    ReadShadowDepthTexel(shadow_pool.shadow_texture, neighbor_x, target_y, 0U,
      "vsm-selective-page-initialization.copy-neighbor-readback"),
    0.20F);
}

NOLINT_TEST_F(VsmSelectivePageInitializationTest,
  ClearsMultipleRequestedPagesForPagedLightsWithoutTouchingUntargetedPages)
{
  auto pool_manager
    = oxygen::renderer::vsm::VsmPhysicalPagePoolManager(&Backend());
  ASSERT_EQ(pool_manager.EnsureShadowPool(MakeSingleSliceShadowPoolConfig(128U,
              256U, "vsm-selective-page-initialization.multi-clear-shadow")),
    oxygen::renderer::vsm::VsmPhysicalPoolChangeResult::kCreated);

  auto manager = VsmCacheManager(&Backend());
  const auto virtual_frame = MakeMultiLevelLocalFrame(1ULL, 10U, "hero", 1U, 2U,
    1U, "vsm-selective-page-initialization.multi-clear");
  const auto requests
    = MakePagedRequests(virtual_frame.local_light_layouts[0].id);

  manager.BeginFrame(MakeSeam(pool_manager, virtual_frame),
    VsmCacheManagerFrameConfig {
      .debug_name = "vsm-selective-page-initialization.multi-clear",
    });
  manager.SetPageRequests(requests);
  const auto& frame = CommitFrame(manager);

  ASSERT_EQ(frame.plan.initialization_work.size(), requests.size());
  EXPECT_EQ(frame.plan.initialization_work[0].action,
    VsmPageInitializationAction::kClearDepth);
  EXPECT_EQ(frame.plan.initialization_work[1].action,
    VsmPageInitializationAction::kClearDepth);

  const auto shadow_pool = pool_manager.GetShadowPoolSnapshot();
  ASSERT_NE(shadow_pool.shadow_texture, nullptr);

  std::array<oxygen::renderer::vsm::VsmPhysicalPageCoord, 2> target_coords {};
  for (std::size_t i = 0; i < frame.plan.initialization_work.size(); ++i) {
    const auto coord
      = TryConvertToCoord(frame.plan.initialization_work[i].physical_page,
        shadow_pool.tile_capacity, shadow_pool.tiles_per_axis,
        shadow_pool.slice_count);
    ASSERT_TRUE(coord.has_value());
    target_coords[i] = *coord;
    SeedShadowPageValue(shadow_pool.shadow_texture,
      shadow_pool.page_size_texels, coord->tile_x, coord->tile_y, 0U, 0.25F,
      i == 0U ? "vsm-selective-page-initialization.multi-clear.target0"
              : "vsm-selective-page-initialization.multi-clear.target1");
  }

  const auto untouched_physical_page = FindUntouchedPhysicalPage(frame);
  const auto untouched_coord
    = TryConvertToCoord(untouched_physical_page, shadow_pool.tile_capacity,
      shadow_pool.tiles_per_axis, shadow_pool.slice_count);
  ASSERT_TRUE(untouched_coord.has_value());
  SeedShadowPageValue(shadow_pool.shadow_texture, shadow_pool.page_size_texels,
    untouched_coord->tile_x, untouched_coord->tile_y, 0U, 0.25F,
    "vsm-selective-page-initialization.multi-clear.untouched");

  ExecuteInitializationPass(
    oxygen::engine::VsmPageInitializationPassInput {
      .frame = frame,
      .physical_pool = shadow_pool,
    },
    "vsm-selective-page-initialization.multi-clear-execute");

  for (std::size_t i = 0; i < target_coords.size(); ++i) {
    const auto center_x = target_coords[i].tile_x * shadow_pool.page_size_texels
      + shadow_pool.page_size_texels / 2U;
    const auto center_y = target_coords[i].tile_y * shadow_pool.page_size_texels
      + shadow_pool.page_size_texels / 2U;
    EXPECT_FLOAT_EQ(
      ReadShadowDepthTexel(shadow_pool.shadow_texture, center_x, center_y, 0U,
        i == 0U
          ? "vsm-selective-page-initialization.multi-clear.target0-readback"
          : "vsm-selective-page-initialization.multi-clear.target1-readback"),
      1.0F);
  }

  const auto untouched_x
    = untouched_coord->tile_x * shadow_pool.page_size_texels
    + shadow_pool.page_size_texels / 2U;
  const auto untouched_y
    = untouched_coord->tile_y * shadow_pool.page_size_texels
    + shadow_pool.page_size_texels / 2U;
  EXPECT_FLOAT_EQ(
    ReadShadowDepthTexel(shadow_pool.shadow_texture, untouched_x, untouched_y,
      0U, "vsm-selective-page-initialization.multi-clear.untouched-readback"),
    0.25F);
}

NOLINT_TEST_F(VsmSelectivePageInitializationTest,
  CopiesStaticSliceIntoMultipleDynamicPagesAfterDynamicOnlyInvalidation)
{
  auto pool_manager
    = oxygen::renderer::vsm::VsmPhysicalPagePoolManager(&Backend());
  ASSERT_EQ(pool_manager.EnsureShadowPool(MakeShadowPoolConfig(
              "vsm-selective-page-initialization.multi-copy-shadow")),
    oxygen::renderer::vsm::VsmPhysicalPoolChangeResult::kCreated);

  auto manager = VsmCacheManager(&Backend());
  const auto previous_frame = MakeMultiLevelLocalFrame(1ULL, 10U, "hero", 1U,
    2U, 1U, "vsm-selective-page-initialization.multi-copy-prev");
  const auto previous_requests
    = MakePagedRequests(previous_frame.local_light_layouts[0].id);

  manager.BeginFrame(MakeSeam(pool_manager, previous_frame),
    VsmCacheManagerFrameConfig {
      .debug_name = "vsm-selective-page-initialization.multi-copy-prev",
    });
  manager.SetPageRequests(previous_requests);
  static_cast<void>(CommitFrame(manager));
  manager.ExtractFrameData();

  manager.InvalidateLocalLights({ "hero" },
    VsmCacheInvalidationScope::kDynamicOnly,
    VsmCacheInvalidationReason::kTargetedInvalidate);

  const auto current_frame = MakeMultiLevelLocalFrame(2ULL, 20U, "hero", 1U, 2U,
    1U, "vsm-selective-page-initialization.multi-copy-current");
  const auto current_requests
    = MakePagedRequests(current_frame.local_light_layouts[0].id);

  manager.BeginFrame(MakeSeam(pool_manager, current_frame, &previous_frame),
    VsmCacheManagerFrameConfig {
      .debug_name = "vsm-selective-page-initialization.multi-copy-current",
    });
  manager.SetPageRequests(current_requests);
  const auto& frame = CommitFrame(manager);

  ASSERT_EQ(frame.plan.initialization_work.size(), current_requests.size());
  EXPECT_EQ(frame.plan.initialization_work[0].action,
    VsmPageInitializationAction::kCopyStaticSlice);
  EXPECT_EQ(frame.plan.initialization_work[1].action,
    VsmPageInitializationAction::kCopyStaticSlice);

  const auto shadow_pool = pool_manager.GetShadowPoolSnapshot();
  ASSERT_NE(shadow_pool.shadow_texture, nullptr);

  constexpr std::array<float, 2> kDynamicSeeds { 0.20F, 0.30F };
  constexpr std::array<float, 2> kStaticSeeds { 0.75F, 0.65F };

  for (std::size_t i = 0; i < frame.plan.initialization_work.size(); ++i) {
    const auto coord
      = TryConvertToCoord(frame.plan.initialization_work[i].physical_page,
        shadow_pool.tile_capacity, shadow_pool.tiles_per_axis,
        shadow_pool.slice_count);
    ASSERT_TRUE(coord.has_value());
    SeedShadowPageValue(shadow_pool.shadow_texture,
      shadow_pool.page_size_texels, coord->tile_x, coord->tile_y, 0U,
      kDynamicSeeds[i],
      i == 0U ? "vsm-selective-page-initialization.multi-copy.dynamic0"
              : "vsm-selective-page-initialization.multi-copy.dynamic1");
    SeedShadowPageValue(shadow_pool.shadow_texture,
      shadow_pool.page_size_texels, coord->tile_x, coord->tile_y, 1U,
      kStaticSeeds[i],
      i == 0U ? "vsm-selective-page-initialization.multi-copy.static0"
              : "vsm-selective-page-initialization.multi-copy.static1");
  }

  const auto untouched_physical_page = FindUntouchedPhysicalPage(frame);
  const auto untouched_coord
    = TryConvertToCoord(untouched_physical_page, shadow_pool.tile_capacity,
      shadow_pool.tiles_per_axis, shadow_pool.slice_count);
  ASSERT_TRUE(untouched_coord.has_value());
  SeedShadowPageValue(shadow_pool.shadow_texture, shadow_pool.page_size_texels,
    untouched_coord->tile_x, untouched_coord->tile_y, 0U, 0.10F,
    "vsm-selective-page-initialization.multi-copy.untouched");

  ExecuteInitializationPass(
    oxygen::engine::VsmPageInitializationPassInput {
      .frame = frame,
      .physical_pool = shadow_pool,
    },
    "vsm-selective-page-initialization.multi-copy-execute");

  for (std::size_t i = 0; i < frame.plan.initialization_work.size(); ++i) {
    const auto coord
      = TryConvertToCoord(frame.plan.initialization_work[i].physical_page,
        shadow_pool.tile_capacity, shadow_pool.tiles_per_axis,
        shadow_pool.slice_count);
    ASSERT_TRUE(coord.has_value());
    const auto center_x = coord->tile_x * shadow_pool.page_size_texels
      + shadow_pool.page_size_texels / 2U;
    const auto center_y = coord->tile_y * shadow_pool.page_size_texels
      + shadow_pool.page_size_texels / 2U;
    EXPECT_FLOAT_EQ(
      ReadShadowDepthTexel(shadow_pool.shadow_texture, center_x, center_y, 0U,
        i == 0U
          ? "vsm-selective-page-initialization.multi-copy.dynamic0-readback"
          : "vsm-selective-page-initialization.multi-copy.dynamic1-readback"),
      kStaticSeeds[i]);
  }

  const auto untouched_x
    = untouched_coord->tile_x * shadow_pool.page_size_texels
    + shadow_pool.page_size_texels / 2U;
  const auto untouched_y
    = untouched_coord->tile_y * shadow_pool.page_size_texels
    + shadow_pool.page_size_texels / 2U;
  EXPECT_FLOAT_EQ(
    ReadShadowDepthTexel(shadow_pool.shadow_texture, untouched_x, untouched_y,
      0U, "vsm-selective-page-initialization.multi-copy.untouched-readback"),
    0.10F);
}

} // namespace
