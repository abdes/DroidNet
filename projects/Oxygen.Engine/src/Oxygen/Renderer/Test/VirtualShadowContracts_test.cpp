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
using oxygen::renderer::HasVirtualShadowHierarchyVisibility;
using oxygen::renderer::MakeVirtualShadowPageFlags;
using oxygen::renderer::MakeVirtualShadowHierarchyFlags;
using oxygen::renderer::MergeVirtualShadowHierarchyFlags;
using oxygen::renderer::PackVirtualShadowPageTableEntry;
using oxygen::renderer::ResolveVirtualShadowFallbackClipIndex;
using oxygen::renderer::VirtualShadowPageFlag;
using oxygen::renderer::internal::shadow_detail::
  IsDirectionalVirtualCacheLayoutCompatible;
using oxygen::renderer::internal::shadow_detail::
  IsDirectionalVirtualClipmapPanningCompatible;
using oxygen::renderer::internal::shadow_detail::
  IsDirectionalVirtualDepthGuardbandValid;
using oxygen::renderer::internal::shadow_detail::
  IsDirectionalVirtualClipReuseGuardbandValid;
using oxygen::renderer::internal::shadow_detail::
  RecoverDirectionalVirtualDepthRange;
using oxygen::renderer::internal::shadow_detail::
  RemapDirectionalRequestedDepthToResolvedClip;
using oxygen::renderer::internal::shadow_detail::
  ResolveDirectionalCoarseBackboneBegin;
using oxygen::renderer::internal::shadow_detail::
  ResolveDirectionalCoarseClipCount;
using oxygen::renderer::internal::shadow_detail::
  ResolveDirectionalVirtualClipmapPageOffset;
using oxygen::renderer::internal::shadow_detail::
  TransformDirectionalRequestedPageCoordToResolvedClip;
using oxygen::renderer::internal::shadow_detail::
  BuildDirectionalVirtualClipRelativeTransform;
using oxygen::renderer::internal::shadow_detail::
  CompareVirtualResidentEvictionPriority;
using oxygen::renderer::internal::shadow_detail::
  kDirectionalVirtualClipReuseGuardbandPages;
using oxygen::renderer::internal::shadow_detail::PackVirtualResidentPageKey;

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

