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
#include <cstring>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <numbers>
#include <span>
#include <stdexcept>
#include <vector>

#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_double3.hpp>
#include <glm/geometric.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Core/Lighting/LightPhotometry.h>
#include <Oxygen/Vortex/Lighting/Types/ForwardLocalLightRecord.h>
#include <Oxygen/Vortex/Test/Lighting/LightingGpuAbiFixture.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/FiniteEmitter.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/GgxBrdf.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/GgxMoments.h>

namespace oxygen::vortex::testing {
namespace {
  struct FiniteEmitterProbeInput {
    ForwardLocalLightRecord light;
    glm::vec3 view;
    float roughness;
    ShaderVisibleIndex energy_srv;
    std::uint32_t reserved;
    float f0;
    float rho;
  };
  // NOLINTBEGIN(*-magic-numbers)
  static_assert(sizeof(FiniteEmitterProbeInput) == 112U);
  static_assert(offsetof(FiniteEmitterProbeInput, view) == 80U);
  static_assert(offsetof(FiniteEmitterProbeInput, roughness) == 92U);
  static_assert(offsetof(FiniteEmitterProbeInput, energy_srv) == 96U);
  static_assert(offsetof(FiniteEmitterProbeInput, reserved) == 100U);
  static_assert(offsetof(FiniteEmitterProbeInput, f0) == 104U);
  static_assert(offsetof(FiniteEmitterProbeInput, rho) == 108U);
  // NOLINTEND(*-magic-numbers)

  // The production payload was independently qualified against the CPU/Arb
  // moments. Reading it directly here isolates emitter integration error from
  // moment interpolation error; geometry and BRDF evaluation use B's oracle.
  class MomentTable {
  public:
    MomentTable()
    {
      const auto directory
        = std::filesystem::path(OXYGEN_LIGHTING_ABI_WORKSPACE)
        / "src/Oxygen/Vortex/Test/Lighting/Reference/Data";
      auto metadata = std::ifstream(directory / "GgxModel1.json");
      const auto description = nlohmann::json::parse(metadata);
      width_ = description.at("view_nodes").get<std::size_t>();
      height_ = description.at("roughness_nodes").get<std::size_t>();
      values_.resize(2U * height_ * (width_ + 1U));
      auto stream
        = std::ifstream(directory / "GgxModel1.bin", std::ios::binary);
      auto bytes = std::vector<char>(values_.size() * sizeof(float));
      stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
      std::memcpy(values_.data(), bytes.data(), bytes.size());
      if (!stream || stream.peek() != std::ifstream::traits_type::eof()) {
        throw std::runtime_error("Invalid independent moment payload");
      }
    }

    [[nodiscard]] auto Sample(reference::ViewCosine view,
      reference::PerceptualRoughness material_roughness,
      bool mean = false) const -> reference::GgxMomentEstimate
    {
      const auto mu = view.get();
      const auto roughness = material_roughness.get();
      const auto x = mean ? 0.0
                          : std::sqrt(std::clamp(mu, 0.0, 1.0))
          * static_cast<double>(width_ - 1U);
      const auto y = (std::max(roughness, 0.045) - 0.045) / 0.955
        * static_cast<double>(height_ - 1U);
      const auto ix = static_cast<std::size_t>(x);
      const auto iy = static_cast<std::size_t>(y);
      const auto nx = std::min(ix + 1U, width_ - 1U);
      const auto ny = std::min(iy + 1U, height_ - 1U);
      const auto fetch = [&](std::size_t row, std::size_t column,
                           std::size_t channel) -> double {
        return values_.at(mean ? (2U * ((width_ * height_) + row)) + channel
                               : (2U * ((row * width_) + column)) + channel);
      };
      const auto interpolate = [&](std::size_t channel) -> double {
        return std::lerp(std::lerp(fetch(iy, ix, channel),
                           fetch(iy, nx, channel), x - static_cast<double>(ix)),
          std::lerp(fetch(ny, ix, channel), fetch(ny, nx, channel),
            x - static_cast<double>(ix)),
          y - static_cast<double>(iy));
      };
      return {
        .directional_albedo = 1.0 - interpolate(0U),
        .schlick_moment = interpolate(1U),
      };
    }

  private:
    std::size_t width_;
    std::size_t height_;
    std::vector<float> values_;
  };

