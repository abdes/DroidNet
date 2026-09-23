//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>
#include <limits>
#include <type_traits>

#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/MaterialDecode.h>

namespace oxygen::vortex::testing::reference {
namespace {
  static_assert(
    !std::is_convertible_v<PackedGBufferNormal, PackedGBufferMaterial>);
  static_assert(
    !std::is_convertible_v<PackedGBufferMaterial, PackedGBufferBaseColor>);

  NOLINT_TEST(
    MaterialDecodeReferenceTest, StoredColorAndLinearAoHaveDifferentTransfers)
  {
    const auto result = DecodeGBufferTexel({
      .material = PackedGBufferMaterial { 0x01118040U },
      .base_color = PackedGBufferBaseColor { 0x80FF0A80U },
    });
    EXPECT_NEAR(result.material.base_color.red, 0.21586050011389926, 1.0e-15);
    EXPECT_NEAR(
      result.material.base_color.green, 0.003035269835488375, 1.0e-17);
    EXPECT_EQ(result.material.base_color.blue, 1.0);
    EXPECT_EQ(result.ambient_occlusion, 128.0 / 255.0);
    EXPECT_EQ(result.material.metallic, 64.0 / 255.0);
    EXPECT_EQ(result.material.specular, 128.0 / 255.0);
    EXPECT_EQ(result.material.roughness.get(), 17.0 / 255.0);
    EXPECT_EQ(result.shading_model.get(), 1U);
    EXPECT_GT(result.ambient_occlusion - result.material.base_color.red, 0.28);
    EXPECT_NEAR(DecodeSrgb8(11U), 0.003346535763899161, 1.0e-17);
    EXPECT_EQ(DecodeSrgb8(0U), 0.0);
    EXPECT_EQ(DecodeSrgb8(255U), 1.0);
  }

  NOLINT_TEST(
    MaterialDecodeReferenceTest, OctahedralNormalsUnfoldBothHemispheres)
  {
    const auto south
      = DecodeGBufferTexel({ .normal = PackedGBufferNormal { 0U } });
    EXPECT_EQ(south.normal.x, 0.0);
    EXPECT_EQ(south.normal.y, 0.0);
    EXPECT_EQ(south.normal.z, -1.0);
    const auto north = DecodeGBufferTexel(
      { .normal = PackedGBufferNormal { 512U | (512U << 10U) } });
    EXPECT_GT(north.normal.x, 0.0);
    EXPECT_GT(north.normal.y, 0.0);
    EXPECT_GT(north.normal.z, 0.99999);
    EXPECT_NEAR(
      std::hypot(north.normal.x, north.normal.y, north.normal.z), 1.0, 1.0e-15);
    const auto poisoned_unused_lanes = DecodeGBufferTexel({
      .normal = PackedGBufferNormal { 512U | (512U << 10U) | 0xFFF00000U },
    });
    EXPECT_EQ(north.normal.x, poisoned_unused_lanes.normal.x);
    EXPECT_EQ(north.normal.y, poisoned_unused_lanes.normal.y);
    EXPECT_EQ(north.normal.z, poisoned_unused_lanes.normal.z);
    const auto opposite = DecodeGBufferTexel(
      { .normal = PackedGBufferNormal { 511U | (511U << 10U) } });
    EXPECT_EQ(opposite.normal.x, -north.normal.x);
    EXPECT_EQ(opposite.normal.y, -north.normal.y);
  }

  NOLINT_TEST(
    MaterialDecodeReferenceTest, QuantizedSpecularAndRoughnessStayExplicit)
  {
    const auto material = DecodeGBufferTexel({
      .material = PackedGBufferMaterial { 0x000B8000U },
      .base_color = PackedGBufferBaseColor { 0xFFFFFFFFU },
    });
    const auto brdf = ResolveMaterialBrdf(material.material);
    ASSERT_TRUE(brdf.has_value());
    EXPECT_NEAR(brdf->red.f0, 0.04015686274509804, 1.0e-16);
    EXPECT_GT(brdf->red.f0, 0.04);
    EXPECT_EQ(brdf->red.diffuse, 1.0);
    EXPECT_EQ(material.material.roughness.get(), 11.0 / 255.0);
    EXPECT_LT(material.material.roughness.get(), 0.045);
  }

  NOLINT_TEST(
    MaterialDecodeReferenceTest, MetallicMixturesResolveEveryReflectanceChannel)
  {
    const auto mixture = ResolveMaterialBrdf({
      .base_color = { .red = 0.8, .green = 0.4, .blue = 0.2 },
      .metallic = 0.25,
      .specular = 0.5,
    });
    ASSERT_TRUE(mixture.has_value());
    EXPECT_NEAR(mixture->red.f0, 0.23, 1.0e-15);
    EXPECT_NEAR(mixture->green.f0, 0.13, 1.0e-15);
    EXPECT_NEAR(mixture->blue.f0, 0.08, 1.0e-15);
    EXPECT_NEAR(mixture->red.diffuse, 0.6, 1.0e-15);
    EXPECT_NEAR(mixture->green.diffuse, 0.3, 1.0e-15);
    EXPECT_NEAR(mixture->blue.diffuse, 0.15, 1.0e-15);
    const auto metal
      = ResolveMaterialBrdf({ .base_color = { .red = 0.8 }, .metallic = 1.0 });
    ASSERT_TRUE(metal.has_value());
    EXPECT_EQ(metal->red.f0, 0.8);
    EXPECT_EQ(metal->red.diffuse, 0.0);
    for (const auto invalid :
      { -0.1, 1.1, std::numeric_limits<double>::quiet_NaN() }) {
      EXPECT_FALSE(ResolveMaterialBrdf({ .base_color = { .red = invalid } }));
      EXPECT_FALSE(ResolveMaterialBrdf({ .metallic = invalid }));
      EXPECT_FALSE(ResolveMaterialBrdf({ .specular = invalid }));
    }
  }
} // namespace
} // namespace oxygen::vortex::testing::reference
