//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <numbers>
#include <span>
#include <vector>

#include <glm/ext/vector_float3.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Test/Lighting/LightingGpuAbiFixture.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/GgxBrdf.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/GgxMoments.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/MaterialDecode.h>

namespace oxygen::vortex::testing {
namespace {
  struct MomentProbeInput {
    float roughness;
    float view_cosine;
    ShaderVisibleIndex moments_srv;
    ShaderVisibleIndex means_srv;
  };
  // NOLINTBEGIN(*-magic-numbers)
  static_assert(sizeof(MomentProbeInput) == 16U);
  static_assert(offsetof(MomentProbeInput, roughness) == 0U);
  static_assert(offsetof(MomentProbeInput, view_cosine) == 4U);
  static_assert(offsetof(MomentProbeInput, moments_srv) == 8U);
  static_assert(offsetof(MomentProbeInput, means_srv) == 12U);
  // NOLINTEND(*-magic-numbers)

  NOLINT_TEST_F(LightingGpuAbiTest, MomentTexturesMatchIndependentCertificates)
  {
    struct Expected {
      double energy;
      double bias;
      double energy_radius;
      double bias_radius;
      std::size_t first_lane;
    };
    const auto tables = PublishBrdfMomentTextures();
    auto inputs = std::vector<MomentProbeInput> {};
    auto expected = std::vector<Expected> {};
    const auto append
      = [&](const nlohmann::json& cases, const bool means) -> void {
      for (const auto& entry : cases) {
        inputs.push_back({
          .roughness = entry.at("roughness").get<float>(),
          .view_cosine = means ? 0.5F : entry.at("view_cosine").get<float>(),
          .moments_srv = tables.at(0),
          .means_srv = tables.at(1),
        });
        expected.push_back({
          .energy
          = entry.at(means ? "E_avg_midpoint" : "E_midpoint").get<double>(),
          .bias
          = entry.at(means ? "B_avg_midpoint" : "B_midpoint").get<double>(),
          .energy_radius
          = entry.at(means ? "E_avg_radius" : "E_radius").get<double>(),
          .bias_radius
          = entry.at(means ? "B_avg_radius" : "B_radius").get<double>(),
          .first_lane = means ? 2U : 0U,
        });
      }
    };
    const auto workspace = std::filesystem::path(OXYGEN_LIGHTING_ABI_WORKSPACE);
    auto directional_stream = std::ifstream(workspace
      / "src/Oxygen/Vortex/Test/Lighting/Reference/GgxMomentCertificates.json");
    auto mean_stream = std::ifstream(OXYGEN_GGX_MEAN_CERTIFICATE_FILE);
    auto model_stream = std::ifstream(
      workspace / "src/Oxygen/Vortex/Lighting/Data/GgxModel1.json");
    ASSERT_TRUE(directional_stream.good());
    ASSERT_TRUE(mean_stream.good());
    ASSERT_TRUE(model_stream.good());
    append(nlohmann::json::parse(directional_stream).at("cases"), false);
    append(nlohmann::json::parse(mean_stream).at("cases"), true);
    append(nlohmann::json::parse(model_stream)
             .at("numerical_certificate")
             .at("independent_worst_samples")
             .at("cases"),
      false);
    ASSERT_EQ(inputs.size(), 69U);
    constexpr std::uint32_t kWords = 4U;
    const auto output = Decode({
      .records = std::as_bytes(std::span(inputs)),
      .stride = sizeof(MomentProbeInput),
      .record_kind = 21U,
      .decoded_words = kWords,
      .count = static_cast<std::uint32_t>(inputs.size()),
    });
    ASSERT_EQ(output.size(), inputs.size() * kWords);
    auto maximum_bound = 0.0;
    auto failures = nlohmann::json::array();
    for (std::size_t index = 0; index < inputs.size(); ++index) {
      SCOPED_TRACE(index);
      const auto& oracle = expected.at(index);
      const auto first = (index * kWords) + oracle.first_lane;
      const auto loss = std::bit_cast<float>(output.at(first));
      const auto bias = std::bit_cast<float>(output.at(first + 1U));
      ASSERT_TRUE(std::isfinite(loss));
      ASSERT_TRUE(std::isfinite(bias));
      EXPECT_GE(loss, 0.0F);
      EXPECT_GE(bias, 0.0F);
      EXPECT_LE(static_cast<double>(bias), 1.0 - loss);
      const auto energy_bound
        = std::abs((1.0 - loss) - oracle.energy) + oracle.energy_radius;
      const auto bias_bound = std::abs(bias - oracle.bias) + oracle.bias_radius;
      EXPECT_LE(energy_bound, 2.0e-4);
      EXPECT_LE(bias_bound, 2.0e-4);
      if (energy_bound > 2.0e-4 || bias_bound > 2.0e-4) {
        failures.push_back({
          { "probe", index },
          { "energy_distance_bound", energy_bound },
          { "bias_distance_bound", bias_bound },
        });
      }
      if (oracle.first_lane == 0U && inputs.at(index).view_cosine == 0.0F) {
        EXPECT_EQ(loss, 0.0F);
      }
      maximum_bound = std::max({ maximum_bound, energy_bound, bias_bound });
    }
    RecordProperty("moment_query_count", inputs.size());
    RecordProperty("maximum_moment_distance_bound", maximum_bound);
    RecordProperty("physical_probe_schema", 1U);
    RecordProperty("probe_count", inputs.size());
    RecordProperty("physical_budget_failures", failures.size());
    RecordProperty("physical_failure_details", failures.dump());
    RecordProperty("maximum_physical_budget_fraction", maximum_bound / 2.0e-4);
  }

