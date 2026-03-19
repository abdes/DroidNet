//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <glm/vec2.hpp>

#include <Oxygen/Renderer/Internal/ShadowBackendCommon.h>
#include <Oxygen/Renderer/Test/VirtualShadow/VirtualShadowTestSupport.h>
#include <Oxygen/Renderer/Types/VirtualShadowPageTableEntry.h>

namespace {

using oxygen::renderer::DecodeVirtualShadowPageTableEntry;
using oxygen::renderer::PackVirtualShadowPageTableEntry;
using oxygen::renderer::ResolveVirtualShadowFallbackClipIndex;
using oxygen::renderer::internal::shadow_detail::
  BuildDirectionalVirtualClipRelativeTransform;
using oxygen::renderer::internal::shadow_detail::
  ComputeDirectionalVirtualFallbackSlopeBiasScale;
using oxygen::renderer::internal::shadow_detail::
  IsDirectionalVirtualDepthGuardbandValid;
using oxygen::renderer::internal::shadow_detail::
  RecoverDirectionalVirtualDepthRange;
using oxygen::renderer::internal::shadow_detail::
  RemapDirectionalRequestedDepthToResolvedClip;
using oxygen::renderer::internal::shadow_detail::
  ResolveDirectionalCoarseBackboneBegin;
using oxygen::renderer::internal::shadow_detail::
  ResolveDirectionalCoarseClipCount;
using oxygen::renderer::internal::shadow_detail::
  ResolveDirectionalVirtualGuardTexels;
using oxygen::renderer::internal::shadow_detail::
  TransformDirectionalRequestedPageCoordToResolvedClip;
using oxygen::renderer::test::virtual_shadow_support::
  MakeDirectionalVirtualMetadata;
using oxygen::renderer::test::virtual_shadow_support::SetDirectionalDepthRange;

TEST(VirtualShadowFallbackContractsTest, DepthRangeRecoveryAndGuardbandAreStable)
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

TEST(VirtualShadowFallbackContractsTest,
  CoarseClipBandUsesStableExplicitTailRange)
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

TEST(VirtualShadowFallbackContractsTest,
  FallbackTransformRemapsRequestedClipToResolvedClip)
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

TEST(VirtualShadowFallbackContractsTest,
  FilterGuardTexelsTrackEffectiveFilterRadius)
{
  EXPECT_EQ(ResolveDirectionalVirtualGuardTexels(128U, 1U), 1U);
  EXPECT_EQ(ResolveDirectionalVirtualGuardTexels(128U, 2U), 2U);
  EXPECT_EQ(ResolveDirectionalVirtualGuardTexels(128U, 5U), 5U);
  EXPECT_EQ(ResolveDirectionalVirtualGuardTexels(8U, 8U), 2U);
}

TEST(VirtualShadowFallbackContractsTest,
  FallbackSlopeBiasScaleTracksLodGrowth)
{
  EXPECT_FLOAT_EQ(ComputeDirectionalVirtualFallbackSlopeBiasScale(0U), 1.0F);
  EXPECT_FLOAT_EQ(ComputeDirectionalVirtualFallbackSlopeBiasScale(1U), 2.0F);
  EXPECT_FLOAT_EQ(ComputeDirectionalVirtualFallbackSlopeBiasScale(3U), 8.0F);
}

TEST(VirtualShadowFallbackContractsTest, PageTableEntryRoundTripsPhysicalAddressAndBits)
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

TEST(VirtualShadowFallbackContractsTest, FallbackDecodeClampsToCoarsestClip)
{
  constexpr auto packed = PackVirtualShadowPageTableEntry(
    11U, 19U, 6U, false, true, true);

  EXPECT_EQ(ResolveVirtualShadowFallbackClipIndex(2U, 8U, packed), 7U);
  EXPECT_EQ(ResolveVirtualShadowFallbackClipIndex(2U, 3U, packed), 2U);
  EXPECT_EQ(ResolveVirtualShadowFallbackClipIndex(2U, 0U, packed), 0U);
}

} // namespace
