//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>
#include <limits>
#include <numbers>
#include <type_traits>

#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/GgxMoments.h>

namespace oxygen::vortex::testing::reference {
namespace {
  static_assert(!std::is_convertible_v<PerceptualRoughness, ViewCosine>);
  static_assert(!std::is_convertible_v<ViewCosine, PerceptualRoughness>);

  NOLINT_TEST(GgxReferenceTest, RoughSurfaceMatchesAnalyticDirectionalEnergy)
  {
    for (const double mu : { 0.0, 0.001, 0.01, 0.1, 0.5, 1.0 }) {
      SCOPED_TRACE(mu);
      const auto result
        = IntegrateGgxMoments(PerceptualRoughness { 1.0 }, ViewCosine { mu });
      ASSERT_TRUE(result.has_value())
        << "order=" << result.error().last_estimate.order
        << " change=" << result.error().last_estimate.estimated_absolute_change
        << " E=" << result.error().last_estimate.directional_albedo;
      const double expected
        = mu == 0.0 ? 1.0 : 1.0 - (mu * std::log1p(1.0 / mu));
      EXPECT_NEAR(result->directional_albedo, expected, 1.0e-8);
      EXPECT_GE(result->schlick_moment, 0.0);
      EXPECT_LE(result->schlick_moment, result->directional_albedo);
    }
  }

  NOLINT_TEST(GgxReferenceTest, RoughGrazingFresnelMatchesAnalyticMoment)
  {
    const auto result
      = IntegrateGgxMoments(PerceptualRoughness { 1.0 }, ViewCosine { 0.0 });
    ASSERT_TRUE(result.has_value())
      << "order=" << result.error().last_estimate.order
      << " change=" << result.error().last_estimate.estimated_absolute_change
      << " E=" << result.error().last_estimate.directional_albedo;
    EXPECT_NEAR(result->directional_albedo, 1.0, 1.0e-12);
    EXPECT_NEAR(result->schlick_moment, 1.0 / 21.0, 1.0e-12);
  }

  NOLINT_TEST(GgxReferenceTest, RoughNormalFresnelMatchesAnalyticAntiderivative)
  {
    // Integrate u/(1+u)*(1-sqrt((1+u)/2))^5 on [0,1], using
    // x=sqrt((1+u)/2). This polynomial/log primitive is independent of the
    // half-vector quadrature in the reference implementation.
    const auto primitive = [](const double x) -> double {
      return (-2.0 * std::log(x)) + (10.0 * x) - (8.0 * x * x)
        + (7.5 * std::pow(x, 4)) - (7.6 * std::pow(x, 5))
        + ((10.0 / 3.0) * std::pow(x, 6)) - ((4.0 / 7.0) * std::pow(x, 7));
    };
    const auto result
      = IntegrateGgxMoments(PerceptualRoughness { 1.0 }, ViewCosine { 1.0 });
    ASSERT_TRUE(result.has_value())
      << "order=" << result.error().last_estimate.order
      << " change=" << result.error().last_estimate.estimated_absolute_change
      << " E=" << result.error().last_estimate.directional_albedo;
    EXPECT_NEAR(result->schlick_moment,
      primitive(1.0) - primitive(1.0 / std::numbers::sqrt2), 1.0e-12);
  }

  NOLINT_TEST(GgxReferenceTest, RoughnessFloorMatchesExplicitMinimum)
  {
    const auto floor
      = IntegrateGgxMoments(PerceptualRoughness { 0.045 }, ViewCosine { 0.5 });
    ASSERT_TRUE(floor.has_value())
      << "order=" << floor.error().last_estimate.order
      << " change=" << floor.error().last_estimate.estimated_absolute_change
      << " E=" << floor.error().last_estimate.directional_albedo;
    for (const double roughness : { 0.0, 0.02 }) {
      const auto result = IntegrateGgxMoments(
        PerceptualRoughness { roughness }, ViewCosine { 0.5 });
      ASSERT_TRUE(result.has_value())
        << "order=" << result.error().last_estimate.order
        << " change=" << result.error().last_estimate.estimated_absolute_change
        << " E=" << result.error().last_estimate.directional_albedo;
      EXPECT_EQ(result->directional_albedo, floor->directional_albedo);
      EXPECT_EQ(result->schlick_moment, floor->schlick_moment);
    }
  }