auto SetDirectionalDepthRange(
  DirectionalVirtualShadowMetadata& metadata, const float near_plane,
  const float far_plane) -> void
{
  const float depth_scale = 1.0F / (near_plane - far_plane);
  const float depth_bias = near_plane / (near_plane - far_plane);
  for (std::uint32_t clip_index = 0U; clip_index < metadata.clip_level_count;
    ++clip_index) {
    metadata.clip_metadata[clip_index].origin_page_scale.w = depth_scale;
    metadata.clip_metadata[clip_index].bias_reserved.x = depth_bias;
  }
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

TEST(VirtualShadowContractsTest, ClipmapPanningCompatibilityAllowsMotionOnlyWhenEnabled)
{
  const auto previous = MakeDirectionalVirtualMetadata(4, -1, 2.0F, 1, 0, 4.0F);
  const auto current = MakeDirectionalVirtualMetadata(5, -1, 2.0F, 1, 0, 4.0F);

  const auto clip0_offset
    = ResolveDirectionalVirtualClipmapPageOffset(previous, current, 0U);

  EXPECT_TRUE(IsDirectionalVirtualClipmapPanningCompatible(clip0_offset, true));
  EXPECT_FALSE(
    IsDirectionalVirtualClipmapPanningCompatible(clip0_offset, false));
}

TEST(VirtualShadowContractsTest, CacheLayoutCompatibilityAllowsPanningButRejectsLayoutChanges)
{
  auto previous = MakeDirectionalVirtualMetadata(10, -4, 2.0F, 3, 1, 4.0F);
  auto current = MakeDirectionalVirtualMetadata(12, -7, 2.0F, 2, 3, 4.0F);

  previous.light_view[3][0] = 3.0F;
  previous.light_view[3][1] = -5.0F;
  current.light_view[3][0] = 8.0F;
  current.light_view[3][1] = 2.0F;

  EXPECT_TRUE(IsDirectionalVirtualCacheLayoutCompatible(previous, current));

  current.pages_per_axis = 32U;
  EXPECT_FALSE(IsDirectionalVirtualCacheLayoutCompatible(previous, current));
}

TEST(VirtualShadowContractsTest, DepthRangeRecoveryAndGuardbandAreStable)
{
  auto metadata = MakeDirectionalVirtualMetadata(0, 0, 2.0F, 0, 0, 4.0F);
  SetDirectionalDepthRange(metadata, 1.0F, 11.0F);

  const auto recovered = RecoverDirectionalVirtualDepthRange(metadata);
  ASSERT_TRUE(recovered.valid);
  EXPECT_NEAR(recovered.near_plane, 1.0F, 1.0e-5F);
  EXPECT_NEAR(recovered.far_plane, 11.0F, 1.0e-5F);
  EXPECT_NEAR(recovered.min_depth, -11.0F, 1.0e-5F);
  EXPECT_NEAR(recovered.max_depth, -1.0F, 1.0e-5F);

  EXPECT_TRUE(IsDirectionalVirtualDepthGuardbandValid(metadata, -9.0F, -2.0F));
  EXPECT_FALSE(
    IsDirectionalVirtualDepthGuardbandValid(metadata, -10.75F, -2.0F));
}

TEST(VirtualShadowContractsTest, CoarseClipBandUsesStableExplicitTailRange)
{
  EXPECT_EQ(ResolveDirectionalCoarseClipCount(6U), 2U);
  EXPECT_EQ(ResolveDirectionalCoarseClipCount(8U), 3U);
  EXPECT_EQ(ResolveDirectionalCoarseClipCount(10U), 4U);
  EXPECT_EQ(ResolveDirectionalCoarseClipCount(12U), 4U);

  EXPECT_EQ(ResolveDirectionalCoarseBackboneBegin(6U), 4U);
  EXPECT_EQ(ResolveDirectionalCoarseBackboneBegin(8U), 5U);
  EXPECT_EQ(ResolveDirectionalCoarseBackboneBegin(10U), 6U);
  EXPECT_EQ(ResolveDirectionalCoarseBackboneBegin(12U), 8U);
}

TEST(VirtualShadowContractsTest, FallbackTransformRemapsRequestedClipToResolvedClip)
{
  auto metadata = MakeDirectionalVirtualMetadata(10, -4, 2.0F, 3, 1, 4.0F);
  SetDirectionalDepthRange(metadata, 2.0F, 18.0F);

  const auto transform = BuildDirectionalVirtualClipRelativeTransform(
    metadata, 0U, 1U);
  ASSERT_TRUE(transform.valid);
  EXPECT_NEAR(transform.page_coord_scale.x, 0.5F, 1.0e-5F);
  EXPECT_NEAR(transform.page_coord_scale.y, 0.5F, 1.0e-5F);
  EXPECT_NEAR(transform.page_coord_bias.x, 2.0F, 1.0e-5F);
  EXPECT_NEAR(transform.page_coord_bias.y, -3.0F, 1.0e-5F);
  EXPECT_NEAR(transform.lod_scale, 2.0F, 1.0e-5F);
  EXPECT_NEAR(transform.depth_scale, 1.0F, 1.0e-5F);
  EXPECT_NEAR(transform.depth_bias, 0.0F, 1.0e-5F);

  const glm::vec2 requested_page_coord(3.0F, 5.0F);
  const glm::vec2 resolved_page_coord
    = TransformDirectionalRequestedPageCoordToResolvedClip(
      requested_page_coord, transform);
  EXPECT_NEAR(resolved_page_coord.x, 3.5F, 1.0e-5F);
  EXPECT_NEAR(resolved_page_coord.y, -0.5F, 1.0e-5F);

  const float requested_depth = 0.375F;
  EXPECT_NEAR(
    RemapDirectionalRequestedDepthToResolvedClip(requested_depth, transform),
    requested_depth, 1.0e-5F);
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

TEST(VirtualShadowContractsTest, HierarchicalPageFlagsPropagateDescendantUsage)
{
  constexpr auto child_flags
    = MakeVirtualShadowPageFlags(true, true, false, true, true);
  constexpr auto propagated_hierarchy
    = MakeVirtualShadowHierarchyFlags(child_flags);
  constexpr auto parent_flags
    = MergeVirtualShadowHierarchyFlags(0U, child_flags);

  EXPECT_TRUE(HasVirtualShadowPageFlag(propagated_hierarchy,
    VirtualShadowPageFlag::kHierarchyAllocatedDescendant));
  EXPECT_TRUE(HasVirtualShadowPageFlag(propagated_hierarchy,
    VirtualShadowPageFlag::kHierarchyDynamicUncachedDescendant));
  EXPECT_FALSE(HasVirtualShadowPageFlag(propagated_hierarchy,
    VirtualShadowPageFlag::kHierarchyStaticUncachedDescendant));
  EXPECT_TRUE(HasVirtualShadowPageFlag(propagated_hierarchy,
    VirtualShadowPageFlag::kHierarchyDetailDescendant));
  EXPECT_TRUE(HasVirtualShadowPageFlag(propagated_hierarchy,
    VirtualShadowPageFlag::kHierarchyUsedThisFrameDescendant));

  EXPECT_EQ(parent_flags, propagated_hierarchy);
}

TEST(VirtualShadowContractsTest, PhysicalPageContractsRemainGpuFriendly)
{
  EXPECT_EQ(sizeof(oxygen::renderer::VirtualShadowPhysicalPageMetadata) % 16U,
    0U);
  EXPECT_EQ(
    sizeof(oxygen::renderer::VirtualShadowPhysicalPageListEntry) % 16U, 0U);
}

TEST(VirtualShadowContractsTest,
  PageFlagsHierarchyVisibilityMatchesFallbackPolicy)
{
  EXPECT_FALSE(HasVirtualShadowHierarchyVisibility(0U));
  EXPECT_TRUE(HasVirtualShadowHierarchyVisibility(
    MakeVirtualShadowPageFlags(true, false, false, false, false)));
  EXPECT_TRUE(HasVirtualShadowHierarchyVisibility(
    MakeVirtualShadowPageFlags(false, false, false, true, false)));
  EXPECT_TRUE(HasVirtualShadowHierarchyVisibility(MergeVirtualShadowHierarchyFlags(
    0U, MakeVirtualShadowPageFlags(true, false, false, true, false))));
  EXPECT_FALSE(HasVirtualShadowHierarchyVisibility(
    MakeVirtualShadowPageFlags(false, false, true, false, false)));
}

TEST(VirtualShadowContractsTest,
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
