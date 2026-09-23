//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <type_traits>
#include <utility>

#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/Photometry.h>

namespace oxygen::vortex::testing::reference {
namespace {
  static_assert(
    !std::is_convertible_v<LuminousFluxLumens, LuminousIntensityCandela>);
  static_assert(
    !std::is_convertible_v<IlluminanceLux, LuminousIntensityCandela>);
  static_assert(!std::is_convertible_v<DistanceMetres, InfluenceRangeMetres>);
  static_assert(
    !std::is_convertible_v<InnerHalfAngleRadians, OuterHalfAngleRadians>);

  NOLINT_TEST(
    PhotometryReferenceTest, PointAndDirectionalUnitsAndSourceCompensation)
  {
    constexpr auto pi = std::numbers::pi_v<double>;
    for (const auto ev : { -1.0, 0.0, 0.25, 1.0 }) {
      const auto point = ResolvePointIntensity(
        LuminousFluxLumens { 400.0 * pi }, SourceExposureEv { ev });
      const auto directional = ResolveDirectionalIlluminance(
        IlluminanceLux { 100.0 }, SourceExposureEv { ev });
      ASSERT_TRUE(point.has_value());
      ASSERT_TRUE(directional.has_value());
      EXPECT_NEAR(point->get(), 100.0 * std::exp2(ev), 1.0e-12);
      EXPECT_EQ(point->get(), directional->get());
    }
    const auto tiny_brightened = ResolvePointIntensity(
      LuminousFluxLumens { std::scalbn(4.0 * pi, -1000) },
      SourceExposureEv { 1500.0 });
    ASSERT_TRUE(tiny_brightened.has_value());
    EXPECT_EQ(tiny_brightened->get(), std::scalbn(1.0, 500));
    const auto large_dimmed = ResolveDirectionalIlluminance(
      IlluminanceLux { std::scalbn(1.0, 1000) }, SourceExposureEv { -1500.0 });
    ASSERT_TRUE(large_dimmed.has_value());
    EXPECT_EQ(large_dimmed->get(), std::scalbn(1.0, -500));
    const auto zero = ResolvePointIntensity(LuminousFluxLumens { 0.0 },
      SourceExposureEv { std::numeric_limits<double>::max() });
    ASSERT_TRUE(zero.has_value());
    EXPECT_EQ(zero->get(), 0.0);
  }

  NOLINT_TEST(PhotometryReferenceTest, AngularIntegrationConservesSpotFlux)
  {
    constexpr auto pi = std::numbers::pi_v<double>;
    constexpr unsigned intervals = 4096U;
    constexpr double flux = 1700.0;
    double maximum_relative_error = 0.0;
    for (const auto& [inner, outer] : {
           std::pair { 0.0, 0.5 },
           std::pair { 0.1, 0.5 },
           std::pair { 0.49, 0.5 },
           std::pair { 0.5, 0.5 },
           std::pair { 0.0, pi / 2.0 },
           std::pair { 0.25, pi / 2.0 },
           std::pair { 0.0, 1.0e-8 },
           std::pair { 1.0e-5, 1.0e-5 + 1.0e-12 },
         }) {
      SCOPED_TRACE(inner);
      SCOPED_TRACE(outer);
      const auto cone = SpotCone {
        .inner = InnerHalfAngleRadians { inner },
        .outer = OuterHalfAngleRadians { outer },
      };
      const auto peak
        = ResolveSpotPeakIntensity(LuminousFluxLumens { flux }, cone);
      ASSERT_TRUE(peak.has_value());
      // Integrate emitted intensity * sin(theta) dtheta dphi in angular
      // coordinates, independently of the analytic cosine/half-angle integral.
      // Split at the inner cone so a derivative discontinuity is not hidden.
      double integral = 0.0;
      for (const auto& [start, end] :
        { std::pair { 0.0, inner }, std::pair { inner, outer } }) {
        if (start == end) {
          continue;
        }
        const auto step = (end - start) / intervals;
        double sum = 0.0;
        for (unsigned sample = 0U; sample <= intervals; ++sample) {
          const auto theta = start + (static_cast<double>(sample) * step);
          const auto angular
            = SpotAngularWeight(cone, OffAxisAngleRadians { theta });
          ASSERT_TRUE(angular.has_value());
          double weight = 2.0;
          if (sample == 0U || sample == intervals) {
            weight = 1.0;
          } else if (sample % 2U != 0U) {
            weight = 4.0;
          }
          sum += weight * peak->get() * *angular * std::sin(theta);
        }
        integral += 2.0 * pi * step * sum / 3.0;
      }
      const auto relative_error = std::abs(integral - flux) / flux;
      EXPECT_LT(relative_error, 1.0e-10);
      maximum_relative_error = std::max(maximum_relative_error, relative_error);
      const auto restored_flux = SpotFluxFromPeakIntensity(*peak, cone);
      ASSERT_TRUE(restored_flux.has_value());
      EXPECT_NEAR(restored_flux->get(), flux, 1.0e-10);
    }
    ::testing::Test::RecordProperty(
      "maximum_flux_relative_error", maximum_relative_error);
  }

