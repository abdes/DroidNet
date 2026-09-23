//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <expected>
#include <numbers>
#include <tuple>

#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/FiniteEmitter.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/GgxBrdf.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/GgxMoments.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/Photometry.h>

namespace oxygen::vortex::testing::reference {
namespace {
  constexpr auto kPi = std::numbers::pi_v<double>;
  auto Lambert([[maybe_unused]] const UnitDirection& direction)
    -> std::expected<BrdfLobes, BrdfReferenceError>
  {
    return BrdfLobes { .diffuse = 1.0 / kPi };
  }

  NOLINT_TEST(FiniteEmitterReferenceTest, SphereMatchesProjectedSolidAngle)
  {
    // A uniform sphere illuminates a perpendicular receiver by I/R^2,
    // independently of radius, when the cap is above the horizon and
    // unwindowed.
    for (const auto radius : { 0.0, 0.0001, 0.1, 0.5, 0.95, 0.999 }) {
      SCOPED_TRACE(radius);
      const auto result = IntegratePointSphere(
        {
          .center = { .x = 0.0, .y = 0.0, .z = 1.0 },
          .radius = SourceRadiusMetres { radius },
          .range = InfluenceRangeMetres { 1.0e6 },
          .flux = LuminousFluxLumens { 4.0 * kPi },
        },
        Lambert);
      ASSERT_TRUE(result.has_value())
        << static_cast<int>(result.error().reason)
        << " order=" << result.error().last_estimate.order;
      EXPECT_NEAR(result->radiance.diffuse, 1.0 / kPi, 1.0e-8);
      EXPECT_EQ(result->radiance.single_scattering, 0.0);
      EXPECT_EQ(result->radiance.multiple_scattering, 0.0);
    }
  }

  NOLINT_TEST(FiniteEmitterReferenceTest,
    DiskMatchesHardAndHemisphericalAnalyticIntegrals)
  {
    constexpr double radius = 0.5;
    constexpr double height = 2.0;
    const auto edge_distance = std::hypot(radius, height);
    for (const auto soft : { false, true }) {
      const auto cone = SpotCone {
        .inner = InnerHalfAngleRadians { soft ? 0.0 : 0.5 },
        .outer = OuterHalfAngleRadians { soft ? kPi / 2.0 : 0.5 },
      };
      const auto omega = SpotEffectiveSolidAngle(cone);
      ASSERT_TRUE(omega.has_value());
      const auto result = IntegrateSpotDisk(
        {
          .center = { .x = 0.0, .y = 0.0, .z = height },
          .radius = SourceRadiusMetres { radius },
          .range = InfluenceRangeMetres { 1.0e6 },
          .flux = LuminousFluxLumens { *omega },
        },
        { .x = 0.0, .y = 0.0, .z = -1.0 }, cone, Lambert);
      ASSERT_TRUE(result.has_value())
        << static_cast<int>(result.error().reason)
        << " order=" << result.error().last_estimate.order;
      const auto expected = soft
        ? 2.0 / (3.0 * radius * radius)
          * (1.0 - std::pow(height / edge_distance, 3)) / kPi
        : 2.0 / (edge_distance * (edge_distance + height)) / kPi;
      EXPECT_NEAR(result->radiance.diffuse, expected, 1.0e-8);
    }
  }

  NOLINT_TEST(
    FiniteEmitterReferenceTest, PositiveRadiusConvergesToGuardedPunctualLimit)
  {
    const auto cone = SpotCone {
      .inner = InnerHalfAngleRadians { 0.0 },
      .outer = OuterHalfAngleRadians { kPi / 2.0 },
    };
    for (const auto distance : { 0.0005, 2.0 }) {
      auto source = FiniteEmitter {
        .center = { .x = 0.0, .y = 0.0, .z = distance },
        .range = InfluenceRangeMetres { 10.0 },
        .flux = LuminousFluxLumens { 0.001 },
      };
      const auto point = IntegratePointSphere(source, Lambert);
      const auto spot = IntegrateSpotDisk(
        source, { .x = 0.0, .y = 0.0, .z = -1.0 }, cone, Lambert);
      ASSERT_TRUE(point.has_value());
      ASSERT_TRUE(spot.has_value());
      source.radius = SourceRadiusMetres { distance * 1.0e-5 };
      const auto sphere = IntegratePointSphere(source, Lambert);
      const auto disk = IntegrateSpotDisk(
        source, { .x = 0.0, .y = 0.0, .z = -1.0 }, cone, Lambert);
      ASSERT_TRUE(sphere.has_value());
      ASSERT_TRUE(disk.has_value());
      EXPECT_NEAR(sphere->radiance.diffuse, point->radiance.diffuse,
        point->radiance.diffuse * 2.0e-5);
      EXPECT_NEAR(disk->radiance.diffuse, spot->radiance.diffuse,
        spot->radiance.diffuse * 1.0e-8);
    }
  }

