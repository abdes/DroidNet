//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>
#include <vector>

#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/GgxBrdf.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/GgxMoments.h>

namespace oxygen::vortex::testing::reference {
namespace {

  NOLINT_TEST(GgxBrdfReferenceTest, EveryLobeIsReciprocal)
  {
    double maximum_relative_difference = 0.0;
    for (const auto roughness : { 0.045, 0.25, 1.0 }) {
      SCOPED_TRACE(roughness);
      const auto mean
        = IntegrateGgxMeanMoments(PerceptualRoughness { roughness });
      ASSERT_TRUE(mean.has_value());
      for (const auto& [nl, nv] : {
             std::pair { 0.001, 0.7 },
             std::pair { 0.1, 1.0 },
             std::pair { 0.6, 0.3 },
           }) {
        const auto light = IntegrateGgxMoments(
          PerceptualRoughness { roughness }, ViewCosine { nl });
        const auto view = IntegrateGgxMoments(
          PerceptualRoughness { roughness }, ViewCosine { nv });
        ASSERT_TRUE(light.has_value());
        ASSERT_TRUE(view.has_value());
        for (const auto azimuth : { 0.0, 1.0, std::numbers::pi }) {
          for (const auto material : {
                 BrdfReflectance { .f0 = 0.04, .diffuse = 0.5 },
                 BrdfReflectance { .f0 = 0.8, .diffuse = 0.0 },
                 BrdfReflectance { .f0 = 0.42, .diffuse = 0.25 },
               }) {
            const auto forward = EvaluateGgxBrdfChannel(
              {
                .roughness = PerceptualRoughness { roughness },
                .light = LightCosine { nl },
                .view = ViewCosine { nv },
                .azimuth = RelativeAzimuth { azimuth },
              },
              material, { .light = *light, .view = *view, .mean = *mean });
            const auto reverse = EvaluateGgxBrdfChannel(
              {
                .roughness = PerceptualRoughness { roughness },
                .light = LightCosine { nv },
                .view = ViewCosine { nl },
                .azimuth = RelativeAzimuth { -azimuth },
              },
              material, { .light = *view, .view = *light, .mean = *mean });
            ASSERT_TRUE(forward.has_value());
            ASSERT_TRUE(reverse.has_value());
            for (const auto& [first, second] : {
                   std::pair {
                     forward->single_scattering, reverse->single_scattering },
                   std::pair { forward->multiple_scattering,
                     reverse->multiple_scattering },
                   std::pair { forward->diffuse, reverse->diffuse },
                 }) {
              EXPECT_GE(first, 0.0);
              const auto scale = std::max(first, second);
              EXPECT_NEAR(first, second, 2.0e-7 + (2.0e-5 * scale));
              if (scale > 0.0) {
                maximum_relative_difference
                  = std::max(maximum_relative_difference,
                    std::abs(first - second) / scale);
              }
            }
          }
        }
      }
    }
    ::testing::Test::RecordProperty(
      "maximum_relative_difference", maximum_relative_difference);
  }

