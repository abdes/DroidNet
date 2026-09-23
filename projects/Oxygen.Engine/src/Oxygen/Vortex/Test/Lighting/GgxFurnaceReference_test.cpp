//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <expected>
#include <fstream>
#include <limits>
#include <map>
#include <numbers>
#include <vector>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/GgxBrdf.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/GgxMoments.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/ReferenceQuadrature.h>

namespace oxygen::vortex::testing::reference {
namespace {
  struct SingleIntegral {
    double unit_fresnel { 0.0 };
    double schlick { 0.0 };
  };
  struct LightMomentSample {
    double cosine { 0.0 };
    double weight { 0.0 };
    GgxMomentEstimate moments {};
  };

  auto IntegrateSingle(const PerceptualRoughness roughness,
    const ViewCosine view, const std::uint32_t order)
    -> std::expected<SingleIntegral, BrdfReferenceError>
  {
    const auto mu = view.get();
    const auto view_sine = std::sqrt((1.0 - mu) * (1.0 + mu));
    const auto effective = std::max(roughness.get(), 0.045);
    const auto alpha = effective * effective;
    constexpr auto pi = std::numbers::pi_v<double>;
    auto energy = detail::Sum {};
    auto bias = detail::Sum {};
    for (const auto& angular : detail::AngularRule(order)) {
      for (const auto sign : { -1.0, 1.0 }) {
        const auto cosine_phi = sign * angular.cosine;
        const auto sine_phi = std::sin(angular.angle);
        const auto projection = view_sine * cosine_phi;
        const auto root = std::hypot(projection, mu);
        const auto maximum_tangent = projection >= 0.0
          ? (root + projection) / mu
          : mu / (root - projection);
        const auto maximum_psi = std::atan(maximum_tangent / alpha);
        const auto scale = 2.0 * maximum_psi / pi;
        for (const auto& radial : detail::AngularRule(order)) {
          // tan(theta_h)=alpha*tan(psi) resolves the NDF peak. This differs
          // from both the angle-based moment reference and Arb's incoming
          // integral.
          const auto psi = radial.angle * scale;
          const auto denominator
            = std::hypot(std::cos(psi), alpha * std::sin(psi));
          const auto sine_h = alpha * std::sin(psi) / denominator;
          const auto cosine_h = std::cos(psi) / denominator;
          const auto vh = (view_sine * sine_h * cosine_phi) + (mu * cosine_h);
          const auto lx = (2.0 * vh * sine_h * cosine_phi) - view_sine;
          const auto ly = 2.0 * vh * sine_h * sine_phi;
          const auto lz = std::clamp((2.0 * vh * cosine_h) - mu, -1.0, 1.0);
          if (lz <= 0.0) {
            continue;
          }
          const auto query = BrdfQuery {
            .roughness = roughness,
            .light = LightCosine { lz },
            .view = view,
            .azimuth = RelativeAzimuth { std::atan2(ly, lx) },
          };
          const auto unit = EvaluateGgxSingleScatteringChannel(query, 1.0);
          const auto schlick = EvaluateGgxSingleScatteringChannel(query, 0.0);
          if (!unit) {
            return std::unexpected(unit.error());
          }
          if (!schlick) {
            return std::unexpected(schlick.error());
          }
          const auto jacobian
            = 8.0 * vh * lz * sine_h * alpha / (denominator * denominator);
          const auto weight = jacobian * scale * radial.weight * angular.weight;
          energy.Add(*unit * weight);
          bias.Add(*schlick * weight);
        }
      }
    }
    return SingleIntegral {
      .unit_fresnel = energy.value,
      .schlick = bias.value,
    };
  }