  NOLINT_TEST_F(
    LightingGpuAbiTest, GrazingResponseRemainsFiniteWithoutClippingTheGgxPeak)
  {
    const auto tables = PublishBrdfMomentTextures();
    auto inputs = std::vector<MomentProbeInput> {};
    for (const auto roughness : { 0.045F, 0.25F, 1.0F }) {
      for (const auto cosine : {
             0.0F,
             std::numeric_limits<float>::min(),
             1.0e-30F,
             1.0e-20F,
             1.0e-10F,
             1.0e-5F,
           }) {
        inputs.push_back({
          .roughness = roughness,
          .view_cosine = cosine,
          .moments_srv = tables.at(0),
          .means_srv = tables.at(1),
        });
      }
    }
    constexpr std::uint32_t kWords = 4U;
    const auto output = Decode({
      .records = std::as_bytes(std::span(inputs)),
      .stride = sizeof(MomentProbeInput),
      .record_kind = 22U,
      .decoded_words = kWords,
      .count = static_cast<std::uint32_t>(inputs.size()),
    });
    ASSERT_EQ(output.size(), inputs.size() * kWords);
    auto failures = nlohmann::json::array();
    auto maximum_budget_fraction = 0.0;
    for (std::size_t index = 0; index < inputs.size(); ++index) {
      SCOPED_TRACE(index);
      const auto& input = inputs.at(index);
      const auto mu = static_cast<double>(input.view_cosine)
        / std::hypot(1.0, static_cast<double>(input.view_cosine));
      const auto alpha = static_cast<double>(input.roughness) * input.roughness;
      const auto a2 = alpha * alpha;
      // Opposed tangents give H=N exactly, unit F0 gives Fresnel one.
      const auto expected = mu == 0.0 ? 0.0
                                      : 1.0
          / (4.0 * std::numbers::pi * a2
            * std::sqrt((mu * mu) + (a2 * (1.0 - (mu * mu)))));
      const auto single = std::bit_cast<float>(output.at(index * kWords));
      const auto total = std::bit_cast<float>(output.at((index * kWords) + 1U));
      const auto swapped
        = std::bit_cast<float>(output.at((index * kWords) + 2U));
      EXPECT_TRUE(std::isfinite(single));
      EXPECT_TRUE(std::isfinite(total));
      EXPECT_NEAR(single, expected, (2.0e-5 * expected) + 2.0e-5);
      EXPECT_NEAR(total, swapped, (2.0e-5 * total) + 2.0e-7);
      EXPECT_EQ(std::bit_cast<float>(output.at((index * kWords) + 3U)), 0.0F);
      const auto fraction
        = std::abs(single - expected) / ((2.0e-5 * expected) + 2.0e-5);
      maximum_budget_fraction = std::max(maximum_budget_fraction, fraction);
      if (!std::isfinite(single) || fraction > 1.0) {
        failures.push_back({
          { "probe", index },
          { "expected", expected },
          { "measured", single },
        });
      }
    }
    RecordProperty("grazing_probe_count", inputs.size());
    RecordProperty("physical_probe_schema", 1U);
    RecordProperty("probe_count", inputs.size());
    RecordProperty("physical_budget_failures", failures.size());
    RecordProperty("physical_failure_details", failures.dump());
    RecordProperty("maximum_physical_budget_fraction", maximum_budget_fraction);
  }