  NOLINT_TEST(GgxReferenceTest, RefinementAgreesAcrossRoughnessAndViewAngles)
  {
    for (const double roughness : { 0.045, 0.2, 0.6, 1.0 }) {
      for (const double mu : { 0.0, 0.01, 0.5, 1.0 }) {
        SCOPED_TRACE(roughness);
        SCOPED_TRACE(mu);
        const auto coarse
          = IntegrateGgxMoments(PerceptualRoughness { roughness },
            ViewCosine { mu }, { .refinement_tolerance = 1.0e-6 });
        const auto fine = IntegrateGgxMoments(PerceptualRoughness { roughness },
          ViewCosine { mu }, { .refinement_tolerance = 1.0e-8 });
        ASSERT_TRUE(coarse.has_value())
          << "order=" << coarse.error().last_estimate.order << " change="
          << coarse.error().last_estimate.estimated_absolute_change
          << " E=" << coarse.error().last_estimate.directional_albedo;
        ASSERT_TRUE(fine.has_value())
          << "order=" << fine.error().last_estimate.order
          << " change=" << fine.error().last_estimate.estimated_absolute_change
          << " E=" << fine.error().last_estimate.directional_albedo;
        EXPECT_NEAR(
          coarse->directional_albedo, fine->directional_albedo, 1.0e-6);
        EXPECT_NEAR(coarse->schlick_moment, fine->schlick_moment, 1.0e-6);
        EXPECT_GE(fine->directional_albedo, 0.0);
        EXPECT_LE(fine->directional_albedo, 1.0 + 1.0e-10);
        EXPECT_GE(fine->schlick_moment, 0.0);
        EXPECT_LE(fine->schlick_moment, fine->directional_albedo);
      }
    }
  }

  NOLINT_TEST(GgxReferenceTest, RoughMeanMomentsMatchAnalyticIntegrals)
  {
    const auto mean = IntegrateGgxMeanMoments(PerceptualRoughness { 1.0 });
    ASSERT_TRUE(mean.has_value())
      << "order=" << mean.error().last_estimate.order << " view="
      << mean.error().failed_view.value_or(ViewCosine { -1.0 }).get();
    const auto expected_energy = (4.0 / 3.0) * (1.0 - std::numbers::ln2);
    const auto expected_bias
      = (111.0 / 35.0) - ((32.0 / 7.0) * std::numbers::ln2);
    EXPECT_NEAR(mean->hemispherical_albedo, expected_energy, 1.0e-8);
    EXPECT_NEAR(mean->schlick_moment, expected_bias, 1.0e-8);
    // The unweighted mean is 1/2 and is not the model's hemispherical mean.
    EXPECT_GT(std::abs(mean->hemispherical_albedo - 0.5), 0.05);
  }

  NOLINT_TEST(GgxReferenceTest, MeanMomentRefinementAgreesAcrossRoughness)
  {
    for (const auto roughness : { 0.045, 0.25, 0.6 }) {
      SCOPED_TRACE(roughness);
      const auto first
        = IntegrateGgxMeanMoments(PerceptualRoughness { roughness });
      ASSERT_TRUE(first.has_value())
        << "order=" << first.error().last_estimate.order << " view="
        << first.error().failed_view.value_or(ViewCosine { -1.0 }).get();
      const auto second
        = IntegrateGgxMeanMoments(PerceptualRoughness { roughness },
          {
            .refinement_tolerance = 1.0e-7,
            .initial_order = 16U,
            .maximum_order = 256U,
          });
      ASSERT_TRUE(second.has_value())
        << "order=" << second.error().last_estimate.order << " view="
        << second.error().failed_view.value_or(ViewCosine { -1.0 }).get();
      EXPECT_NEAR(
        first->hemispherical_albedo, second->hemispherical_albedo, 1.0e-6);
      EXPECT_NEAR(first->schlick_moment, second->schlick_moment, 1.0e-6);
      EXPECT_GE(second->schlick_moment, 0.0);
      EXPECT_LE(second->schlick_moment, second->hemispherical_albedo);
      EXPECT_LT(second->hemispherical_albedo, 1.0);
    }
  }

