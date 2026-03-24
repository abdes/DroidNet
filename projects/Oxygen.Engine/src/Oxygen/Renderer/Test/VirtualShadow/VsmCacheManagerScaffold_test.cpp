//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <type_traits>

#include <Oxygen/Testing/GTest.h>

#include "VirtualShadowTestFixtures.h"

#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManager.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmPhysicalPagePoolManager.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualAddressSpace.h>

namespace {

using oxygen::Format;
using oxygen::renderer::vsm::VsmCacheManager;
using oxygen::renderer::vsm::VsmCacheManagerConfig;
using oxygen::renderer::vsm::VsmCacheManagerFrameConfig;
using oxygen::renderer::vsm::VsmDirectionalClipmapDesc;
using oxygen::renderer::vsm::VsmHzbPoolConfig;
using oxygen::renderer::vsm::VsmPhysicalPagePoolManager;
using oxygen::renderer::vsm::VsmPhysicalPoolConfig;
using oxygen::renderer::vsm::VsmPhysicalPoolSliceRole;
using oxygen::renderer::vsm::VsmSinglePageLightDesc;
using oxygen::renderer::vsm::VsmVirtualAddressSpace;
using oxygen::renderer::vsm::VsmVirtualAddressSpaceConfig;
using oxygen::renderer::vsm::testing::VsmPhysicalPoolTestBase;

class VsmCacheManagerScaffoldTest : public VsmPhysicalPoolTestBase { };

NOLINT_TEST_F(
  VsmCacheManagerScaffoldTest, CacheManagerScaffoldConsumesTheSeamDirectly)
{
  static_assert(std::is_constructible_v<VsmCacheManager, oxygen::Graphics*,
    const VsmCacheManagerConfig&>);
  static_assert(!std::is_copy_constructible_v<VsmCacheManager>);
  static_assert(!std::is_move_constructible_v<VsmCacheManager>);

  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  const auto pool_config = MakeShadowPoolConfig("phase0-shadow-pool");
  const auto hzb_config = MakeHzbPoolConfig("phase0-hzb-pool");

  ASSERT_EQ(pool_manager.EnsureShadowPool(pool_config),
    oxygen::renderer::vsm::VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(hzb_config),
    oxygen::renderer::vsm::VsmHzbPoolChangeResult::kCreated);

  auto address_space = VsmVirtualAddressSpace {};
  address_space.BeginFrame(
    VsmVirtualAddressSpaceConfig {
      .first_virtual_id = 32,
      .debug_name = "phase0-current",
    },
    7ULL);
  address_space.AllocateSinglePageLocalLight(
    VsmSinglePageLightDesc { .remap_key = "local-a", .debug_name = "local-a" });
  address_space.AllocateDirectionalClipmap(VsmDirectionalClipmapDesc {
    .remap_key = "sun",
    .clip_level_count = 2,
    .pages_per_axis = 4,
    .page_grid_origin = { { 0, 0 }, { 4, 4 } },
    .page_world_size = { 32.0F, 64.0F },
    .near_depth = { 1.0F, 2.0F },
    .far_depth = { 101.0F, 202.0F },
    .debug_name = "sun",
  });

  const auto seam = oxygen::renderer::vsm::VsmCacheManagerSeam {
    .physical_pool = pool_manager.GetShadowPoolSnapshot(),
    .hzb_pool = pool_manager.GetHzbPoolSnapshot(),
    .current_frame = address_space.DescribeFrame(),
    .previous_to_current_remap = {},
  };

  auto cache_manager = VsmCacheManager(
    nullptr, VsmCacheManagerConfig { .debug_name = "phase0-cache-manager" });
  cache_manager.BeginFrame(seam,
    VsmCacheManagerFrameConfig {
      .allow_reuse = true,
      .force_invalidate_all = false,
      .debug_name = "phase0-frame",
    });
  cache_manager.Reset();
  cache_manager.Reset();

  SUCCEED();
}

} // namespace