  NOLINT_TEST(
    GgxFurnaceReferenceTest, SmoothAndGrazingFurnacesMatchCertifiedMoments)
  {
    auto stream = std::ifstream(OXYGEN_GGX_MOMENT_CERTIFICATE_FILE);
    ASSERT_TRUE(stream.good());
    const auto data = nlohmann::json::parse(stream);
    auto mean_stream = std::ifstream(OXYGEN_GGX_MEAN_CERTIFICATE_FILE);
    ASSERT_TRUE(mean_stream.good());
    const auto mean_data = nlohmann::json::parse(mean_stream);
    double maximum_error = 0.0;
    double maximum_furnace_error = 0.0;
    double maximum_tail_bound = 0.0;
    std::uint32_t maximum_order = 0U;
    unsigned checked = 0U;
    unsigned furnace_cases = 0U;
    auto light_samples = std::map<double, std::vector<LightMomentSample>> {};
    constexpr double grazing_cutoff = 1.0e-4;
    constexpr auto pi = std::numbers::pi_v<double>;
    for (const auto& sample : data.at("cases")) {
      const auto mu = sample.at("view_cosine").get<double>();
      if (mu == 0.0) {
        continue; // BRDF at the exact backface boundary is zero.
      }
      const auto roughness = sample.at("roughness").get<double>();
      SCOPED_TRACE(roughness);
      SCOPED_TRACE(mu);
      auto previous = SingleIntegral {};
      bool converged = false;
      double final_change = 0.0;
      for (std::uint32_t order = 16U; order <= 4096U; order *= 2U) {
        const auto integral = IntegrateSingle(
          PerceptualRoughness { roughness }, ViewCosine { mu }, order);
        ASSERT_TRUE(integral.has_value());
        const auto change = 8.0
          * std::max(std::abs(integral->unit_fresnel - previous.unit_fresnel),
            std::abs(integral->schlick - previous.schlick));
        final_change = change;
        previous = *integral;
        // The independent enclosure, not the stopping estimate, proves
        // accuracy.
        if (order == 16U || change >= 1.0e-6) {
          continue;
        }
        const auto infinity = std::numeric_limits<double>::infinity();
        const auto energy_difference
          = std::nextafter(std::abs(integral->unit_fresnel
                             - sample.at("E_midpoint").get<double>()),
            infinity);
        const auto bias_difference = std::nextafter(
          std::abs(integral->schlick - sample.at("B_midpoint").get<double>()),
          infinity);
        const auto energy_error = std::nextafter(
          energy_difference + sample.at("E_radius").get<double>(), infinity);
        const auto bias_error = std::nextafter(
          bias_difference + sample.at("B_radius").get<double>(), infinity);
        EXPECT_LE(energy_error, 1.0e-5);
        EXPECT_LE(bias_error, 1.0e-5);
        maximum_error = std::max({ maximum_error, energy_error, bias_error });
        maximum_order = std::max(maximum_order, order);
        converged = true;
        break;
      }
      ASSERT_TRUE(converged)
        << "change=" << final_change << " E residual="
        << previous.unit_fresnel - sample.at("E_midpoint").get<double>()
        << " B residual="
        << previous.schlick - sample.at("B_midpoint").get<double>();
      auto mean = GgxMeanMomentEstimate {};
      for (const auto& entry : mean_data.at("cases")) {
        if (entry.at("roughness") == roughness) {
          mean.hemispherical_albedo = entry.at("E_avg_midpoint").get<double>();
          mean.schlick_moment = entry.at("B_avg_midpoint").get<double>();
        }
      }
      ASSERT_GT(mean.hemispherical_albedo, 0.0);
      const auto [found, inserted] = light_samples.try_emplace(roughness);
      if (inserted) {
        const auto log_span = -std::log(grazing_cutoff);
        for (const auto& node : detail::AngularRule(128U)) {
          const auto cosine
            = std::exp(-log_span * (1.0 - (2.0 * node.angle / pi)));
          const auto moments
            = IntegrateGgxMoments(PerceptualRoughness { roughness },
              ViewCosine { cosine }, { .refinement_tolerance = 1.0e-7 });
          ASSERT_TRUE(moments.has_value());
          found->second.push_back({
            .cosine = cosine,
            .weight = 4.0 * cosine * cosine * log_span * node.weight / pi,
            .moments = *moments,
          });
        }
      }
      const auto view = GgxMomentEstimate {
        .directional_albedo = sample.at("E_midpoint").get<double>(),
        .schlick_moment = sample.at("B_midpoint").get<double>(),
      };
      for (const auto material : {
             BrdfReflectance { .f0 = 1.0, .diffuse = 0.0 },
             BrdfReflectance { .f0 = 1.0, .diffuse = 1.0 },
             BrdfReflectance { .f0 = 0.04, .diffuse = 1.0 },
             BrdfReflectance { .f0 = 0.04, .diffuse = 0.5 },
             BrdfReflectance { .f0 = 0.8, .diffuse = 0.0 },
             BrdfReflectance { .f0 = 0.42, .diffuse = 0.25 },
             BrdfReflectance { .f0 = 0.0, .diffuse = 0.0 },
             BrdfReflectance { .f0 = 0.999999, .diffuse = 1.0 },
           }) {
        SCOPED_TRACE(material.f0);
        SCOPED_TRACE(material.diffuse);
        auto multiple = detail::Sum {};
        auto diffuse = detail::Sum {};
        for (const auto& light : found->second) {
          const auto value = EvaluateGgxBrdfChannel(
            {
              .roughness = PerceptualRoughness { roughness },
              .light = LightCosine { light.cosine },
              .view = ViewCosine { mu },
            },
            material, { .light = light.moments, .view = view, .mean = mean });
          ASSERT_TRUE(value.has_value());
          ASSERT_GE(value->multiple_scattering, 0.0);
          ASSERT_GE(value->diffuse, 0.0);
          for (const auto azimuth : { pi / 2.0, pi }) {
            const auto rotated = EvaluateGgxBrdfChannel(
              {
                .roughness = PerceptualRoughness { roughness },
                .light = LightCosine { light.cosine },
                .view = ViewCosine { mu },
                .azimuth = RelativeAzimuth { azimuth },
              },
              material, { .light = light.moments, .view = view, .mean = mean });
            ASSERT_TRUE(rotated.has_value());
            EXPECT_EQ(rotated->multiple_scattering, value->multiple_scattering);
            EXPECT_EQ(rotated->diffuse, value->diffuse);
          }
          // The two smooth lobes are azimuth independent. Integrate their
          // actual evaluator values; pi*weight is the projected solid angle.
          multiple.Add(value->multiple_scattering * pi * light.weight);
          diffuse.Add(value->diffuse * pi * light.weight);
        }
        const auto favg = material.f0 + ((1.0 - material.f0) / 21.0);
        const auto k = favg * favg * mean.hemispherical_albedo
          / ((1.0 - favg) + (favg * mean.hemispherical_albedo));
        const auto specular = (material.f0 * view.directional_albedo)
          + ((1.0 - material.f0) * view.schlick_moment)
          + (k * (1.0 - view.directional_albedo));
        const auto transmission = ((1.0 - k) * (1.0 - view.directional_albedo))
          + ((1.0 - material.f0)
            * (view.directional_albedo - view.schlick_moment));
        const auto mean_transmission
          = ((1.0 - k) * (1.0 - mean.hemispherical_albedo))
          + ((1.0 - material.f0)
            * (mean.hemispherical_albedo - mean.schlick_moment));
        const auto denominator
          = (1.0 - material.diffuse) + (material.diffuse * mean_transmission);
        const auto expected_diffuse = denominator > 0.0
          ? material.diffuse * transmission * mean_transmission / denominator
          : 0.0;
        const auto expected_multiple = k * (1.0 - view.directional_albedo);
        // Explicitly bound the unsampled grazing tail using 0<=E,B,T<=1.
        const auto multiple_tail = grazing_cutoff * grazing_cutoff * k
          * (1.0 - view.directional_albedo) / (1.0 - mean.hemispherical_albedo);
        const auto diffuse_tail = denominator > 0.0 ? grazing_cutoff
            * grazing_cutoff * material.diffuse * transmission / denominator
                                                    : 0.0;
        const auto tail = multiple_tail + diffuse_tail;
        EXPECT_LE(
          std::abs(multiple.value - expected_multiple) + multiple_tail, 2.0e-3);
        EXPECT_LE(
          std::abs(diffuse.value - expected_diffuse) + diffuse_tail, 2.0e-3);
        const auto single = (material.f0 * previous.unit_fresnel)
          + ((1.0 - material.f0) * previous.schlick);
        const auto total = single + multiple.value + diffuse.value;
        const auto error
          = std::abs(total - (specular + expected_diffuse)) + tail;
        EXPECT_LE(error, 2.0e-3);
        EXPECT_LE(total + tail, 1.0 + 2.0e-3);
        if (material.f0 == 1.0 || material.diffuse == 1.0) {
          EXPECT_LE(std::abs(total - 1.0) + tail, 2.0e-3);
        }
        if (roughness == 1.0 && mu == 1.0 && material.f0 == 1.0) {
          EXPECT_GT(
            1.0 - single, 2.0e-3); // Omitting compensation fails the furnace.
        }
        if (roughness == 1.0 && mu == 1.0 && material.f0 == 0.04
          && material.diffuse == 1.0) {
          EXPECT_GT(specular + material.diffuse,
            1.0 + 2.0e-3); // Uncoupled Lambert exceeds unit energy.
        }
        maximum_furnace_error = std::max(maximum_furnace_error, error);
        maximum_tail_bound = std::max(maximum_tail_bound, tail);
        ++furnace_cases;
      }
      ++checked;
    }
    EXPECT_EQ(checked, 49U);
    EXPECT_EQ(furnace_cases, 392U);
    RecordProperty(
      "maximum_single_scattering_error", nlohmann::json(maximum_error).dump());
    RecordProperty("maximum_quadrature_order", maximum_order);
    RecordProperty("maximum_furnace_error_including_tail",
      nlohmann::json(maximum_furnace_error).dump());
    RecordProperty(
      "maximum_grazing_tail_bound", nlohmann::json(maximum_tail_bound).dump());
  }
} // namespace
} // namespace oxygen::vortex::testing::reference
