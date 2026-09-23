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
#include <fstream>
#include <numbers>
#include <span>
#include <vector>

#include <glm/ext/vector_float3.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Test/Lighting/LightingGpuAbiFixture.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/GgxBrdf.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/GgxMoments.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/MaterialDecode.h>

namespace oxygen::vortex::testing {
namespace {
  struct BrdfProbeInput {
    float roughness { 1.0F };
    float light_cosine { 1.0F };
    float view_cosine { 1.0F };
    float azimuth { 0.0F };
    glm::vec3 base_color { 1.0F };
    float metallic { 0.0F };
    glm::vec3 incident_rgb { 0.5F, 1.25F, 2.0F };
    float specular { 0.5F };
  };
  // NOLINTBEGIN(*-magic-numbers)
  static_assert(sizeof(BrdfProbeInput) == 48U);
  static_assert(offsetof(BrdfProbeInput, roughness) == 0U);
  static_assert(offsetof(BrdfProbeInput, light_cosine) == 4U);
  static_assert(offsetof(BrdfProbeInput, view_cosine) == 8U);
  static_assert(offsetof(BrdfProbeInput, azimuth) == 12U);
  static_assert(offsetof(BrdfProbeInput, base_color) == 16U);
  static_assert(offsetof(BrdfProbeInput, metallic) == 28U);
  static_assert(offsetof(BrdfProbeInput, incident_rgb) == 32U);
  static_assert(offsetof(BrdfProbeInput, specular) == 44U);
  // NOLINTEND(*-magic-numbers)

  NOLINT_TEST_F(
    LightingGpuAbiTest, DirectBrdfProbeReportsIndependentOracleResiduals)
  {
    auto mean_stream = std::ifstream(OXYGEN_GGX_MEAN_CERTIFICATE_FILE);
    ASSERT_TRUE(mean_stream.good());
    const auto mean_data = nlohmann::json::parse(mean_stream);
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