  NOLINT_TEST(
    GgxReferenceTest, InvalidInputsAndExhaustedWorkNeverProduceAnEstimate)
  {
    EXPECT_FALSE(
      IntegrateGgxMoments(PerceptualRoughness { -1.0 }, ViewCosine { 0.5 }));
    EXPECT_FALSE(
      IntegrateGgxMoments(PerceptualRoughness { 0.5 }, ViewCosine { 1.1 }));
    EXPECT_FALSE(IntegrateGgxMoments(
      PerceptualRoughness { std::numeric_limits<double>::quiet_NaN() },
      ViewCosine { 0.5 }));
    EXPECT_FALSE(IntegrateGgxMoments(PerceptualRoughness { 0.5 },
      ViewCosine { std::numeric_limits<double>::infinity() }));
    EXPECT_FALSE(IntegrateGgxMoments(PerceptualRoughness { 0.5 },
      ViewCosine { 0.5 }, { .initial_order = 5U, .maximum_order = 64U }));
    const auto exhausted
      = IntegrateGgxMoments(PerceptualRoughness { 0.045 }, ViewCosine { 0.5 },
        {
          .refinement_tolerance = 1.0e-14,
          .initial_order = 4U,
          .maximum_order = 16U,
        });
    ASSERT_FALSE(exhausted.has_value());
    EXPECT_EQ(
      exhausted.error().reason, MomentIntegrationError::kDidNotConverge);
  }

  NOLINT_TEST(GgxReferenceTest, MeanMomentFailuresRetainTheirCause)
  {
    const auto invalid = IntegrateGgxMeanMoments(PerceptualRoughness { -0.1 });
    ASSERT_FALSE(invalid.has_value());
    EXPECT_EQ(invalid.error().reason, MomentIntegrationError::kInvalidInput);
    EXPECT_FALSE(invalid.error().failed_view.has_value());
    const auto exhausted = IntegrateGgxMeanMoments(
      PerceptualRoughness { 0.045 },
      { .directional = { .refinement_tolerance = 1.0e-14,
          .initial_order = 4U, .maximum_order = 16U, }, });
    ASSERT_FALSE(exhausted.has_value());
    EXPECT_EQ(
      exhausted.error().reason, MomentIntegrationError::kDidNotConverge);
    ASSERT_TRUE(exhausted.error().failed_view.has_value());
    EXPECT_GT(exhausted.error().failed_view->get(), 0.0);
    EXPECT_LT(exhausted.error().failed_view->get(), 1.0);
    const auto outer_exhausted
      = IntegrateGgxMeanMoments(PerceptualRoughness { 1.0 },
        { .refinement_tolerance = 1.0e-14,
          .initial_order = 4U,
          .maximum_order = 16U });
    ASSERT_FALSE(outer_exhausted.has_value());
    EXPECT_EQ(
      outer_exhausted.error().reason, MomentIntegrationError::kDidNotConverge);
    EXPECT_FALSE(outer_exhausted.error().failed_view.has_value());
    EXPECT_EQ(outer_exhausted.error().last_estimate.order, 16U);
    EXPECT_GT(
      outer_exhausted.error().last_estimate.endpoint_absolute_bound, 0.0);
  }

} // namespace
} // namespace oxygen::vortex::testing::reference