  NOLINT_TEST_F(LightingGpuAbiTest, AnalyticEmitterReportsIndependentGeometryDeviation)
  {
    const auto tables = PublishBrdfEnergyTexture();
    const auto moments = MomentTable {};
    auto inputs = std::vector<FiniteEmitterProbeInput> {};
    auto expected = std::vector<reference::BrdfLobes> {};
    struct Geometry {
      glm::vec3 center;
      float radius;
      float range;
      glm::vec3 axis;
      float inner { 0.25F };
      float outer { std::numbers::pi_v<float> / 2.0F };
      glm::vec3 view { 0.6F, 0.0F, 0.8F };
    };
    const auto geometries = std::array {
      // Opposed view/light vectors with a large source exercise the fully
      // rough half-vector limit; the raster approximation must remain finite.
      Geometry {
        .center = { -1.2F, 0, -1.6F },
        .radius = 3.0F,
        .range = 10.0F,
        .axis = { 0.6F, 0, 0.8F },
      },
      // Peaks at the source axis and inside its cap exercise the bounded
      // importance rule, including the square-root radial endpoint.
      Geometry {
        .center = { 0, 0, 2 },
        .radius = 0.7F,
        .range = 10.0F,
        .axis = { 0, 0, -1 },
        .view = { 0, 0, 1 },
      },
      Geometry {
        .center = { -1.2F, 0, 1.6F },
        .radius = 0.7F,
        .range = 10.0F,
        .axis = { 0, 0, -1 },
      },
      Geometry {
        .center = { 0.7F, 0, 1 },
        .radius = 0.1F,
        .range = 10.0F,
        .axis = { 0, 0, -1 },
        .inner = 0.61F,
        .outer = 0.785F,
      },
      Geometry {
        .center = { 0, 0, 2 },
        .radius = 0.25F,
        .range = 10.0F,
        .axis = { 0, 0, -1 },
      },
      Geometry {
        .center = { 0.6F, 0.2F, 1 },
        .radius = 0.4F,
        .range = 3.0F,
        .axis = { 0, 0, -1 },
      },
      Geometry {
        .center = { 1, 0, -0.1F },
        .radius = 0.5F,
        .range = 3.0F,
        .axis = { -1, 0, 0 },
      },
      Geometry {
        .center = { 0, 0, 2 },
        .radius = 0.5F,
        .range = 1.8F,
        .axis = { 0, 0, -1 },
      },
      Geometry {
        .center = { 0, 0, 0.0005F },
        .radius = 0.0001F,
        .range = 0.01F,
        .axis = { 0, 0, -1 },
      },
      Geometry {
        .center = { 0, 0, 0.2F },
        .radius = 0.5F,
        .range = 3.0F,
        .axis = { 0, 0, -1 },
      },
      Geometry {
        .center = { 0.2F, 0, 0 },
        .radius = 0.5F,
        .range = 3.0F,
        .axis = { 0, 0, -1 },
      },
      Geometry {
        .center = { 0, 0, -1 },
        .radius = 0.25F,
        .range = 3.0F,
        .axis = { 0, 0, -1 },
      },
      Geometry {
        .center = { 0, 0, 2 },
        .radius = 0.0F,
        .range = 10.0F,
        .axis = { 0, 0, -1 },
      },
      Geometry {
        .center = { 0, 0, 2 },
        .radius = 1.0e-6F,
        .range = 10.0F,
        .axis = { 0, 0, -1 },
      },
      Geometry {
        .center = { 0.6F, 0, 1 },
        .radius = 0.5F,
        .range = 3.0F,
        .axis = { 0, 0, -1 },
        .inner = 0.2F,
        .outer = 0.3F,
      },
      Geometry {
        .center = { 0.6F, 0, 1 },
        .radius = 0.5F,
        .range = 3.0F,
        .axis = { 0, 0, -1 },
        .inner = 0.3F,
        .outer = 0.3F,
      },
    };

    for (const auto kind : { 0U, 1U }) {
      for (const auto& geometry : geometries) {
        const auto profile = oxygen::lighting::ResolveSpotConeProfile(
          geometry.inner, geometry.outer);
        ASSERT_TRUE(profile.has_value());
        for (const auto roughness : { 0.045F, 0.25F, 1.0F }) {
          SCOPED_TRACE(kind);
          SCOPED_TRACE(inputs.size());
          const auto input = FiniteEmitterProbeInput {
            .light = { .position_ws = geometry.center,
              .range_m = geometry.range,
              .intensity_rgb_cd = glm::vec3 { 1.0F },
              .source_radius_m = geometry.radius,
              .emitted_direction_ws = geometry.axis,
              .outer_cone_cosine = profile->outer_cosine,
              .inverse_cone_cosine_width = profile->inverse_cosine_width,
              .kind = kind, },
            .view = geometry.view,
            .roughness = roughness,
            .energy_srv = tables,
            .reserved = 0U,
            .f0 = 0.04F,
            .rho = 0.7F,
          };
          const auto mean = moments.Sample(reference::ViewCosine { 0.0 },
            reference::PerceptualRoughness { roughness }, true);
          const auto view_mu = static_cast<double>(input.view.z)
            / std::hypot(input.view.x, input.view.y, input.view.z);
          const auto view = moments.Sample(reference::ViewCosine { view_mu },
            reference::PerceptualRoughness { roughness });
          const auto brdf = [&](const reference::UnitDirection& direction)
            -> std::expected<reference::BrdfLobes,
              reference::BrdfReferenceError> {
            return reference::EvaluateGgxBrdfChannel(
              { .roughness = reference::PerceptualRoughness { roughness },
                .light = reference::LightCosine { direction.z },
                .view = reference::ViewCosine { view_mu },
                .azimuth = reference::RelativeAzimuth { std::atan2(
                  direction.y, direction.x) }, },
              { .f0 = input.f0, .diffuse = input.rho },
              { .light = moments.Sample(reference::ViewCosine {direction.z}, reference::PerceptualRoughness {roughness}),
                .view = view,
                .mean = { .hemispherical_albedo = mean.directional_albedo,
                  .schlick_moment = mean.schlick_moment, }, });
          };
          const auto emitter = reference::FiniteEmitter {
            .center = { .x = geometry.center.x,
              .y = geometry.center.y,
              .z = geometry.center.z, },
            .radius = reference::SourceRadiusMetres { geometry.radius },
            .range = reference::InfluenceRangeMetres { geometry.range },
            .flux = reference::LuminousFluxLumens { kind == 0U
                ? 4.0 * std::numbers::pi
                : profile->solid_angle_sr },
          };
          const auto settings = reference::EmitterIntegrationSettings {
            .absolute_tolerance = 5.0e-8,
            .relative_tolerance = 2.5e-4,
            .initial_order = 8U,
            .maximum_order = 2048U,
            .peak_direction = reference::UnitDirection { .x
              = -std::sqrt(1.0 - (view_mu * view_mu)),
              .y = 0.0,
              .z = view_mu, },
          };
          const auto oracle_axis = glm::normalize(glm::dvec3 { geometry.axis });
          const auto oracle = kind == 0U
            ? reference::IntegratePointSphere(emitter, brdf, settings)
            : reference::IntegrateSpotDisk(emitter,
                {
                  .x = oracle_axis.x,
                  .y = oracle_axis.y,
                  .z = oracle_axis.z,
                },
                {
                  .inner = reference::InnerHalfAngleRadians { geometry.inner },
                  .outer = reference::OuterHalfAngleRadians { geometry.outer
                        == std::numbers::pi_v<float> / 2.0F
                      ? std::numbers::pi / 2.0
                      : static_cast<double>(geometry.outer) },
                },
                brdf, settings);
          ASSERT_TRUE(oracle.has_value())
            << "Emitter reference error: "
            << (oracle.has_value() ? 0 : static_cast<int>(oracle.error().reason));
          inputs.push_back(input);
          expected.push_back(oracle->radiance);
        }
      }
    }
    const auto output = Decode({
      .records = std::as_bytes(std::span(inputs)),
      .stride = sizeof(FiniteEmitterProbeInput),
      .record_kind = 25U,
      .decoded_words = 3U,
      .count = static_cast<std::uint32_t>(inputs.size()),
    });
    ASSERT_EQ(output.size(), inputs.size() * 3U);
    double maximum_fraction = 0.0;
    auto measurements = nlohmann::json::array();
    for (std::size_t index = 0; index < inputs.size(); ++index) {
      SCOPED_TRACE(index);
      const auto& oracle = expected.at(index);
      const auto lobes = std::array {
        oracle.single_scattering,
        oracle.multiple_scattering,
        oracle.diffuse,
      };
      for (std::size_t lobe = 0; lobe < 3U; ++lobe) {
        const auto measured
          = std::bit_cast<float>(output.at((index * 3U) + lobe));
        EXPECT_TRUE(std::isfinite(measured));
        if (!std::isfinite(measured)) {
          continue;
        }
        EXPECT_GE(measured, 0.0F);
        const auto tolerance = (0.01 * lobes.at(lobe)) + 5.0e-6;
        // The exact emitting-surface reference quantifies the production
        // approximation. It no longer dictates the real-time algorithm.
        maximum_fraction = std::max(
          maximum_fraction, std::abs(measured - lobes.at(lobe)) / tolerance);
        measurements.push_back({ { "case", index }, { "lobe", lobe },
          { "kind", inputs.at(index).light.kind },
          { "roughness", inputs.at(index).roughness },
          { "radius", inputs.at(index).light.source_radius_m },
          { "reference", lobes.at(lobe) }, { "measured", measured } });
        const auto& light = inputs.at(index).light;
        const auto center_distance = glm::length(light.position_ws);
        if (light.range_m == 0.0F || center_distance >= light.range_m) EXPECT_EQ(measured, 0.0F);
      }
    }
    RecordProperty("finite_emitter_cases", inputs.size());
    RecordProperty("maximum_finite_emitter_reference_deviation_scale", maximum_fraction);
    RecordProperty("lighting_model_revision", 2U);
    RecordProperty("finite_emitter_measurements", measurements.dump());
  }
} // namespace
} // namespace oxygen::vortex::testing