  NOLINT_TEST(
    PhotometryReferenceTest, SpotLimitsAndInnerConeAffectPeakIntensity)
  {
    constexpr auto pi = std::numbers::pi_v<double>;
    const auto hemisphere = SpotCone {
      .inner = InnerHalfAngleRadians { 0.0 },
      .outer = OuterHalfAngleRadians { pi / 2.0 },
    };
    const auto solid_angle = SpotEffectiveSolidAngle(hemisphere);
    ASSERT_TRUE(solid_angle.has_value());
    EXPECT_NEAR(*solid_angle, 2.0 * pi / 3.0, 1.0e-14);
    const auto boundary
      = SpotAngularWeight(hemisphere, OffAxisAngleRadians { pi / 2.0 });
    ASSERT_TRUE(boundary.has_value());
    EXPECT_EQ(*boundary, 0.0);
    const auto mid
      = SpotAngularWeight(hemisphere, OffAxisAngleRadians { pi / 4.0 });
    ASSERT_TRUE(mid.has_value());
    EXPECT_NEAR(*mid, 0.5, 1.0e-15);
    const auto hard = SpotCone {
      .inner = InnerHalfAngleRadians { 0.5 },
      .outer = OuterHalfAngleRadians { 0.5 },
    };
    const auto soft = SpotCone {
      .inner = InnerHalfAngleRadians { 0.0 },
      .outer = OuterHalfAngleRadians { 0.5 },
    };
    const auto hard_peak
      = ResolveSpotPeakIntensity(LuminousFluxLumens { 100.0 }, hard);
    const auto soft_peak
      = ResolveSpotPeakIntensity(LuminousFluxLumens { 100.0 }, soft);
    ASSERT_TRUE(hard_peak.has_value());
    ASSERT_TRUE(soft_peak.has_value());
    EXPECT_NEAR(soft_peak->get(), 3.0 * hard_peak->get(), 1.0e-12);
    // An outer-angle-only solid angle cannot conserve this soft profile's flux.
    EXPECT_GT(std::abs(soft_peak->get() - hard_peak->get()), hard_peak->get());
    const auto edge = SpotAngularWeight(hard, OffAxisAngleRadians { 0.5 });
    ASSERT_TRUE(edge.has_value());
    EXPECT_EQ(*edge, 1.0);
    const auto beyond = SpotAngularWeight(
      hard, OffAxisAngleRadians { std::nextafter(0.5, 1.0) });
    ASSERT_TRUE(beyond.has_value());
    EXPECT_EQ(*beyond, 0.0);
  }