  NOLINT_TEST(
    FiniteEmitterReferenceTest, RadiusCanReachAboveHorizonOrInsideRange)
  {
    auto source = FiniteEmitter {
      .center = { .x = 1.0, .y = 0.0, .z = -0.2 },
      .range = InfluenceRangeMetres { 10.0 },
      .flux = LuminousFluxLumens { 1.0 },
    };
    const auto punctual = IntegratePointSphere(source, Lambert);
    ASSERT_TRUE(punctual.has_value());
    EXPECT_EQ(punctual->radiance.diffuse, 0.0);
    source.radius = SourceRadiusMetres { 0.5 };
    const auto sphere = IntegratePointSphere(source, Lambert);
    ASSERT_TRUE(sphere.has_value())
      << "order=" << sphere.error().last_estimate.order;
    EXPECT_GT(sphere->radiance.diffuse, 0.0);
    const auto disk
      = IntegrateSpotDisk(source, { .x = -1.0, .y = 0.0, .z = 0.0 },
        {
          .inner = InnerHalfAngleRadians { 0.0 },
          .outer = OuterHalfAngleRadians { kPi / 2.0 },
        },
        Lambert);
    ASSERT_TRUE(disk.has_value())
      << "order=" << disk.error().last_estimate.order;
    EXPECT_GT(disk->radiance.diffuse, 0.0);
    source = {
      .center = { .x = 0.0, .y = 0.0, .z = 2.0 },
      .radius = SourceRadiusMetres { 0.5 },
      .range = InfluenceRangeMetres { 1.6 },
      .flux = LuminousFluxLumens { 1.0 },
    };
    const auto range_cap = IntegratePointSphere(source, Lambert);
    ASSERT_TRUE(range_cap.has_value());
    EXPECT_GT(range_cap->radiance.diffuse, 0.0);
  }

  NOLINT_TEST(FiniteEmitterReferenceTest, GeometricZeroCasesDoNotEvaluateBrdf)
  {
    const auto fail_if_called =
      [](const UnitDirection&) -> std::expected<BrdfLobes, BrdfReferenceError> {
      return std::unexpected(BrdfReferenceError::kInvalidInput);
    };
    for (const auto height : { 0.0, 0.5, 1.0 }) {
      const auto result = IntegratePointSphere(
        {
          .center = { .x = 0.0, .y = 0.0, .z = height },
          .radius = SourceRadiusMetres { 1.0 },
        },
        fail_if_called);
      ASSERT_TRUE(result.has_value());
      EXPECT_EQ(result->evaluations, 0U);
    }
    for (const auto height : { -1.0, 0.0 }) {
      const auto result = IntegrateSpotDisk(
        {
          .center = { .x = 0.0, .y = 0.0, .z = height },
          .radius = SourceRadiusMetres { 1.0 },
        },
        { .x = 0.0, .y = 0.0, .z = -1.0 }, {}, fail_if_called);
      ASSERT_TRUE(result.has_value());
      EXPECT_EQ(result->evaluations, 0U);
    }
    EXPECT_TRUE(IntegratePointSphere(
      { .range = InfluenceRangeMetres { 0.0 } }, fail_if_called));
    EXPECT_TRUE(IntegrateSpotDisk({ .range = InfluenceRangeMetres { 0.0 } },
      { 0.0, 0.0, -1.0 }, {}, fail_if_called));
  }

