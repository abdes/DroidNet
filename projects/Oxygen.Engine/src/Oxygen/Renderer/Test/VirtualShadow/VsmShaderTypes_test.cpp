//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include "VirtualShadowTestFixtures.h"

#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManagerTypes.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmShaderTypes.h>

namespace {

using oxygen::renderer::vsm::DecodePhysicalPageIndex;
using oxygen::renderer::vsm::HasAnyFlag;
using oxygen::renderer::vsm::IsMapped;
using oxygen::renderer::vsm::MakeMappedShaderPageTableEntry;
using oxygen::renderer::vsm::MakeUnmappedShaderPageTableEntry;
using oxygen::renderer::vsm::VsmPhysicalPageIndex;
using oxygen::renderer::vsm::VsmShaderPageFlagBits;
using oxygen::renderer::vsm::VsmShaderPageFlags;
using oxygen::renderer::vsm::testing::VirtualShadowTest;

class VsmShaderTypesTest : public VirtualShadowTest { };

NOLINT_TEST_F(
  VsmShaderTypesTest, ShaderPageTableEntryEncodesMappedBitAndPhysicalPageIndex)
{
  constexpr auto unmapped = MakeUnmappedShaderPageTableEntry();
  constexpr auto mapped
    = MakeMappedShaderPageTableEntry(VsmPhysicalPageIndex { .value = 37U });

  EXPECT_FALSE(IsMapped(unmapped));
  EXPECT_TRUE(IsMapped(mapped));
  EXPECT_EQ(DecodePhysicalPageIndex(mapped).value, 37U);
}

NOLINT_TEST_F(VsmShaderTypesTest, ShaderPageFlagsUseStableBitAssignments)
{
  const auto flags = VsmShaderPageFlags {
    .bits = static_cast<std::uint32_t>(VsmShaderPageFlagBits::kAllocated)
      | static_cast<std::uint32_t>(VsmShaderPageFlagBits::kStaticUncached),
  };

  EXPECT_THAT(flags.bits, ::testing::Eq(0x5U));
  EXPECT_TRUE(HasAnyFlag(flags, VsmShaderPageFlagBits::kAllocated));
  EXPECT_FALSE(HasAnyFlag(flags, VsmShaderPageFlagBits::kDynamicUncached));
}

} // namespace