  NOLINT_TEST(
    PhotometryReferenceTest, DistanceWindowAndGuardMatchPhysicalContract)
  {
    const auto half_range = PunctualDistanceFactor(
      DistanceMetres { 2.0 }, InfluenceRangeMetres { 4.0 });
    ASSERT_TRUE(half_range.has_value());
    EXPECT_EQ(*half_range, 225.0 / 1024.0);
    const auto guarded = PunctualDistanceFactor(
      DistanceMetres { 0.0005 }, InfluenceRangeMetres { 1.0 });
    ASSERT_TRUE(guarded.has_value());
    const auto window = 1.0 - std::pow(0.0005, 4);
    EXPECT_NEAR(*guarded, window * window / 1.0e-6, 1.0e-9);
    for (const auto& [distance, range] : {
           std::pair { 0.0, 1.0 },
           std::pair { 1.0, 0.0 },
           std::pair { 1.0, 1.0 },
           std::pair { 2.0, 1.0 },
         }) {
      const auto result = PunctualDistanceFactor(
        DistanceMetres { distance }, InfluenceRangeMetres { range });
      ASSERT_TRUE(result.has_value());
      EXPECT_EQ(*result, 0.0);
    }
    const auto first = PunctualDistanceFactor(
      DistanceMetres { 2.0 }, InfluenceRangeMetres { 1.0e6 });
    const auto second = PunctualDistanceFactor(
      DistanceMetres { 4.0 }, InfluenceRangeMetres { 1.0e6 });
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_NEAR(*first, 4.0 * *second, 1.0e-14);
    const auto close_to_range
      = PunctualDistanceFactor(DistanceMetres { std::nextafter(1.0, 0.0) },
        InfluenceRangeMetres { 1.0 });
    ASSERT_TRUE(close_to_range.has_value());
    EXPECT_GT(*close_to_range, 0.0);
  }

  NOLINT_TEST(
    PhotometryReferenceTest, InvalidAndUnrepresentableInputsFailExplicitly)
  {
    constexpr auto pi = std::numbers::pi_v<double>;
    for (const auto& [inner, outer] : {
           std::pair { -0.1, 0.5 },
           std::pair { 0.0, 0.0 },
           std::pair { 0.5, 0.1 },
           std::pair { pi / 2.0, pi / 2.0 },
           std::pair { 0.0, 2.0 },
         }) {
      const auto cone = SpotCone {
        .inner = InnerHalfAngleRadians { inner },
        .outer = OuterHalfAngleRadians { outer },
      };
      const auto result = SpotEffectiveSolidAngle(cone);
      ASSERT_FALSE(result.has_value());
      EXPECT_EQ(result.error(), PhotometryError::kInvalidInput);
      EXPECT_FALSE(ResolveSpotPeakIntensity(LuminousFluxLumens { 0.0 }, cone));
    }
    for (const auto invalid : {
           -1.0,
           std::numeric_limits<double>::quiet_NaN(),
           std::numeric_limits<double>::infinity(),
         }) {
      EXPECT_FALSE(ResolvePointIntensity(LuminousFluxLumens { invalid }));
      EXPECT_FALSE(ResolveDirectionalIlluminance(IlluminanceLux { invalid }));
      EXPECT_FALSE(PunctualDistanceFactor(
        DistanceMetres { invalid }, InfluenceRangeMetres { 1.0 }));
      EXPECT_FALSE(PunctualDistanceFactor(
        DistanceMetres { 1.0 }, InfluenceRangeMetres { invalid }));
      EXPECT_FALSE(SpotAngularWeight({}, OffAxisAngleRadians { invalid }));
    }
    const auto overflow = ResolveDirectionalIlluminance(
      IlluminanceLux { std::numeric_limits<double>::max() },
      SourceExposureEv { 1.0 });
    ASSERT_FALSE(overflow.has_value());
    EXPECT_EQ(overflow.error(), PhotometryError::kUnrepresentable);
    const auto underflow = ResolveDirectionalIlluminance(
      IlluminanceLux { 1.0 }, SourceExposureEv { -2000.0 });
    ASSERT_FALSE(underflow.has_value());
    EXPECT_EQ(underflow.error(), PhotometryError::kUnrepresentable);
    const auto tiny_cone = SpotEffectiveSolidAngle({
      .inner = InnerHalfAngleRadians { 0.0 },
      .outer = OuterHalfAngleRadians { 1.0e-200 },
    });
    ASSERT_FALSE(tiny_cone.has_value());
    EXPECT_EQ(tiny_cone.error(), PhotometryError::kUnrepresentable);
  }
} // namespace
} // namespace oxygen::vortex::testing::reference