  NOLINT_TEST(
    FiniteEmitterReferenceTest, CoupledGgxDiskMatchesIncomingAngleIntegration)
  {
    constexpr double radius = 0.4;
    constexpr double height = 2.0;
    const auto view
      = IntegrateGgxMoments(PerceptualRoughness { 1.0 }, ViewCosine { 1.0 });
    ASSERT_TRUE(view.has_value());
    const auto mean = GgxMeanMomentEstimate {
      .hemispherical_albedo = (4.0 / 3.0) * (1.0 - std::numbers::ln2),
      .schlick_moment = (111.0 / 35.0) - ((32.0 / 7.0) * std::numbers::ln2),
    };
    const auto brdf = [&](const UnitDirection& direction)
      -> std::expected<BrdfLobes, BrdfReferenceError> {
      const auto light = IntegrateGgxMoments(
        PerceptualRoughness { 1.0 }, ViewCosine { direction.z });
      if (!light) {
        return std::unexpected(BrdfReferenceError::kUnrepresentable);
      }
      return EvaluateGgxBrdfChannel({ .light = LightCosine { direction.z } },
        {}, { .light = *light, .view = *view, .mean = mean });
    };
    const auto cone = SpotCone {
      .inner = InnerHalfAngleRadians { 0.0 },
      .outer = OuterHalfAngleRadians { kPi / 2.0 },
    };
    const auto result = IntegrateSpotDisk(
      {
        .center = { .x = 0.0, .y = 0.0, .z = height },
        .radius = SourceRadiusMetres { radius },
        .range = InfluenceRangeMetres { 1.0e6 },
        .flux = LuminousFluxLumens { 2.0 * kPi / 3.0 },
      },
      { .x = 0.0, .y = 0.0, .z = -1.0 }, cone, brdf);
    ASSERT_TRUE(result.has_value());
    // Independent receiver-angle integral: projected area and inverse square
    // leave (2*I/a^2)*f(theta)*w(theta)*sin(theta) dtheta for an on-axis disk.
    constexpr unsigned intervals = 128U;
    const auto step = std::atan(radius / height) / intervals;
    auto expected = BrdfLobes {};
    for (unsigned index = 0U; index <= intervals; ++index) {
      const auto theta = static_cast<double>(index) * step;
      const auto cosine = std::cos(theta);
      const auto sample = brdf({ .x = std::sin(theta), .y = 0.0, .z = cosine });
      ASSERT_TRUE(sample.has_value());
      double weight = 2.0;
      if (index == 0U || index == intervals) {
        weight = 1.0;
      } else if (index % 2U != 0U) {
        weight = 4.0;
      }
      const auto factor = weight * 2.0 / (radius * radius) * cosine * cosine
        * std::sin(theta) * step / 3.0;
      expected.single_scattering += factor * sample->single_scattering;
      expected.multiple_scattering += factor * sample->multiple_scattering;
      expected.diffuse += factor * sample->diffuse;
    }
    EXPECT_NEAR(
      result->radiance.single_scattering, expected.single_scattering, 1.0e-8);
    EXPECT_NEAR(result->radiance.multiple_scattering,
      expected.multiple_scattering, 1.0e-8);
    EXPECT_NEAR(result->radiance.diffuse, expected.diffuse, 1.0e-8);
  }

  NOLINT_TEST(
    FiniteEmitterReferenceTest, InvalidInputsCallbackErrorsAndExhaustionFail)
  {
    EXPECT_FALSE(
      IntegratePointSphere({ .radius = SourceRadiusMetres { -1.0 } }, Lambert));
    EXPECT_FALSE(IntegrateSpotDisk({}, { 0.0, 0.0, 0.0 }, {}, Lambert));
    const auto failed = IntegratePointSphere({},
      [](const UnitDirection&) -> std::expected<BrdfLobes, BrdfReferenceError> {
        return std::unexpected(BrdfReferenceError::kUnrepresentable);
      });
    ASSERT_FALSE(failed.has_value());
    EXPECT_EQ(
      failed.error().reason, EmitterIntegrationError::kBrdfEvaluationFailed);
    EXPECT_EQ(failed.error().brdf_error, BrdfReferenceError::kUnrepresentable);
    const auto exhausted
      = IntegratePointSphere({ .radius = SourceRadiusMetres { 0.5 } },
        [](const UnitDirection& direction)
          -> std::expected<BrdfLobes, BrdfReferenceError> {
          return BrdfLobes {
            .diffuse = 1.0 + (0.5 * std::cos(200.0 * direction.x)),
          };
        },
        {
          .absolute_tolerance = 1.0e-14,
          .relative_tolerance = 0.0,
          .initial_order = 4U,
          .maximum_order = 16U,
        });
    ASSERT_FALSE(exhausted.has_value());
    EXPECT_EQ(
      exhausted.error().reason, EmitterIntegrationError::kDidNotConverge);
    EXPECT_EQ(exhausted.error().last_estimate.order, 16U);
  }
  NOLINT_TEST(
    FiniteEmitterReferenceTest, SphereWindowAndGuardMatchSurfaceAreaIntegral)
  {
    double maximum_error = 0.0;
    for (const auto& [distance, radius, range, flux] : {
           std::tuple { 2.0, 0.5, 1.6, 1.0 },
           std::tuple { 2.0, 0.5, 3.0, 1.0 },
           std::tuple { 0.0005, 0.0002, 1.0, 0.001 },
         }) {
      const auto result = IntegratePointSphere(
        {
          .center = { .x = 0.0, .y = 0.0, .z = distance },
          .radius = SourceRadiusMetres { radius },
          .range = InfluenceRangeMetres { range },
          .flux = LuminousFluxLumens { flux },
        },
        Lambert);
      ASSERT_TRUE(result.has_value());
      // Integrate over the actual emitting sphere surface, independently of
      // the reference's apparent-cap coordinate and ray/sphere Jacobian.
      constexpr unsigned intervals = 4096U;
      const auto minimum_cosine = radius / distance;
      const auto step = (1.0 - minimum_cosine) / intervals;
      double sum = 0.0;
      for (unsigned index = 0U; index <= intervals; ++index) {
        const auto cosine
          = minimum_cosine + (static_cast<double>(index) * step);
        const auto separation_squared = std::pow(distance - radius, 2)
          + (2.0 * distance * radius * (1.0 - cosine));
        const auto separation = std::sqrt(separation_squared);
        if (separation >= range) {
          continue;
        }
        const auto window = std::pow(1.0 - std::pow(separation / range, 4), 2);
        const auto geometric = (distance - (radius * cosine))
          * ((distance * cosine) - radius) / separation_squared;
        double weight = 2.0;
        if (index == 0U || index == intervals) {
          weight = 1.0;
        } else if (index % 2U != 0U) {
          weight = 4.0;
        }
        sum
          += weight * geometric * window / std::max(separation_squared, 1.0e-6);
      }
      const auto expected = flux / (2.0 * kPi * kPi) * step / 3.0 * sum;
      const auto error = std::abs(result->radiance.diffuse - expected);
      EXPECT_NEAR(
        result->radiance.diffuse, expected, 1.0e-10 + (1.0e-8 * expected));
      maximum_error = std::max(maximum_error, error);
    }
    ::testing::Test::RecordProperty(
      "maximum_surface_integral_error", maximum_error);
  }

