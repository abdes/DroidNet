//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <cmath>
#include <cstdint>

#include <Oxygen/Renderer/Types/LightCullingConfig.h>

namespace {

using oxygen::Extent;
using oxygen::engine::LightCullingConfig;

NOLINT_TEST(LightCullingConfigTest, GridDimensionsUseFixedShippingConstants)
{
  const auto dims = LightCullingConfig {}.ComputeGridDimensions(
    Extent<std::uint32_t> { .width = 129U, .height = 65U });

  EXPECT_EQ(dims.x, 3U);
  EXPECT_EQ(dims.y, 2U);
  EXPECT_EQ(dims.z, LightCullingConfig::kLightGridSizeZ);
  EXPECT_EQ(dims.total_clusters, 3U * 2U * LightCullingConfig::kLightGridSizeZ);
}

NOLINT_TEST(
  LightCullingConfigTest, ComputeLightGridZParamsMatchesUeShapeInMeters)
{
  constexpr float kNearPlane = 0.1F;
  constexpr float kFarPlane = 1000.0F;

  const auto z_params
    = LightCullingConfig::ComputeLightGridZParams(kNearPlane, kFarPlane);

  const double n = static_cast<double>(kNearPlane)
    + static_cast<double>(LightCullingConfig::kNearOffsetMeters);
  const double f = static_cast<double>(kFarPlane)
    + static_cast<double>(LightCullingConfig::kFarPlanePadMeters);
  const double s
    = static_cast<double>(LightCullingConfig::kSliceDistributionScale);
  const double expected_o
    = (f
        - n
          * std::exp2(
            static_cast<double>(LightCullingConfig::kLightGridSizeZ - 1U) / s))
    / (f - n);
  const double expected_b = (1.0 - expected_o) / n;

  EXPECT_NEAR(z_params.s, LightCullingConfig::kSliceDistributionScale, 1.0e-6);
  EXPECT_NEAR(z_params.b, expected_b, 1.0e-6);
  EXPECT_NEAR(z_params.o, expected_o, 1.0e-6);
}

NOLINT_TEST(LightCullingConfigTest, ComputeZSliceUsesPublishedUeLikeMapping)
{
  const auto z_params
    = LightCullingConfig::ComputeLightGridZParams(0.1F, 1000.0F);

  for (const std::uint32_t slice : { 0U, 1U, 7U, 15U, 31U }) {
    const double mid_slice = static_cast<double>(slice) + 0.5;
    const double linear_depth
      = (std::exp2(mid_slice / z_params.s) - z_params.o) / z_params.b;

    EXPECT_EQ(LightCullingConfig::ComputeZSlice(
                static_cast<float>(linear_depth), z_params),
      slice);
  }

  EXPECT_EQ(LightCullingConfig::ComputeZSlice(0.0F, z_params), 0U);
  EXPECT_EQ(LightCullingConfig::ComputeZSlice(1.0e8F, z_params),
    LightCullingConfig::kLightGridSizeZ - 1U);
}

} // namespace
