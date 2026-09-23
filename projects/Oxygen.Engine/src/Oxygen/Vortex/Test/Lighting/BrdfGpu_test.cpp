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

#include <d3d12.h>

#include <glm/ext/vector_float3.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Test/Lighting/LightingGpuAbiFixture.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/GgxBrdf.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/GgxEnergyLookup.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/GgxMoments.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/MaterialDecode.h>

namespace oxygen::vortex::testing {
namespace {
  struct MomentProbeInput {
    float roughness;
    float view_cosine;
    ShaderVisibleIndex energy_srv;
    std::uint32_t reserved;
  };
  // NOLINTBEGIN(*-magic-numbers)
  static_assert(sizeof(MomentProbeInput) == 16U);
  static_assert(offsetof(MomentProbeInput, roughness) == 0U);
  static_assert(offsetof(MomentProbeInput, view_cosine) == 4U);
  static_assert(offsetof(MomentProbeInput, energy_srv) == 8U);
  static_assert(offsetof(MomentProbeInput, reserved) == 12U);
  // NOLINTEND(*-magic-numbers)

  NOLINT_TEST_F(LightingGpuAbiTest, EnergyTextureMatchesFilteringAndReportsReferenceError)
  {
    const auto texture = PublishBrdfEnergyTexture();
    auto inputs = std::vector<MomentProbeInput> {};
    auto expected = std::vector<reference::GgxMomentEstimate> {};
    for (const auto roughness : { 0.045F, 0.1F, 0.25F, 0.5F, 0.75F, 1.0F }) {
      for (const auto cosine : { 0.0F, 0.0001F, 0.001F, 0.01F, 0.1F, 0.25F, 0.5F, 0.75F, 1.0F }) {
        inputs.push_back({ .roughness = roughness, .view_cosine = cosine,
          .energy_srv = texture, .reserved = 0U });
        const auto oracle = reference::IntegrateGgxMoments(
          reference::PerceptualRoughness { roughness }, reference::ViewCosine { cosine });
        ASSERT_TRUE(oracle.has_value());
        expected.push_back(*oracle);
      }
    }
    const auto output = Decode({ .records = std::as_bytes(std::span(inputs)),
      .stride = sizeof(MomentProbeInput), .record_kind = 21U,
      .decoded_words = 4U, .count = static_cast<std::uint32_t>(inputs.size()) });
    ASSERT_EQ(output.size(), inputs.size() * 4U);
    double maximum_energy_error = 0.0, maximum_bias_error = 0.0;
    for (std::size_t index = 0; index < inputs.size(); ++index) {
      SCOPED_TRACE(index);
      const auto energy = std::bit_cast<float>(output.at(index * 4U));
      const auto bias = std::bit_cast<float>(output.at(index * 4U + 1U));
      ASSERT_TRUE(std::isfinite(energy));
      ASSERT_TRUE(std::isfinite(bias));
      EXPECT_GT(energy, 0.0F);
      EXPECT_LE(energy, 1.0F);
      EXPECT_GE(bias, 0.0F);
      EXPECT_LE(bias, energy);
      EXPECT_EQ(output.at(index * 4U + 2U), 32U);
      EXPECT_EQ(output.at(index * 4U + 3U), 32U);
      const auto filter = reference::SampleProductionGgxEnergy(
        inputs.at(index).roughness, inputs.at(index).view_cosine);
      EXPECT_NEAR(energy, filter.moments.directional_albedo, filter.filtering_error_bound);
      EXPECT_NEAR(bias, filter.moments.schlick_moment, filter.filtering_error_bound);
      maximum_energy_error = std::max(maximum_energy_error,
        std::abs(energy - expected.at(index).directional_albedo));
      maximum_bias_error = std::max(maximum_bias_error,
        std::abs(bias - expected.at(index).schlick_moment));
    }
    const auto allocation_bytes = [&](const std::uint64_t width, const std::uint32_t height) {
      auto description = D3D12_RESOURCE_DESC {};
      description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
      description.Width = width;
      description.Height = height;
      description.DepthOrArraySize = 1U;
      description.MipLevels = 1U;
      description.Format = DXGI_FORMAT_R32G32_FLOAT;
      description.SampleDesc.Count = 1U;
      return Backend().GetCurrentDevice()->GetResourceAllocationInfo(0U, 1U, &description).SizeInBytes;
    };
    const auto backing = allocation_bytes(32U, 32U);
    EXPECT_GE(backing, 8192U);
    RecordProperty("energy_payload_bytes", 8192U);
    RecordProperty("energy_native_allocation_bytes", backing);
    RecordProperty("previous_moment_native_allocation_bytes",
      allocation_bytes(513U, 1025U) + allocation_bytes(1U, 1025U));
    RecordProperty("energy_queries", inputs.size());
    RecordProperty("maximum_energy_error", maximum_energy_error);
    RecordProperty("maximum_bias_error", maximum_bias_error);
    RecordProperty("lighting_model_revision", 2U);
  }