  struct BrdfProbeInput {
    float roughness { 1.0F };
    float light_cosine { 1.0F };
    float view_cosine { 1.0F };
    float azimuth { 0.0F };
    glm::vec3 base_color { 1.0F };
    float metallic { 0.0F };
    glm::vec3 incident_rgb { 0.5F, 1.25F, 2.0F };
    float specular { 0.5F };
    ShaderVisibleIndex moments_srv { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex means_srv { kInvalidShaderVisibleIndex };
  };
  // NOLINTBEGIN(*-magic-numbers)
  static_assert(sizeof(BrdfProbeInput) == 56U);
  static_assert(offsetof(BrdfProbeInput, roughness) == 0U);
  static_assert(offsetof(BrdfProbeInput, light_cosine) == 4U);
  static_assert(offsetof(BrdfProbeInput, view_cosine) == 8U);
  static_assert(offsetof(BrdfProbeInput, azimuth) == 12U);
  static_assert(offsetof(BrdfProbeInput, base_color) == 16U);
  static_assert(offsetof(BrdfProbeInput, metallic) == 28U);
  static_assert(offsetof(BrdfProbeInput, incident_rgb) == 32U);
  static_assert(offsetof(BrdfProbeInput, specular) == 44U);
  static_assert(offsetof(BrdfProbeInput, moments_srv) == 48U);
  static_assert(offsetof(BrdfProbeInput, means_srv) == 52U);
  // NOLINTEND(*-magic-numbers)

