//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>
#include <limits>
#include <numbers>

#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/GgxBrdf.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/GgxMoments.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/MaterialDecode.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/MaterialEvaluation.h>

namespace oxygen::vortex::testing::reference {
namespace {
  NOLINT_TEST(
    MaterialEvaluationReferenceTest, FactorsPackedChannelsAndNormalScaleCompose)
  {
    const auto result = EvaluateMaterial({
      .factors = {
        .base_color = { .rgb = { .red = 0.8, .green = 0.4, .blue = 0.2 }, .alpha = 0.75 },
        .metallic = 0.6, .roughness = PerceptualRoughness { 0.8 },
        .ambient_occlusion = 0.5,
        .emissive = { .red = 2.0, .green = 4.0, .blue = 8.0 }, .normal_scale = 2.0,
      },
      .samples = {
        .base_color = LinearRgba { .rgb = { .red = 0.5, .green = 0.25, .blue = 1.0 }, .alpha = 0.8 },
        .normal = LinearRgb { .red = 0.75, .green = 0.5, .blue = 1.0 },
        .orm = OrmSample { .occlusion = 0.75, .roughness = 0.25, .metallic = 0.5 },
        .metallic = 0.9, .roughness = 0.9, .occlusion = 0.2,
        .emissive = LinearRgb { .red = 3.0, .green = 0.5, .blue = 0.25 },
      },
      .alpha_test = true, .alpha_cutoff = 0.6,
    });
    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(result->material.base_color.red, 0.4);
    EXPECT_DOUBLE_EQ(result->material.base_color.green, 0.1);
    EXPECT_DOUBLE_EQ(result->material.base_color.blue, 0.2);
    EXPECT_DOUBLE_EQ(result->material.metallic, 0.3);
    EXPECT_DOUBLE_EQ(result->material.roughness.get(), 0.2);
    EXPECT_DOUBLE_EQ(result->ambient_occlusion, 0.1);
    EXPECT_DOUBLE_EQ(result->alpha, 0.6);
    EXPECT_TRUE(result->fragment_visible);
    EXPECT_DOUBLE_EQ(result->emissive.red, 6.0);
    EXPECT_DOUBLE_EQ(result->emissive.green, 2.0);
    EXPECT_DOUBLE_EQ(result->emissive.blue, 2.0);
    EXPECT_NEAR(result->normal.x, 1.0 / std::sqrt(2.0), 1.0e-14);
    EXPECT_EQ(result->normal.y, 0.0);
    EXPECT_NEAR(result->normal.z, 1.0 / std::sqrt(2.0), 1.0e-14);
    const auto brdf = ResolveMaterialBrdf(result->material);
    ASSERT_TRUE(brdf.has_value());
    EXPECT_NEAR(brdf->red.f0, 0.148, 1.0e-14);
    EXPECT_NEAR(brdf->red.diffuse, 0.28, 1.0e-14);
  }

  NOLINT_TEST(MaterialEvaluationReferenceTest,
    BasisHandednessAndTwoSidedNormalsAreIndependent)
  {
    auto input = MaterialEvaluationInput {
      .samples
      = { .normal = LinearRgb { .red = 0.5, .green = 0.75, .blue = 1.0 } },
      .basis = { .normal = { .x = 0.0, .y = 0.0, .z = 2.0 },
        .tangent = { 2.0, 0.0, 1.0 },
        .bitangent = { 0.0, -4.0, 0.0 }, },
      .front_face = false,
    };
    const auto culled = EvaluateMaterial(input);
    ASSERT_TRUE(culled.has_value());
    EXPECT_FALSE(culled->fragment_visible);
    EXPECT_NEAR(culled->normal.y, -1.0 / std::sqrt(5.0), 1.0e-14);
    EXPECT_NEAR(culled->normal.z, 2.0 / std::sqrt(5.0), 1.0e-14);
    input.double_sided = true;
    const auto back = EvaluateMaterial(input);
    ASSERT_TRUE(back.has_value());
    EXPECT_TRUE(back->fragment_visible);
    EXPECT_EQ(back->normal.x, -culled->normal.x);
    EXPECT_EQ(back->normal.y, -culled->normal.y);
    EXPECT_EQ(back->normal.z, -culled->normal.z);
  }