  NOLINT_TEST_F(
    LightingGpuAbiTest, GrazingResponseRemainsFiniteWithoutClippingTheGgxPeak)
  {
    const auto tables = PublishBrdfEnergyTexture();
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
          .energy_srv = tables,
          .reserved = 0U,
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
    RecordProperty("lighting_measurement_schema", 2U);
    RecordProperty("lighting_model_revision", 2U);
    RecordProperty("probe_count", inputs.size());
    RecordProperty("reference_budget_exceedances", failures.size());
    RecordProperty("reference_deviation_details", failures.dump());
    RecordProperty("maximum_reference_deviation_scale", maximum_budget_fraction);
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
    ShaderVisibleIndex energy_srv { kInvalidShaderVisibleIndex };
    std::uint32_t reserved { 0U };
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
  static_assert(offsetof(BrdfProbeInput, energy_srv) == 48U);
  static_assert(offsetof(BrdfProbeInput, reserved) == 52U);
  // NOLINTEND(*-magic-numbers)

  NOLINT_TEST_F(
    LightingGpuAbiTest, ViewDependentCompensationReportsReciprocityDeviation)
  {
    const auto tables = PublishBrdfEnergyTexture();
    auto inputs = std::vector<BrdfProbeInput> {};
    for (const auto roughness : { 0.045F, 0.1F, 0.25F, 0.5F, 0.75F, 1.0F }) {
      for (const auto nv : { 0.001F, 0.01F, 0.1F, 0.5F, 1.0F }) {
        for (const auto nl : { 0.001F, 0.01F, 0.1F, 0.5F, 1.0F }) {
          for (const auto phi :
            { 0.0F, 0.7F, 1.57F, std::numbers::pi_v<float> }) {
            for (const auto metallic : { 0.0F, 0.5F, 1.0F }) {
              inputs.push_back({
                .roughness = roughness,
                .light_cosine = nl,
                .view_cosine = nv,
                .azimuth = phi,
                .base_color = { 0.0F, 0.5F, 1.0F },
                .metallic = metallic,
                .energy_srv = tables,
                .reserved = 0U,
              });
            }
          }
        }
      }
    }
    constexpr std::uint32_t kWords = 18U;
    const auto output = Decode({
      .records = std::as_bytes(std::span(inputs)),
      .stride = sizeof(BrdfProbeInput),
      .record_kind = 23U,
      .decoded_words = kWords,
      .count = static_cast<std::uint32_t>(inputs.size()),
    });
    ASSERT_EQ(output.size(), inputs.size() * kWords);
    double maximum_fraction = 0.0;
    for (std::size_t index = 0; index < inputs.size(); ++index) {
      SCOPED_TRACE(index);
      for (std::size_t lane = 0; lane < 9U; ++lane) {
        const auto a = std::bit_cast<float>(output.at((index * kWords) + lane));
        const auto b
          = std::bit_cast<float>(output.at((index * kWords) + lane + 9U));
        ASSERT_TRUE(std::isfinite(a));
        ASSERT_TRUE(std::isfinite(b));
        EXPECT_GE(a, 0.0F);
        EXPECT_GE(b, 0.0F);
        const auto tolerance = (2.0e-5 * std::max(a, b)) + 2.0e-7;
        maximum_fraction
          = std::max(maximum_fraction, std::abs(a - b) / tolerance);
      }
    }
    RecordProperty("lighting_model_revision", 2U);
    RecordProperty("reciprocity_queries", inputs.size());
    RecordProperty("maximum_reciprocity_deviation_scale", maximum_fraction);
  }

  struct FurnaceProbeInput {
    float roughness;
    float view_cosine;
    float azimuth;
    float light_cosine;
    std::uint32_t order;
    ShaderVisibleIndex energy_srv;
    std::uint32_t reserved;
  };
  // NOLINTBEGIN(*-magic-numbers)
  static_assert(sizeof(FurnaceProbeInput) == 28U);
  static_assert(offsetof(FurnaceProbeInput, order) == 16U);
  static_assert(offsetof(FurnaceProbeInput, energy_srv) == 20U);
  static_assert(offsetof(FurnaceProbeInput, reserved) == 24U);
  // NOLINTEND(*-magic-numbers)

  NOLINT_TEST_F(
    LightingGpuAbiTest, IntegratedGpuLobesReportEnergyAndIndirectError)
  {
    const auto tables = PublishBrdfEnergyTexture();
    auto inputs = std::vector<FurnaceProbeInput> {};
    struct Case {
      float roughness;
      float mu;
      std::uint32_t order;
      std::size_t first;
    };
    auto cases = std::vector<Case> {};
    for (const auto roughness : { 0.045F, 0.1F, 0.25F, 0.5F, 0.75F, 1.0F }) {
      for (const auto mu : { 0.001F, 0.01F, 0.1F, 0.5F, 1.0F }) {
        for (const auto order : { 256U, 512U }) {
          cases.push_back({
            .roughness = roughness,
            .mu = mu,
            .order = order,
            .first = inputs.size(),
          });
          for (std::uint32_t angular = 0; angular < order; ++angular) {
            const auto fraction = (static_cast<float>(angular) + 0.5F)
              / static_cast<float>(order);
            inputs.push_back({
              .roughness = roughness,
              .view_cosine = mu,
              .azimuth = fraction * std::numbers::pi_v<float>,
              .light_cosine = fraction,
              .order = order,
              .energy_srv = tables,
              .reserved = 0U,
            });
          }
        }
      }
    }
    constexpr std::uint32_t kWords = 15U;
    const auto output = Decode({
      .records = std::as_bytes(std::span(inputs)),
      .stride = sizeof(FurnaceProbeInput),
      .record_kind = 24U,
      .decoded_words = kWords,
      .count = static_cast<std::uint32_t>(inputs.size()),
    });
    ASSERT_EQ(output.size(), inputs.size() * kWords);
    auto previous = std::array<double, 9> {};
    double maximum_furnace_error = 0.0;
    double maximum_indirect_error = 0.0;
    double maximum_refinement_change = 0.0;
    for (const auto& test : cases) {
      SCOPED_TRACE(test.roughness);
      SCOPED_TRACE(test.mu);
      SCOPED_TRACE(test.order);
      auto sums = std::array<double, 9> {};
      for (std::size_t angular = 0; angular < test.order; ++angular) {
        for (std::size_t lane = 0; lane < sums.size(); ++lane) {
          const auto sample = std::bit_cast<float>(
            output.at(((test.first + angular) * kWords) + lane));
          ASSERT_TRUE(std::isfinite(sample));
          EXPECT_GE(sample, 0.0F);
          sums.at(lane) += sample;
        }
      }
      if (test.order == 256U) {
        previous = sums;
        continue;
      }
      for (std::size_t lane = 0; lane < sums.size(); ++lane) {
        const auto change = std::abs(sums.at(lane) - previous.at(lane));
        maximum_refinement_change = std::max(maximum_refinement_change, change);
      }
      const auto moment = reference::IntegrateGgxMoments(
        reference::PerceptualRoughness { test.roughness },
        reference::ViewCosine { test.mu });
      ASSERT_TRUE(moment.has_value());
      for (std::size_t channel = 0; channel < 3U; ++channel) {
        const auto f0 = std::array { 0.04, 0.45, 1.0 }.at(channel);
        const auto expected_single = (f0 * moment->directional_albedo)
          + ((1.0 - f0) * moment->schlick_moment);
        const auto sampled = reference::SampleProductionGgxEnergy(test.roughness, test.mu);
        const auto weight = 1.0 + f0 * (1.0 / sampled.moments.directional_albedo - 1.0);
        // This checks implementation/integration agreement; LUT approximation
        // and energy differences are separately reported measurements.
        EXPECT_NEAR(sums.at(channel), expected_single * weight,
          4.0 * maximum_refinement_change + 4.0 * sampled.filtering_error_bound + 1.0e-4);
        const auto specular = sums.at(channel) + sums.at(channel + 3U);
        const auto diffuse = sums.at(channel + 6U);
        const auto read = [&](const std::size_t lane) -> float {
          return std::bit_cast<float>(output.at((test.first * kWords) + lane));
        };
        const auto specular_error = std::abs(specular - read(9U + channel));
        const auto diffuse_error = std::abs(diffuse - read(12U + channel));
        maximum_indirect_error
          = std::max({ maximum_indirect_error, specular_error, diffuse_error });
      }
      const auto furnace_error = std::abs(sums.at(2) + sums.at(5) - 1.0);
      EXPECT_EQ(sums.at(8), 0.0);
      maximum_furnace_error = std::max(maximum_furnace_error, furnace_error);
    }
    RecordProperty("lighting_model_revision", 2U);
    RecordProperty("integrated_material_cases", cases.size() * 3U / 2U);
    RecordProperty("maximum_furnace_error", maximum_furnace_error);
    RecordProperty("maximum_indirect_error", maximum_indirect_error);
    RecordProperty("maximum_refinement_change", maximum_refinement_change);
  }

  NOLINT_TEST_F(
    LightingGpuAbiTest, DirectBrdfProbeReportsIndependentOracleResiduals)
  {
    const auto tables = PublishBrdfEnergyTexture();
    auto inputs = std::vector<BrdfProbeInput> {};
    auto expected = std::vector<std::array<double, 3>> {};
    auto sampled_expected = std::vector<std::array<double, 3>> {};
    auto expected_f0 = std::vector<std::array<double, 3>> {};
    for (const auto roughness : { 0.0F, 0.25F, 1.0F }) {
      for (const auto cosines : {
             std::array { 0.01F, 0.5F },
             std::array { 0.5F, 1.0F },
             std::array { 1.0F, 1.0F },
             std::array { 0.5F, 0.5F },
           }) {
        const auto view = reference::IntegrateGgxMoments(
          reference::PerceptualRoughness { roughness },
          reference::ViewCosine { cosines.at(1) });
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
              .energy_srv = tables,
              .reserved = 0U,
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
            const auto lobes = reference::EvaluateRasterGgxBrdf(
              { .roughness = reference::PerceptualRoughness { roughness },
                .light = reference::LightCosine { input.light_cosine },
                .view = reference::ViewCosine { input.view_cosine },
                .azimuth = reference::RelativeAzimuth { azimuth } }, channels, *view);
            ASSERT_TRUE(lobes.has_value());
            const auto filter = reference::SampleProductionGgxEnergy(roughness, input.view_cosine);
            const auto sampled = reference::EvaluateRasterGgxBrdf(
              { .roughness = reference::PerceptualRoughness { roughness },
                .light = reference::LightCosine { input.light_cosine },
                .view = reference::ViewCosine { input.view_cosine },
                .azimuth = reference::RelativeAzimuth { azimuth } }, channels, filter.moments);
            ASSERT_TRUE(sampled.has_value());
            auto sampled_response = std::array<double, 3> {};
            for (std::size_t channel = 0U; channel < channels.size(); ++channel) {
              const auto& measured_lobes = sampled->at(channel);
              sampled_response.at(channel) = (measured_lobes.single_scattering
                + measured_lobes.multiple_scattering + measured_lobes.diffuse)
                  * incident.at(channel) * input.light_cosine;
              const auto& lobe = lobes->at(channel);
              response.at(channel) = (lobe.single_scattering + lobe.multiple_scattering + lobe.diffuse)
                * incident.at(channel) * input.light_cosine;
              reflectance.at(channel) = channels.at(channel).f0;
            }
            inputs.push_back(input);
            expected.push_back(response);
            sampled_expected.push_back(sampled_response);
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
          EXPECT_NEAR(read(channel), read(4U + channel),
            2.0e-6 * std::max(read(channel), read(4U + channel)) + 1.0e-7);
          const auto sampled = sampled_expected.at(index).at(channel);
          EXPECT_NEAR(measured, sampled, 0.005 * sampled + 1.0e-5);
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
    RecordProperty("lighting_measurement_schema", 2U);
    RecordProperty("lighting_model_revision", 2U);
    RecordProperty("reference_budget_exceedances", failures.size());
    RecordProperty("reference_deviation_details", failures.dump());
    RecordProperty("maximum_reference_deviation_scale", maximum_scaled_error);
  }
} // namespace
} // namespace oxygen::vortex::testing