  NOLINT_TEST(
    GgxBrdfReferenceTest, IncomingDirectionFurnaceMatchesIntegratedResponse)
  {
    // Incoming-direction midpoint quadrature is deliberately independent of
    // the moment reference's half-vector Gauss-Legendre coordinates.
    constexpr unsigned cosine_count = 128U;
    constexpr unsigned azimuth_count = 256U;
    constexpr auto pi = std::numbers::pi_v<double>;
    double maximum_error = 0.0;
    for (const auto roughness : { 0.6, 1.0 }) {
      SCOPED_TRACE(roughness);
      auto mean = GgxMeanMomentEstimate {};
      if (roughness == 1.0) {
        // Independent exact hemispherical integrals at alpha=1.
        mean.hemispherical_albedo = (4.0 / 3.0) * (1.0 - std::numbers::ln2);
        mean.schlick_moment
          = (111.0 / 35.0) - ((32.0 / 7.0) * std::numbers::ln2);
      } else {
        const auto integrated
          = IntegrateGgxMeanMoments(PerceptualRoughness { roughness });
        ASSERT_TRUE(integrated.has_value());
        mean = *integrated;
      }
      auto incident_moments = std::vector<GgxMomentEstimate> {};
      incident_moments.reserve(cosine_count);
      for (unsigned index = 0U; index < cosine_count; ++index) {
        const auto mu = (static_cast<double>(index) + 0.5) / cosine_count;
        const auto moment = IntegrateGgxMoments(
          PerceptualRoughness { roughness }, ViewCosine { mu });
        ASSERT_TRUE(moment.has_value());
        incident_moments.push_back(*moment);
      }
      for (const auto nv : { 0.05, 0.5, 1.0 }) {
        SCOPED_TRACE(nv);
        const auto view = IntegrateGgxMoments(
          PerceptualRoughness { roughness }, ViewCosine { nv });
        ASSERT_TRUE(view.has_value());
        for (const auto material : {
               BrdfReflectance { .f0 = 1.0, .diffuse = 0.0 },
               BrdfReflectance { .f0 = 1.0, .diffuse = 1.0 },
               BrdfReflectance { .f0 = 0.04, .diffuse = 1.0 },
               BrdfReflectance { .f0 = 0.04, .diffuse = 0.5 },
               BrdfReflectance { .f0 = 0.8, .diffuse = 0.0 },
               BrdfReflectance { .f0 = 0.42, .diffuse = 0.25 },
               BrdfReflectance { .f0 = 0.0, .diffuse = 0.0 },
             }) {
          SCOPED_TRACE(material.f0);
          SCOPED_TRACE(material.diffuse);
          auto integral = BrdfLobes {};
          for (unsigned index = 0U; index < cosine_count; ++index) {
            const auto nl = (static_cast<double>(index) + 0.5) / cosine_count;
            const auto weight = nl * 2.0 * pi / (cosine_count * azimuth_count);
            for (unsigned azimuth = 0U; azimuth < azimuth_count; ++azimuth) {
              const auto phi = 2.0 * pi * (static_cast<double>(azimuth) + 0.5)
                / azimuth_count;
              const auto value = EvaluateGgxBrdfChannel(
                {
                  .roughness = PerceptualRoughness { roughness },
                  .light = LightCosine { nl },
                  .view = ViewCosine { nv },
                  .azimuth = RelativeAzimuth { phi },
                },
                material,
                {
                  .light = incident_moments.at(index),
                  .view = *view,
                  .mean = mean,
                });
              ASSERT_TRUE(value.has_value());
              ASSERT_GE(value->single_scattering, 0.0);
              ASSERT_GE(value->multiple_scattering, 0.0);
              ASSERT_GE(value->diffuse, 0.0);
              integral.single_scattering += weight * value->single_scattering;
              integral.multiple_scattering
                += weight * value->multiple_scattering;
              integral.diffuse += weight * value->diffuse;
            }
          }
          const auto favg = material.f0 + ((1.0 - material.f0) / 21.0);
          const auto k = favg * favg * mean.hemispherical_albedo
            / (1.0 - (favg * (1.0 - mean.hemispherical_albedo)));
          const auto expected_ss = (material.f0 * view->directional_albedo)
            + ((1.0 - material.f0) * view->schlick_moment);
          const auto expected_ms = k * (1.0 - view->directional_albedo);
          const auto r = expected_ss + expected_ms;
          const auto ravg = (material.f0 * mean.hemispherical_albedo)
            + ((1.0 - material.f0) * mean.schlick_moment)
            + (k * (1.0 - mean.hemispherical_albedo));
          const auto denominator = 1.0 - (material.diffuse * ravg);
          const auto expected_diffuse = denominator > 0.0
            ? material.diffuse * (1.0 - r) * (1.0 - ravg) / denominator
            : 0.0;
          EXPECT_NEAR(integral.single_scattering, expected_ss, 2.0e-3);
          EXPECT_NEAR(integral.multiple_scattering, expected_ms, 2.0e-3);
          EXPECT_NEAR(integral.diffuse, expected_diffuse, 2.0e-3);
          const auto total = integral.single_scattering
            + integral.multiple_scattering + integral.diffuse;
          const auto expected = r + expected_diffuse;
          EXPECT_NEAR(total, expected, 2.0e-3);
          EXPECT_LE(total, 1.0 + 2.0e-3);
          maximum_error = std::max(maximum_error, std::abs(total - expected));
          if (material.f0 == 1.0 || material.diffuse == 1.0) {
            EXPECT_NEAR(total, 1.0, 2.0e-3);
          }
          if (material.f0 == 1.0) {
            EXPECT_EQ(integral.diffuse, 0.0);
            // Negative control: omitting compensation loses furnace energy.
            EXPECT_LT(integral.single_scattering, 0.99);
          }
        }
      }
    }
    ::testing::Test::RecordProperty("maximum_furnace_error", maximum_error);
  }

  NOLINT_TEST(GgxBrdfReferenceTest, InvalidInputsFailAndBackFacesAreZero)
  {
    const auto directional
      = IntegrateGgxMoments(PerceptualRoughness { 1.0 }, ViewCosine { 1.0 });
    ASSERT_TRUE(directional.has_value());
    const auto moments = BrdfMoments { .light = *directional,
      .view = *directional,
      .mean = { .hemispherical_albedo = (4.0 / 3.0) * (1.0 - std::numbers::ln2),
        .schlick_moment
        = (111.0 / 35.0) - ((32.0 / 7.0) * std::numbers::ln2), }, };
    for (const auto invalid : {
           -0.1,
           1.1,
           std::numeric_limits<double>::quiet_NaN(),
           std::numeric_limits<double>::infinity(),
         }) {
      EXPECT_FALSE(EvaluateGgxBrdfChannel({}, { invalid, 0.5 }, moments));
      EXPECT_FALSE(EvaluateGgxBrdfChannel({}, { 0.04, invalid }, moments));
      EXPECT_FALSE(EvaluateGgxBrdfChannel(
        { .roughness = PerceptualRoughness { invalid } }, {}, moments));
    }
    EXPECT_FALSE(EvaluateGgxBrdfChannel({}, {}, {}));
    auto invalid_moments = moments;
    invalid_moments.light.schlick_moment = 1.0;
    EXPECT_FALSE(EvaluateGgxBrdfChannel({}, {}, invalid_moments));
    invalid_moments = moments;
    invalid_moments.mean.hemispherical_albedo = 1.0;
    EXPECT_FALSE(EvaluateGgxBrdfChannel({}, {}, invalid_moments));
    for (const auto cosine : { -1.0, 0.0 }) {
      const auto value
        = EvaluateGgxBrdfChannel({ .light = LightCosine { cosine } }, {}, {});
      ASSERT_TRUE(value.has_value());
      EXPECT_EQ(value->single_scattering, 0.0);
      EXPECT_EQ(value->multiple_scattering, 0.0);
      EXPECT_EQ(value->diffuse, 0.0);
    }
  }
} // namespace
} // namespace oxygen::vortex::testing::reference
