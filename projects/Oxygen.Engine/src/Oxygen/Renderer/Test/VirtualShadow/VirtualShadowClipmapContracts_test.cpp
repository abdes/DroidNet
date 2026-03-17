//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <glm/ext/matrix_transform.hpp>

#include <Oxygen/Renderer/Internal/ShadowBackendCommon.h>
#include <Oxygen/Renderer/Test/VirtualShadow/VirtualShadowTestSupport.h>

namespace {

using oxygen::renderer::internal::shadow_detail::
  HashDirectionalVirtualFeedbackAddressSpace;
using oxygen::renderer::internal::shadow_detail::
  IsDirectionalVirtualCacheLayoutCompatible;
using oxygen::renderer::internal::shadow_detail::
  IsDirectionalVirtualClipReuseGuardbandValid;
using oxygen::renderer::internal::shadow_detail::
  IsDirectionalVirtualClipmapPanningCompatible;
using oxygen::renderer::internal::shadow_detail::
  ResolveDirectionalVirtualClipmapPageOffset;
using oxygen::renderer::internal::shadow_detail::
  kDirectionalVirtualClipReuseGuardbandPages;
using oxygen::renderer::test::virtual_shadow_support::
  MakeDirectionalVirtualMetadata;

TEST(VirtualShadowClipmapContractsTest, PageOffsetTracksSnappedGridMotion)
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

TEST(VirtualShadowClipmapContractsTest,
  GuardbandRejectsOffsetsBeyondReuseWindow)
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

TEST(VirtualShadowClipmapContractsTest,
  ClipmapPanningCompatibilityAllowsMotionOnlyWhenEnabled)
{
  const auto previous = MakeDirectionalVirtualMetadata(4, -1, 2.0F, 1, 0, 4.0F);
  const auto current = MakeDirectionalVirtualMetadata(5, -1, 2.0F, 1, 0, 4.0F);

  const auto clip0_offset
    = ResolveDirectionalVirtualClipmapPageOffset(previous, current, 0U);

  EXPECT_TRUE(IsDirectionalVirtualClipmapPanningCompatible(clip0_offset, true));
  EXPECT_FALSE(
    IsDirectionalVirtualClipmapPanningCompatible(clip0_offset, false));
}

TEST(VirtualShadowClipmapContractsTest,
  CacheLayoutCompatibilityAllowsEquivalentViewPanning)
{
  auto previous = MakeDirectionalVirtualMetadata(10, -4, 2.0F, 3, 1, 4.0F);
  auto current = MakeDirectionalVirtualMetadata(12, -7, 2.0F, 2, 3, 4.0F);

  previous.light_view = glm::translate(glm::mat4(1.0F), glm::vec3(3.0F, -5.0F, 7.0F));
  current.light_view = glm::translate(glm::mat4(1.0F), glm::vec3(8.0F, 2.0F, -11.0F));

  EXPECT_TRUE(IsDirectionalVirtualCacheLayoutCompatible(previous, current));
  EXPECT_EQ(HashDirectionalVirtualFeedbackAddressSpace(previous),
    HashDirectionalVirtualFeedbackAddressSpace(current));
}

TEST(VirtualShadowClipmapContractsTest,
  CacheLayoutCompatibilityRejectsClipCountChange)
{
  const auto previous = MakeDirectionalVirtualMetadata(10, -4, 2.0F, 3, 1, 4.0F);
  auto current = previous;
  current.clip_level_count = 1U;

  EXPECT_FALSE(IsDirectionalVirtualCacheLayoutCompatible(previous, current));
  EXPECT_NE(HashDirectionalVirtualFeedbackAddressSpace(previous),
    HashDirectionalVirtualFeedbackAddressSpace(current));
}

TEST(VirtualShadowClipmapContractsTest,
  CacheLayoutCompatibilityRejectsPagesPerAxisChange)
{
  const auto previous = MakeDirectionalVirtualMetadata(10, -4, 2.0F, 3, 1, 4.0F);
  auto current = previous;
  current.pages_per_axis = 32U;

  EXPECT_FALSE(IsDirectionalVirtualCacheLayoutCompatible(previous, current));
  EXPECT_NE(HashDirectionalVirtualFeedbackAddressSpace(previous),
    HashDirectionalVirtualFeedbackAddressSpace(current));
}

TEST(VirtualShadowClipmapContractsTest,
  CacheLayoutCompatibilityRejectsPageSizeChange)
{
  const auto previous = MakeDirectionalVirtualMetadata(10, -4, 2.0F, 3, 1, 4.0F);
  auto current = previous;
  current.page_size_texels = 256U;

  EXPECT_FALSE(IsDirectionalVirtualCacheLayoutCompatible(previous, current));
  EXPECT_NE(HashDirectionalVirtualFeedbackAddressSpace(previous),
    HashDirectionalVirtualFeedbackAddressSpace(current));
}

TEST(VirtualShadowClipmapContractsTest,
  CacheLayoutCompatibilityRejectsPerClipPageWorldChange)
{
  const auto previous = MakeDirectionalVirtualMetadata(10, -4, 2.0F, 3, 1, 4.0F);
  auto current = previous;
  current.clip_metadata[1].origin_page_scale.z = 8.0F;

  EXPECT_FALSE(IsDirectionalVirtualCacheLayoutCompatible(previous, current));
  EXPECT_NE(HashDirectionalVirtualFeedbackAddressSpace(previous),
    HashDirectionalVirtualFeedbackAddressSpace(current));
}

TEST(VirtualShadowClipmapContractsTest,
  CacheLayoutCompatibilityRejectsLightViewBasisChange)
{
  const auto previous = MakeDirectionalVirtualMetadata(10, -4, 2.0F, 3, 1, 4.0F);
  auto current = previous;
  current.light_view = glm::rotate(
    glm::mat4(1.0F), 0.25F, glm::vec3(0.0F, 0.0F, 1.0F));

  EXPECT_FALSE(IsDirectionalVirtualCacheLayoutCompatible(previous, current));
  EXPECT_NE(HashDirectionalVirtualFeedbackAddressSpace(previous),
    HashDirectionalVirtualFeedbackAddressSpace(current));
}

} // namespace
