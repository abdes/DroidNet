//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Renderer/Types/VirtualShadowPageFlags.h>
#include <Oxygen/Renderer/Types/VirtualShadowPageTableEntry.h>
#include <Oxygen/Renderer/Types/VirtualShadowPhysicalPageMetadata.h>

namespace {

using oxygen::renderer::DecodeVirtualShadowPageTableEntry;
using oxygen::renderer::HasVirtualShadowPageFlag;
using oxygen::renderer::MakeVirtualShadowPageFlags;
using oxygen::renderer::PackVirtualShadowPageTableEntry;
using oxygen::renderer::ResolveVirtualShadowFallbackClipIndex;
using oxygen::renderer::VirtualShadowPageFlag;

TEST(VirtualShadowContractsTest, PageTableEntryRoundTripsPhysicalAddressAndBits)
{
  constexpr auto packed = PackVirtualShadowPageTableEntry(
    321U, 654U, 3U, false, true, false);
  constexpr auto decoded = DecodeVirtualShadowPageTableEntry(packed);

  EXPECT_EQ(decoded.tile_x, 321U);
  EXPECT_EQ(decoded.tile_y, 654U);
  EXPECT_EQ(decoded.fallback_lod_offset, 3U);
  EXPECT_FALSE(decoded.current_lod_valid);
  EXPECT_TRUE(decoded.any_lod_valid);
  EXPECT_FALSE(decoded.requested_this_frame);
}

TEST(VirtualShadowContractsTest, FallbackDecodeClampsToCoarsestClip)
{
  constexpr auto packed = PackVirtualShadowPageTableEntry(
    11U, 19U, 6U, false, true, true);

  EXPECT_EQ(ResolveVirtualShadowFallbackClipIndex(2U, 8U, packed), 7U);
  EXPECT_EQ(ResolveVirtualShadowFallbackClipIndex(2U, 3U, packed), 2U);
  EXPECT_EQ(ResolveVirtualShadowFallbackClipIndex(2U, 0U, packed), 0U);
}

TEST(VirtualShadowContractsTest, PageFlagsHelpersExposeBinaryCoarseDetailPolicy)
{
  constexpr auto flags
    = MakeVirtualShadowPageFlags(true, true, false, true, true);

  EXPECT_TRUE(HasVirtualShadowPageFlag(flags, VirtualShadowPageFlag::kAllocated));
  EXPECT_TRUE(HasVirtualShadowPageFlag(
    flags, VirtualShadowPageFlag::kDynamicUncached));
  EXPECT_FALSE(HasVirtualShadowPageFlag(
    flags, VirtualShadowPageFlag::kStaticUncached));
  EXPECT_TRUE(HasVirtualShadowPageFlag(
    flags, VirtualShadowPageFlag::kDetailGeometry));
  EXPECT_TRUE(HasVirtualShadowPageFlag(
    flags, VirtualShadowPageFlag::kUsedThisFrame));
}

TEST(VirtualShadowContractsTest, PhysicalPageContractsRemainGpuFriendly)
{
  EXPECT_EQ(sizeof(oxygen::renderer::VirtualShadowPhysicalPageMetadata) % 16U,
    0U);
  EXPECT_EQ(
    sizeof(oxygen::renderer::VirtualShadowPhysicalPageListEntry) % 16U, 0U);
}

} // namespace