  NOLINT_TEST_F(
    LightingGpuAbiTest, DirectBrdfProbeReportsIndependentOracleResiduals)
  {
    auto mean_stream = std::ifstream(OXYGEN_GGX_MEAN_CERTIFICATE_FILE);
    ASSERT_TRUE(mean_stream.good());
    const auto mean_data = nlohmann::json::parse(mean_stream);
    const auto tables = PublishBrdfMomentTextures();
    auto inputs = std::vector<BrdfProbeInput> {};
    auto expected = std::vector<std::array<double, 3>> {};
    auto expected_f0 = std::vector<std::array<double, 3>> {};
    for (const auto roughness : { 0.0F, 0.25F, 1.0F }) {
      auto mean = reference::GgxMeanMomentEstimate {};
      for (const auto& entry : mean_data.at("cases")) {
        if (entry.at("roughness")
          == std::max(0.045, static_cast<double>(roughness))) {
          mean.hemispherical_albedo = entry.at("E_avg_midpoint").get<double>();
          mean.schlick_moment = entry.at("B_avg_midpoint").get<double>();
        }
      }
      ASSERT_GT(mean.hemispherical_albedo, 0.0);
      for (const auto cosines : {
             std::array { 0.01F, 0.5F },
             std::array { 0.5F, 1.0F },
             std::array { 1.0F, 1.0F },
             std::array { 0.5F, 0.5F },
           }) {
        const auto light = reference::IntegrateGgxMoments(
          reference::PerceptualRoughness { roughness },
          reference::ViewCosine { cosines.at(0) });
        const auto view = reference::IntegrateGgxMoments(
          reference::PerceptualRoughness { roughness },
          reference::ViewCosine { cosines.at(1) });
        ASSERT_TRUE(light.has_value());
        ASSERT_TRUE(view.has_value());
        for (const auto azimuth : {
               0.0F,
               std::numbers::pi_v<float> / 2.0F,
               std::numbers::pi_v<float>,
             }) {
          for (const auto metallic : { 0.0F, 0.25F, 1.0F }) {
            const auto input = BrdfProbeInput {
              .roughness = roughness,
              .light_cosine = cosines.at(0),
              .view_cosine = cosines.at(1),
              .azimuth = azimuth,
              .base_color = metallic == 1.0F ? glm::vec3 { 1.0F }
                                             : glm::vec3 { 0.8F, 0.4F, 0.2F },
              .metallic = metallic,
              .moments_srv = tables.at(0),
              .means_srv = tables.at(1),
            };
            const auto material = reference::ResolveMaterialBrdf({
              .base_color = { .red = input.base_color.x,
                .green = input.base_color.y,
                .blue = input.base_color.z, },
              .metallic = metallic,
              .specular = input.specular,
              .roughness = reference::PerceptualRoughness { roughness },
            });
            ASSERT_TRUE(material.has_value());
            const auto channels
              = std::array { material->red, material->green, material->blue };
            const auto incident = std::array {
              input.incident_rgb.x,
              input.incident_rgb.y,
              input.incident_rgb.z,
            };
            auto response = std::array<double, 3> {};
            auto reflectance = std::array<double, 3> {};
            for (std::size_t channel = 0U; channel < channels.size();
              ++channel) {
              const auto lobes = reference::EvaluateGgxBrdfChannel(
                {
                  .roughness = reference::PerceptualRoughness { roughness },
                  .light = reference::LightCosine { input.light_cosine },
                  .view = reference::ViewCosine { input.view_cosine },
                  .azimuth = reference::RelativeAzimuth { azimuth },
                },
                channels.at(channel),
                { .light = *light, .view = *view, .mean = mean });
              ASSERT_TRUE(lobes.has_value());
              response.at(channel)
                = (lobes->single_scattering + lobes->multiple_scattering
                    + lobes->diffuse)
                * incident.at(channel) * input.light_cosine;
              reflectance.at(channel) = channels.at(channel).f0;
            }
            inputs.push_back(input);
            expected.push_back(response);
            expected_f0.push_back(reflectance);
          }
        }
      }
    }
    ASSERT_EQ(inputs.size(), 108U);
    constexpr std::uint32_t output_words = 12U;
    const auto output = Decode({
      .records = std::as_bytes(std::span(inputs)),
      .stride = sizeof(BrdfProbeInput),
      .record_kind = 19U,
      .decoded_words = output_words,
      .count = static_cast<std::uint32_t>(inputs.size()),
    });
    ASSERT_EQ(output.size(), inputs.size() * output_words);
    auto failures = nlohmann::json::array();
    double maximum_scaled_error = 0.0;
    unsigned anchor_channels = 0U;
    for (std::size_t index = 0U; index < inputs.size(); ++index) {
      SCOPED_TRACE(index);
      const auto& input = inputs.at(index);
      const auto read = [&](const std::size_t lane) -> double {
        return std::bit_cast<float>(output.at((index * output_words) + lane));
      };
      for (std::size_t channel = 0U; channel < 3U; ++channel) {
        EXPECT_NEAR(
          read(8U + channel), expected_f0.at(index).at(channel), 1.0e-7);
        for (const auto first_lane : { 0U, 4U }) {
          const auto measured = read(first_lane + channel);
          ASSERT_TRUE(std::isfinite(measured));
          EXPECT_GE(measured, 0.0);
          // D=1/pi at alpha=1. This nonzero analytic anchor validates the
          // instrument without freezing the current uncompensated response.
          if (channel == 0U && input.roughness == 1.0F
            && input.light_cosine == 1.0F && input.view_cosine == 1.0F) {
            EXPECT_NEAR(read(first_lane + 3U), 1.0 / std::numbers::pi, 1.0e-6);
            ++anchor_channels;
          }
          const auto wanted = expected.at(index).at(channel);
          const auto tolerance = (0.02 * wanted) + 2.0e-5;
          const auto error = std::abs(measured - wanted);
          EXPECT_LE(error, tolerance);
          maximum_scaled_error
            = std::max(maximum_scaled_error, error / tolerance);
          if (error > tolerance) {
            failures.push_back({
              { "probe", index },
              { "lane", first_lane + channel },
              { "expected", wanted },
              { "measured", measured },
              { "tolerance", tolerance },
            });
          }
        }
      }
    }
    EXPECT_EQ(anchor_channels, 18U);
    RecordProperty("probe_count", inputs.size());
    RecordProperty("physical_probe_schema", 1U);
    RecordProperty("physical_budget_failures", failures.size());
    RecordProperty("physical_failure_details", failures.dump());
    RecordProperty("maximum_physical_budget_fraction", maximum_scaled_error);
  }
} // namespace
} // namespace oxygen::vortex::testing
