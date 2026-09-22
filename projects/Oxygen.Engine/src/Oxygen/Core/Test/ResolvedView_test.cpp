//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <limits>

#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Core/Types/ViewHelpers.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::ResolvedView;

constexpr float kNearDepthMagnitudeM = 8.0F;
constexpr float kFarDepthM = 24.0F;

NOLINT_TEST(ResolvedViewTest, OrthographicSupportsSignedAndZeroNearPlanes)
{
  for (const auto near_plane :
    std::array { -kNearDepthMagnitudeM, 0.0F, 2.0F }) {
    auto params = ResolvedView::Params {};
    params.near_plane = near_plane;
    params.far_plane = kFarDepthM;
    params.proj_matrix = oxygen::MakeReversedZOrthographicProjectionRH_ZO(
      -2.0F, 2.0F, -2.0F, 2.0F, near_plane, params.far_plane);
    const auto view = ResolvedView(params);
    EXPECT_TRUE(view.IsOrthographic());
    EXPECT_EQ(view.NearPlane(), near_plane);
    EXPECT_EQ(view.FarPlane(), params.far_plane);
    const auto near_clip
      = view.ProjectionMatrix() * glm::vec4 { 0.0F, 0.0F, -near_plane, 1.0F };
    const auto far_clip = view.ProjectionMatrix()
      * glm::vec4 { 0.0F, 0.0F, -params.far_plane, 1.0F };
    EXPECT_NEAR(near_clip.z / near_clip.w, 1.0F, 1.0e-6F);
    EXPECT_NEAR(far_clip.z / far_clip.w, 0.0F, 1.0e-6F);
  }
}

NOLINT_TEST(ResolvedViewTest, OrthographicSupportsAnEntirelySignedDepthInterval)
{
  auto params = ResolvedView::Params {};
  params.near_plane = -kFarDepthM;
  params.far_plane = -kNearDepthMagnitudeM;
  params.proj_matrix = oxygen::MakeReversedZOrthographicProjectionRH_ZO(
    -2.0F, 2.0F, -2.0F, 2.0F, params.near_plane, params.far_plane);
  const auto view = ResolvedView(params);
  EXPECT_TRUE(view.IsOrthographic());
  EXPECT_EQ(view.NearPlane(), -kFarDepthM);
  EXPECT_EQ(view.FarPlane(), -kNearDepthMagnitudeM);
}

NOLINT_TEST(ResolvedViewTest, PerspectiveRetainsPositiveClipPlanes)
{
  auto params = ResolvedView::Params {};
  params.near_plane = 0.25F;
  params.far_plane = kFarDepthM;
  params.proj_matrix = oxygen::MakeReversedZPerspectiveProjectionRH_ZO(
    1.0F, 1.0F, params.near_plane, params.far_plane);
  const auto view = ResolvedView(params);
  EXPECT_FALSE(view.IsOrthographic());
  EXPECT_EQ(view.NearPlane(), params.near_plane);
  EXPECT_EQ(view.FarPlane(), params.far_plane);
}

NOLINT_TEST(ResolvedViewDeathTest, RejectsNonpositivePerspectiveNear)
{
  for (const auto near_plane : std::array { -kNearDepthMagnitudeM, 0.0F }) {
    auto params = ResolvedView::Params {};
    params.proj_matrix = oxygen::MakeReversedZPerspectiveProjectionRH_ZO(
      1.0F, 1.0F, 0.25F, kFarDepthM);
    params.near_plane = near_plane;
    params.far_plane = kFarDepthM;
    NOLINT_ASSERT_DEATH(
      [[maybe_unused]] const auto view = ResolvedView(params), "near_plane");
  }
}

NOLINT_TEST(
  ResolvedViewDeathTest, RejectsNonfiniteAndUnorderedOrthographicPlanes)
{
  for (const auto near_plane : std::array {
         std::numeric_limits<float>::quiet_NaN(),
         -std::numeric_limits<float>::infinity(),
         kFarDepthM,
         25.0F,
       }) {
    auto params = ResolvedView::Params {};
    params.proj_matrix = oxygen::MakeReversedZOrthographicProjectionRH_ZO(
      -2.0F, 2.0F, -2.0F, 2.0F, -kNearDepthMagnitudeM, kFarDepthM);
    params.near_plane = near_plane;
    params.far_plane = kFarDepthM;
    NOLINT_ASSERT_DEATH(
      [[maybe_unused]] const auto view = ResolvedView(params), "plane");
  }
}

} // namespace
