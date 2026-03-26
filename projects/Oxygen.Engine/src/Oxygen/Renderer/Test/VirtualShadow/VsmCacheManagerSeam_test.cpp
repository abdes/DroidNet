//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <type_traits>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManagerSeam.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmPhysicalPagePoolManager.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualAddressSpace.h>

#include "VirtualShadowTestFixtures.h"

namespace {

using oxygen::Format;
using oxygen::renderer::vsm::VsmCacheManagerSeam;
using oxygen::renderer::vsm::VsmDirectionalClipmapDesc;
using oxygen::renderer::vsm::VsmHzbPoolConfig;
using oxygen::renderer::vsm::VsmLocalLightDesc;
using oxygen::renderer::vsm::VsmPhysicalPagePoolManager;
using oxygen::renderer::vsm::VsmPhysicalPoolConfig;
using oxygen::renderer::vsm::VsmPhysicalPoolSliceRole;
using oxygen::renderer::vsm::VsmSinglePageLightDesc;
using oxygen::renderer::vsm::VsmVirtualAddressSpace;
using oxygen::renderer::vsm::VsmVirtualAddressSpaceConfig;
using oxygen::renderer::vsm::testing::VsmPhysicalPoolTestBase;

class VsmCacheManagerSeamTest : public VsmPhysicalPoolTestBase { };

NOLINT_TEST_F(
  VsmCacheManagerSeamTest, CacheManagerSeamExposesStablePoolAndFrameContracts)
{
  static_assert(std::is_copy_constructible_v<VsmCacheManagerSeam>);

  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  const auto pool_config = MakeShadowPoolConfig("phase6-shadow-pool");
  const auto hzb_config = MakeHzbPoolConfig("phase6-hzb-pool");
  ASSERT_TRUE(pool_manager.IsCompatible(pool_config)
    || !pool_manager.IsShadowPoolAvailable());
  ASSERT_EQ(pool_manager.EnsureShadowPool(pool_config),
    oxygen::renderer::vsm::VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(hzb_config),
    oxygen::renderer::vsm::VsmHzbPoolChangeResult::kCreated);

  auto previous_space = VsmVirtualAddressSpace {};
  previous_space.BeginFrame(
    VsmVirtualAddressSpaceConfig {
      .first_virtual_id = 10,
      .clipmap_reuse_config = {
        .max_page_offset_x = 4,
        .max_page_offset_y = 4,
        .depth_range_epsilon = 0.01F,
        .page_world_size_epsilon = 0.01F,
      },
      .debug_name = "phase6-prev",
    },
    1ULL);
  previous_space.AllocateSinglePageLocalLight(
    VsmSinglePageLightDesc { .remap_key = "local-a", .debug_name = "local-a" });
  previous_space.AllocateDirectionalClipmap(VsmDirectionalClipmapDesc {
    .remap_key = "sun",
    .clip_level_count = 2,
    .pages_per_axis = 4,
    .page_grid_origin = { { 0, 0 }, { 4, 4 } },
    .page_world_size = { 32.0F, 64.0F },
    .near_depth = { 1.0F, 2.0F },
    .far_depth = { 101.0F, 202.0F },
    .debug_name = "sun",
  });
  const auto previous_frame = previous_space.DescribeFrame();

  auto current_space = VsmVirtualAddressSpace {};
  current_space.BeginFrame(
    VsmVirtualAddressSpaceConfig {
      .first_virtual_id = 20,
      .clipmap_reuse_config = {
        .max_page_offset_x = 4,
        .max_page_offset_y = 4,
        .depth_range_epsilon = 0.01F,
        .page_world_size_epsilon = 0.01F,
      },
      .debug_name = "phase6-current",
    },
    2ULL);
  current_space.AllocateSinglePageLocalLight(
    VsmSinglePageLightDesc { .remap_key = "local-a", .debug_name = "local-a" });
  current_space.AllocateDirectionalClipmap(VsmDirectionalClipmapDesc {
    .remap_key = "sun",
    .clip_level_count = 2,
    .pages_per_axis = 4,
    .page_grid_origin = { { 1, -1 }, { 5, 3 } },
    .page_world_size = { 32.0F, 64.0F },
    .near_depth = { 1.0F, 2.0F },
    .far_depth = { 101.0F, 202.0F },
    .debug_name = "sun",
  });

  const auto current_frame = current_space.DescribeFrame();
  const auto remap = current_space.BuildRemapTable(previous_frame);
  const auto seam = VsmCacheManagerSeam {
    .physical_pool = pool_manager.GetShadowPoolSnapshot(),
    .hzb_pool = pool_manager.GetHzbPoolSnapshot(),
    .current_frame = current_frame,
    .previous_to_current_remap = remap,
  };

  EXPECT_TRUE(seam.physical_pool.is_available);
  EXPECT_EQ(seam.physical_pool.pool_identity, 1ULL);
  ASSERT_EQ(seam.physical_pool.slice_roles.size(), 2U);
  EXPECT_EQ(
    seam.physical_pool.slice_roles[0], VsmPhysicalPoolSliceRole::kDynamicDepth);
  EXPECT_EQ(
    seam.physical_pool.slice_roles[1], VsmPhysicalPoolSliceRole::kStaticDepth);

  EXPECT_TRUE(seam.hzb_pool.is_available);
  EXPECT_EQ(seam.hzb_pool.width, 1024U);
  EXPECT_EQ(seam.hzb_pool.height, 1024U);
  EXPECT_EQ(seam.hzb_pool.array_size, 1U);
  EXPECT_EQ(seam.current_frame.frame_generation, 2ULL);
  ASSERT_EQ(seam.current_frame.local_light_layouts.size(), 1U);
  EXPECT_EQ(seam.current_frame.local_light_layouts[0].remap_key, "local-a");

  const auto stored_previous_frame = previous_frame;
  EXPECT_EQ(stored_previous_frame.frame_generation, 1ULL);
  EXPECT_EQ(stored_previous_frame.directional_layouts[0].remap_key, "sun");

  ASSERT_EQ(seam.previous_to_current_remap.entries.size(), 3U);
  EXPECT_EQ(seam.previous_to_current_remap.entries[0].current_id, 20U);
}

} // namespace