  NOLINT_TEST(FiniteEmitterReferenceTest,
    NarrowDiskContributionSurvivesCenterConeRejection)
  {
    auto source = FiniteEmitter {
      .center = { .x = 1.0, .y = 0.0, .z = 1.0 },
      .range = InfluenceRangeMetres { 10.0 },
      .flux = LuminousFluxLumens { 1.0 },
    };
    const auto cone = SpotCone {
      .inner = InnerHalfAngleRadians { 0.0 },
      .outer = OuterHalfAngleRadians { 0.01 },
    };
    const auto point = IntegrateSpotDisk(
      source, { .x = 0.0, .y = 0.0, .z = -1.0 }, cone, Lambert);
    ASSERT_TRUE(point.has_value());
    EXPECT_EQ(point->radiance.diffuse, 0.0);
    source.radius = SourceRadiusMetres { 1.1 };
    const auto disk = IntegrateSpotDisk(
      source, { .x = 0.0, .y = 0.0, .z = -1.0 }, cone, Lambert);
    ASSERT_TRUE(disk.has_value())
      << "order=" << disk.error().last_estimate.order;
    EXPECT_GT(disk->radiance.diffuse, 0.01);
  }
  NOLINT_TEST(
    FiniteEmitterReferenceTest, NarrowSupportSurvivesRoundedUnitCosine)
  {
    const auto disk = IntegrateSpotDisk(
      {
        .center = { .x = 1.0, .y = 0.0, .z = 1.0 },
        .radius = SourceRadiusMetres { 1.1 },
        .range = InfluenceRangeMetres { 10.0 },
        .flux = LuminousFluxLumens { 1.0 },
      },
      { .x = 0.0, .y = 0.0, .z = -1.0 },
      {
        .inner = InnerHalfAngleRadians { 0.0 },
        .outer = OuterHalfAngleRadians { 1.0e-8 },
      },
      Lambert);
    ASSERT_TRUE(disk.has_value())
      << "order=" << disk.error().last_estimate.order;
    EXPECT_GT(disk->radiance.diffuse, 0.01);
  }
  NOLINT_TEST(FiniteEmitterReferenceTest,
    UnrepresentableDiskOverlapFailsInsteadOfReturningZero)
  {
    const auto result = IntegrateSpotDisk(
      {
        .center = { .x = 1.0, .y = 0.0, .z = 1.0 },
        .radius = SourceRadiusMetres { 1.1 },
        .range = InfluenceRangeMetres { 10.0 },
        .flux = LuminousFluxLumens { 1.0 },
      },
      { .x = 0.0, .y = 0.0, .z = -1.0 },
      {
        .inner = InnerHalfAngleRadians { 0.0 },
        .outer = OuterHalfAngleRadians { 1.0e-20 },
      },
      Lambert);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().reason, EmitterIntegrationError::kUnrepresentable);
  }
} // namespace
} // namespace oxygen::vortex::testing::reference
