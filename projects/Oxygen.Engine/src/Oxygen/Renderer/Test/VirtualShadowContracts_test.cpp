//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <glm/vec4.hpp>

#include <Oxygen/Renderer/Internal/ShadowBackendCommon.h>
#include <Oxygen/Renderer/Types/DirectionalVirtualShadowMetadata.h>
#include <Oxygen/Renderer/Types/VirtualShadowPageFlags.h>
#include <Oxygen/Renderer/Types/VirtualShadowPhysicalPageMetadata.h>
#include <Oxygen/Renderer/Types/VirtualShadowPageTableEntry.h>

namespace {

using oxygen::engine::DirectionalVirtualShadowMetadata;
using oxygen::renderer::DecodeVirtualShadowPageTableEntry;
using oxygen::renderer::HasVirtualShadowPageFlag;
using oxygen::renderer::MakeVirtualShadowPageFlags;
using oxygen::renderer::PackVirtualShadowPageTableEntry;
using oxygen::renderer::ResolveVirtualShadowFallbackClipIndex;
using oxygen::renderer::VirtualShadowPageFlag;
using oxygen::renderer::internal::shadow_detail::
  IsDirectionalVirtualClipReuseGuardbandValid;
using oxygen::renderer::internal::shadow_detail::
  ResolveDirectionalVirtualClipmapPageOffset;
using oxygen::renderer::internal::shadow_detail::
  kDirectionalVirtualClipReuseGuardbandPages;

auto MakeDirectionalVirtualMetadata(const std::int32_t clip0_grid_x,
  const std::int32_t clip0_grid_y, const float clip0_page_world,
  const std::int32_t clip1_grid_x, const std::int32_t clip1_grid_y,
  const float clip1_page_world) -> DirectionalVirtualShadowMetadata
{
  DirectionalVirtualShadowMetadata metadata {};
  metadata.clip_level_count = 2U;
  metadata.pages_per_axis = 16U;
  metadata.page_size_texels = 128U;
  metadata.clip_metadata[0].origin_page_scale
    = glm::vec4(static_cast<float>(clip0_grid_x) * clip0_page_world,
      static_cast<float>(clip0_grid_y) * clip0_page_world, clip0_page_world,
      0.0F);
  metadata.clip_metadata[1].origin_page_scale
    = glm::vec4(static_cast<float>(clip1_grid_x) * clip1_page_world,
      static_cast<float>(clip1_grid_y) * clip1_page_world, clip1_page_world,
      0.0F);
  return metadata;
}

TEST(VirtualShadowContractsTest, ClipmapPageOffsetTracksSnappedGridMotion)
{
  const auto previous = MakeDirectionalVirtualMetadata(10, -4, 2.0F, 3, 1, 4.0F);
  const auto current = MakeDirectionalVirtualMetadata(12, -7, 2.0F, 2, 3, 4.0F);

  const auto clip0_offset
    = ResolveDirectionalVirtualClipmapPageOffset(previous, current, 0U);
  const auto clip1_offset
    = ResolveDirectionalVirtualClipmapPageOffset(previous, current, 1U);

  ASSERT_TRUE(clip0_offset.valid);
  EXPECT_EQ(clip0_offset.delta_x, 2);
  EXPECT_EQ(clip0_offset.delta_y, -3);
  ASSERT_TRUE(clip1_offset.valid);
  EXPECT_EQ(clip1_offset.delta_x, -1);
  EXPECT_EQ(clip1_offset.delta_y, 2);
}

TEST(VirtualShadowContractsTest, ClipmapGuardbandRejectsOffsetsBeyondReuseWindow)
{
  const auto previous = MakeDirectionalVirtualMetadata(0, 0, 2.0F, 0, 0, 4.0F);
  const auto current = MakeDirectionalVirtualMetadata(2, 0, 2.0F, 0, 1, 4.0F);

  const auto clip0_offset
    = ResolveDirectionalVirtualClipmapPageOffset(previous, current, 0U);
  const auto clip1_offset
    = ResolveDirectionalVirtualClipmapPageOffset(previous, current, 1U);

  EXPECT_FALSE(IsDirectionalVirtualClipReuseGuardbandValid(
    clip0_offset, kDirectionalVirtualClipReuseGuardbandPages));
  EXPECT_TRUE(IsDirectionalVirtualClipReuseGuardbandValid(
    clip1_offset, kDirectionalVirtualClipReuseGuardbandPages));
}

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
