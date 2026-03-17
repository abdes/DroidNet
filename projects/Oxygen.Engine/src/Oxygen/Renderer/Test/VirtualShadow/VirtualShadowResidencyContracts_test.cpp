//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Renderer/Internal/ShadowBackendCommon.h>
#include <Oxygen/Renderer/Types/VirtualShadowPhysicalPageMetadata.h>

namespace {

using oxygen::renderer::internal::shadow_detail::
  CompareVirtualResidentEvictionPriority;
using oxygen::renderer::internal::shadow_detail::PackVirtualResidentPageKey;

TEST(VirtualShadowResidencyContractsTest, PhysicalPageContractsRemainGpuFriendly)
{
  EXPECT_EQ(sizeof(oxygen::renderer::VirtualShadowPhysicalPageMetadata) % 16U,
    0U);
  EXPECT_EQ(
    sizeof(oxygen::renderer::VirtualShadowPhysicalPageListEntry) % 16U, 0U);
}

TEST(VirtualShadowResidencyContractsTest,
  EvictionPriorityOrdersInvalidThenCoarserThenLruThenKey)
{
  const auto fine_old = PackVirtualResidentPageKey(1U, 10, 10);
  const auto fine_new = PackVirtualResidentPageKey(1U, 11, 10);
  const auto coarse_old = PackVirtualResidentPageKey(3U, 4, 4);
  const auto coarse_invalid = PackVirtualResidentPageKey(3U, 5, 4);

  EXPECT_TRUE(CompareVirtualResidentEvictionPriority(coarse_invalid, false, 9U,
    coarse_old, true, 1U));
  EXPECT_TRUE(CompareVirtualResidentEvictionPriority(
    coarse_old, true, 1U, fine_old, true, 1U));
  EXPECT_TRUE(CompareVirtualResidentEvictionPriority(
    fine_old, true, 1U, fine_new, true, 2U));
  EXPECT_TRUE(CompareVirtualResidentEvictionPriority(
    fine_old, true, 1U, fine_new, true, 1U));
  EXPECT_FALSE(CompareVirtualResidentEvictionPriority(
    fine_new, true, 1U, fine_old, true, 1U));
}

} // namespace