  NOLINT_TEST(MaterialEvaluationReferenceTest,
    SeparateScalarsClampAndAlphaCutoffIncludesEquality)
  {
    auto input = MaterialEvaluationInput {
      .factors = { .base_color = { .alpha = 0.5 }, .metallic = 0.7 },
      .samples = { .metallic = 2.0, .roughness = -1.0, .occlusion = 3.0 },
      .alpha_test = true,
    };
    const auto kept = EvaluateMaterial(input);
    ASSERT_TRUE(kept.has_value());
    EXPECT_EQ(kept->material.metallic, 0.7);
    EXPECT_EQ(kept->material.roughness.get(), 0.0);
    EXPECT_EQ(kept->ambient_occlusion, 1.0);
    EXPECT_TRUE(kept->fragment_visible);
    input.samples.base_color = LinearRgba { .alpha = 0.5 };
    const auto clipped = EvaluateMaterial(input);
    ASSERT_TRUE(clipped.has_value());
    EXPECT_FALSE(clipped->fragment_visible);
    EXPECT_EQ(clipped->alpha, 0.25);
  }

  NOLINT_TEST(MaterialEvaluationReferenceTest,
    DisabledSamplesAndDegenerateBasisHaveDefinedResults)
  {
    auto input = MaterialEvaluationInput {
      .samples
      = { .normal = LinearRgb { .red = 0.5, .green = 0.5, .blue = 0.5 } },
      .basis = { .normal = { .x = 0.0, .y = 0.0, .z = 0.0 },
        .tangent = {},
        .bitangent = {}, },
    };
    const auto fallback = EvaluateMaterial(input);
    ASSERT_TRUE(fallback.has_value());
    EXPECT_EQ(fallback->normal.x, 0.0);
    EXPECT_EQ(fallback->normal.y, 0.0);
    EXPECT_EQ(fallback->normal.z, 1.0);
    input.samples.normal->red = std::numeric_limits<double>::quiet_NaN();
    EXPECT_FALSE(EvaluateMaterial(input));
    input.textures_enabled = false;
    const auto ignored = EvaluateMaterial(input);
    ASSERT_TRUE(ignored.has_value());
    EXPECT_EQ(ignored->normal.z, 1.0);
    EXPECT_EQ(ignored->material.base_color.red, 1.0);
  }

  NOLINT_TEST(
    MaterialEvaluationReferenceTest, InvalidInputsAndEmissionOverflowFail)
  {
    auto input = MaterialEvaluationInput {};
    input.factors.normal_scale = std::numeric_limits<double>::quiet_NaN();
    EXPECT_FALSE(EvaluateMaterial(input));
    input.factors.normal_scale = 1.0;
    input.factors.base_color.rgb.red = 1.1;
    EXPECT_FALSE(EvaluateMaterial(input));
    input.factors.base_color.rgb.red = 1.0;
    input.factors.emissive.red = std::numeric_limits<double>::max();
    input.samples.emissive = LinearRgb { .red = 2.0 };
    const auto overflow = EvaluateMaterial(input);
    ASSERT_FALSE(overflow.has_value());
    EXPECT_EQ(overflow.error(), BrdfReferenceError::kUnrepresentable);
  }

  NOLINT_TEST(
    MaterialEvaluationReferenceTest, UvTransformOrderAndFailureAreExplicit)
  {
    const auto transformed = TransformMaterialUv({ .u = 0.25, .v = 0.5 },
      {
        .scale = { 2.0, -3.0 },
        .rotation = UvRotationRadians { std::numbers::pi / 2.0 },
        .offset = { .u = -0.5, .v = 0.25 },
      });
    ASSERT_TRUE(transformed.has_value());
    EXPECT_NEAR(transformed->u, 1.0, 1.0e-14);
    EXPECT_NEAR(transformed->v, 0.75, 1.0e-14);
    EXPECT_FALSE(TransformMaterialUv(
      { .u = std::numeric_limits<double>::quiet_NaN() }, {}));
    const auto overflow = TransformMaterialUv(
      { .u = 2.0 }, { .scale = { std::numeric_limits<double>::max(), 1.0 } });
    ASSERT_FALSE(overflow.has_value());
    EXPECT_EQ(overflow.error(), BrdfReferenceError::kUnrepresentable);
  }
} // namespace
} // namespace oxygen::vortex::testing::reference
