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
#include <expected>
#include <numbers>
#include <span>
#include <vector>

#include <glm/ext/vector_double3.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Lighting/Internal/LightPhotometry.h>
#include <Oxygen/Vortex/Test/Lighting/LightingGpuAbiFixture.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/Photometry.h>

namespace oxygen::vortex::testing {
namespace {
  namespace production = lighting::internal;

  struct PhotometryProbeInput {
    glm::vec3 light_vector { 0.0F };
    float range_m { 0.0F };
    glm::vec3 direction_to_source { 0.0F, 0.0F, 1.0F };
    float inner_sin_half_squared { 0.0F };
    glm::vec3 emitted_axis { 0.0F, 0.0F, -1.0F };
    float outer_sin_half_squared { 0.0F };
    glm::vec3 intensity_rgb_cd { 0.0F };
    std::uint32_t is_spot { 0U };
    float inner_relative_correction { 0.0F };
    float outer_relative_correction { 0.0F };
  };
  // NOLINTBEGIN(*-magic-numbers)
  static_assert(sizeof(PhotometryProbeInput) == 72U);
  static_assert(offsetof(PhotometryProbeInput, light_vector) == 0U);
  static_assert(offsetof(PhotometryProbeInput, range_m) == 12U);
  static_assert(offsetof(PhotometryProbeInput, direction_to_source) == 16U);
  static_assert(offsetof(PhotometryProbeInput, inner_sin_half_squared) == 28U);
  static_assert(offsetof(PhotometryProbeInput, emitted_axis) == 32U);
  static_assert(offsetof(PhotometryProbeInput, outer_sin_half_squared) == 44U);
  static_assert(offsetof(PhotometryProbeInput, intensity_rgb_cd) == 48U);
  static_assert(offsetof(PhotometryProbeInput, is_spot) == 60U);
  static_assert(
    offsetof(PhotometryProbeInput, inner_relative_correction) == 64U);
  static_assert(
    offsetof(PhotometryProbeInput, outer_relative_correction) == 68U);
  // NOLINTEND(*-magic-numbers)

  NOLINT_TEST_F(LightingGpuAbiTest,
    PunctualPhotometryProbeQualifiesFactorsAndReportsBoundaryResiduals)
  {
    struct Cone {
      float inner { 0.0F };
      float outer { 0.0F }; // Zero denotes the point-source matrix row.
    };
    const auto cones = std::array {
      Cone {},
      Cone { .inner = 0.0F, .outer = 0.5F },
      Cone { .inner = 0.2F, .outer = 0.5F },
      Cone { .inner = 0.5F, .outer = 0.5F },
      Cone { .inner = 0.0F, .outer = std::numbers::pi_v<float> / 2.0F },
      Cone { .inner = 1.2F, .outer = std::numbers::pi_v<float> / 2.0F },
      Cone { .inner = 0.0F, .outer = 1.0e-5F },
    };
    auto inputs = std::vector<PhotometryProbeInput> {};
    auto expected = std::vector<std::array<double, 5>> {};
    for (const auto& cone : cones) {
      const bool spot = cone.outer > 0.0F;
      // The authored float pi/2 endpoint represents the exact hemisphere.
      const auto outer = cone.outer == std::numbers::pi_v<float> / 2.0F
        ? std::numbers::pi / 2.0
        : static_cast<double>(cone.outer);
      const auto reference_cone = reference::SpotCone {
        .inner = reference::InnerHalfAngleRadians { cone.inner },
        .outer = reference::OuterHalfAngleRadians { outer },
      };
      auto profile = production::SpotConeProfile {};
      if (spot) {
        const auto resolved
          = production::ResolveSpotConeProfile(cone.inner, cone.outer);
        ASSERT_TRUE(resolved.has_value());
        profile = *resolved;
      }
      for (const auto ev : { -2.0F, 0.0F, 3.0F }) {
        const auto modifiers = production::LightPhotometryModifiers {
          .color_rgb = { 0.0F, 0.5F, 2.0F },
          .exposure_compensation_ev = ev,
        };
        const auto intensity = spot
          ? production::ResolveSpotIntensityRgb(100.0F, profile, modifiers)
          : production::ResolvePointIntensityRgb(100.0F, modifiers);
        ASSERT_TRUE(intensity.has_value());
        const auto reference_intensity = spot
          ? reference::ResolveSpotPeakIntensity(
              reference::LuminousFluxLumens { 100.0 }, reference_cone,
              reference::SourceExposureEv { ev })
          : reference::ResolvePointIntensity(
              reference::LuminousFluxLumens { 100.0 },
              reference::SourceExposureEv { ev });
        ASSERT_TRUE(reference_intensity.has_value());
        EXPECT_EQ(intensity->x, 0.0F);
        EXPECT_NEAR(intensity->y, reference_intensity->get() * 0.5,
          reference_intensity->get() * 1.0e-5);
        EXPECT_NEAR(intensity->z, reference_intensity->get() * 2.0,
          reference_intensity->get() * 4.0e-5);
        for (const auto angle_fraction : { 0.0, 0.25, 0.75, 1.0, 1.001 }) {
          // Avoid a quantization-ambiguous hard edge. Exact axial support and
          // just-outside support remain covered for hard cones.
          if ((!spot && angle_fraction != 0.0)
            || (spot && cone.inner == cone.outer && angle_fraction == 1.0)) {
            continue;
          }
          const auto angle = angle_fraction * outer;
          const auto direction = glm::vec3 {
            -static_cast<float>(std::sin(angle)),
            0.0F,
            static_cast<float>(std::cos(angle)),
          };
          const auto actual_angle
            = std::atan2(std::abs(static_cast<double>(direction.x)),
              static_cast<double>(direction.z));
          const auto angular = spot
            ? reference::SpotAngularWeight(
                reference_cone, reference::OffAxisAngleRadians { actual_angle })
            : std::expected<double, reference::PhotometryError> { 1.0 };
          ASSERT_TRUE(angular.has_value());
          for (const auto range : { 0.0F, 0.001F, 10.0F }) {
            for (const auto distance :
              { 0.0F, 0.0005F, 0.001F, 0.01F, 1.0F, 9.999F, 10.0F, 20.0F }) {
              const auto attenuation = reference::PunctualDistanceFactor(
                reference::DistanceMetres { distance },
                reference::InfluenceRangeMetres { range });
              ASSERT_TRUE(attenuation.has_value());
              const auto scalar
                = reference_intensity->get() * *attenuation * *angular;
              inputs.push_back({
                .light_vector = { 0.0F, 0.0F, distance },
                .range_m = range,
                .direction_to_source = direction,
                .inner_sin_half_squared = profile.inner_sin_half_squared,
                .outer_sin_half_squared = profile.outer_sin_half_squared,
                .intensity_rgb_cd = *intensity,
                .is_spot = spot ? 1U : 0U,
                .inner_relative_correction = profile.inner_relative_correction,
                .outer_relative_correction = profile.outer_relative_correction,
              });
              expected.push_back(
                { *attenuation, *angular, 0.0, scalar * 0.5, scalar * 2.0 });
            }
          }
        }
      }
    }
    constexpr std::uint32_t output_words = 5U;
    ASSERT_EQ(inputs.size(), 2160U);
    const auto output = Decode({
      .records = std::as_bytes(std::span(inputs)),
      .stride = sizeof(PhotometryProbeInput),
      .record_kind = 18U,
      .decoded_words = output_words,
      .count = static_cast<std::uint32_t>(inputs.size()),
    });
    ASSERT_EQ(output.size(), inputs.size() * output_words);
    double maximum_scaled_error = 0.0;
    auto physical_failures = nlohmann::json::array();
    double maximum_physical_scaled_error = 0.0;
    for (std::size_t index = 0U; index < inputs.size(); ++index) {
      SCOPED_TRACE(index);
      for (std::size_t lane = 0U; lane < output_words; ++lane) {
        SCOPED_TRACE(lane);
        const auto wanted = expected.at(index).at(lane);
        const auto measured = static_cast<double>(
          std::bit_cast<float>(output.at((index * output_words) + lane)));
        ASSERT_TRUE(std::isfinite(measured));
        EXPECT_GE(measured, 0.0);
        if (lane < 2U) {
          const auto tolerance = (2.0e-5 * wanted) + 2.0e-7;
          EXPECT_NEAR(measured, wanted, tolerance);
          maximum_scaled_error = std::max(
            maximum_scaled_error, std::abs(measured - wanted) / tolerance);
        } else {
          // Independently verify the GPU's float product, including all three
          // uploaded intensity lanes. Physical qualification below retains
          // errors amplified by high intensity near support boundaries.
          const auto distance_factor = static_cast<double>(
            std::bit_cast<float>(output.at(index * output_words)));
          const auto angular_factor = static_cast<double>(
            std::bit_cast<float>(output.at((index * output_words) + 1U)));
          const auto rgb = inputs.at(index).intensity_rgb_cd;
          const auto intensity
            = std::array { rgb.x, rgb.y, rgb.z }.at(lane - 2U);
          const auto product = intensity * distance_factor * angular_factor;
          EXPECT_NEAR(
            measured, product, (2.0e-5 * product) + std::ldexp(1.0, -120));
          if (product == 0.0) {
            EXPECT_EQ(measured, 0.0);
          }
          const auto physical_tolerance = (0.02 * wanted) + 2.0e-5;
          const auto physical_error = std::abs(measured - wanted);
          maximum_physical_scaled_error = std::max(
            maximum_physical_scaled_error, physical_error / physical_tolerance);
          EXPECT_LE(physical_error, physical_tolerance);
          if (physical_error > physical_tolerance) {
            physical_failures.push_back({
              { "probe", index },
              { "lane", lane },
              { "expected", wanted },
              { "measured", measured },
              { "tolerance", physical_tolerance },
            });
          }
        }
      }
    }
    RecordProperty("probe_count", inputs.size());
    RecordProperty("physical_probe_schema", 1U);
    RecordProperty("maximum_fraction_of_error_budget", maximum_scaled_error);
    // A passing instrument test does not qualify a renderer with residuals.
    // EX07-C admission requires physical_budget_failures=0 in this report.
    RecordProperty("physical_budget_failures", physical_failures.size());
    RecordProperty("physical_failure_details", physical_failures.dump());
    RecordProperty(
      "maximum_physical_budget_fraction", maximum_physical_scaled_error);
  }

  NOLINT_TEST_F(LightingGpuAbiTest,
    SpotConePrecisionPreservesRotatedAndNarrowBoundaryContributions)
  {
    const auto axes = std::array {
      glm::vec3 { 0.0F, 0.0F, -1.0F },
      glm::normalize(glm::vec3 { 1.0F, 2.0F, -3.0F }),
      glm::normalize(glm::vec3 { -0.3F, 0.5F, -0.7F }),
    };
    auto inputs = std::vector<PhotometryProbeInput> {};
    auto expected = std::vector<double> {};
    for (const auto outer :
      { 1.0e-18F, 1.0e-10F, 1.0e-5F, 0.5F, std::numbers::pi_v<float> / 2.0F }) {
      for (const auto inner_fraction : { 0.0F, 0.8F, 1.0F }) {
        if (inner_fraction == 1.0F
          && outer == std::numbers::pi_v<float> / 2.0F) {
          continue;
        }
        const auto inner = inner_fraction * outer;
        const auto profile = production::ResolveSpotConeProfile(inner, outer);
        ASSERT_TRUE(profile.has_value());
        const auto outer_angle = outer == std::numbers::pi_v<float> / 2.0F
          ? std::numbers::pi / 2.0
          : static_cast<double>(outer);
        for (const auto axis : axes) {
          const auto axis_precise = glm::normalize(glm::dvec3 { axis });
          const auto tangent = glm::normalize(
            glm::cross(axis_precise, glm::dvec3 { 0.0, 1.0, 0.0 }));
          for (const auto fraction :
            { 0.0, 0.5, 0.999, 0.9999999, 1.0, 1.0000001, 1.001 }) {
            const auto theta = outer_angle * fraction;
            const auto direction = glm::vec3 { -(
              axis_precise * std::cos(theta) + tangent * std::sin(theta)) };
            // The reference normalizes the actual uploaded vectors in double,
            // independently resolving their angular separation via atan2.
            const auto ray = -glm::normalize(glm::dvec3 { direction });
            const auto actual_angle
              = std::atan2(glm::length(glm::cross(ray, axis_precise)),
                glm::dot(ray, axis_precise));
            const auto angular = reference::SpotAngularWeight(
              {
                .inner = reference::InnerHalfAngleRadians { inner },
                .outer = reference::OuterHalfAngleRadians { outer_angle },
              },
              reference::OffAxisAngleRadians { actual_angle });
            ASSERT_TRUE(angular.has_value());
            constexpr auto kIntensity = 1.0e20F;
            constexpr auto kDistance = 0.0005F;
            const auto attenuation = reference::PunctualDistanceFactor(
              reference::DistanceMetres { kDistance },
              reference::InfluenceRangeMetres { 10.0 });
            ASSERT_TRUE(attenuation.has_value());
            inputs.push_back({
              .light_vector = { 0.0F, 0.0F, kDistance },
              .range_m = 10.0F,
              .direction_to_source = direction,
              .inner_sin_half_squared = profile->inner_sin_half_squared,
              .emitted_axis = axis,
              .outer_sin_half_squared = profile->outer_sin_half_squared,
              .intensity_rgb_cd = glm::vec3 { kIntensity },
              .is_spot = 1U,
              .inner_relative_correction = profile->inner_relative_correction,
              .outer_relative_correction = profile->outer_relative_correction,
            });
            expected.push_back(kIntensity * *attenuation * *angular);
          }
        }
      }
    }
    constexpr std::uint32_t kOutputWords = 5U;
    ASSERT_EQ(inputs.size(), 294U);
    const auto decode = [&] -> std::vector<std::uint32_t> {
      return Decode({
        .records = std::as_bytes(std::span(inputs)),
        .stride = sizeof(PhotometryProbeInput),
        .record_kind = 18U,
        .decoded_words = kOutputWords,
        .count = static_cast<std::uint32_t>(inputs.size()),
      });
    };
    const auto output = decode();
    ASSERT_EQ(output.size(), inputs.size() * kOutputWords);
    auto maximum_budget_fraction = 0.0;
    auto physical_failures = nlohmann::json::array();
    for (std::size_t index = 0; index < inputs.size(); ++index) {
      SCOPED_TRACE(index);
      const auto measured = static_cast<double>(
        std::bit_cast<float>(output.at((index * kOutputWords) + 2U)));
      ASSERT_TRUE(std::isfinite(measured));
      const auto tolerance = (0.02 * expected.at(index)) + 2.0e-5;
      EXPECT_NEAR(measured, expected.at(index), tolerance);
      if (std::abs(measured - expected.at(index)) > tolerance) {
        physical_failures.push_back({
          { "probe", index },
          { "expected", expected.at(index) },
          { "measured", measured },
          { "tolerance", tolerance },
        });
      }
      maximum_budget_fraction = std::max(maximum_budget_fraction,
        std::abs(measured - expected.at(index)) / tolerance);
    }
    // Deliberately omit the additional transported precision. The instrument
    // must expose the resulting physical error, not merely decode new lanes.
    for (auto& input : inputs) {
      input.inner_relative_correction = 0.0F;
      input.outer_relative_correction = 0.0F;
    }
    const auto negative = decode();
    ASSERT_EQ(negative.size(), output.size());
    std::size_t rejected = 0;
    for (std::size_t index = 0; index < inputs.size(); ++index) {
      const auto measured = static_cast<double>(
        std::bit_cast<float>(negative.at((index * kOutputWords) + 2U)));
      if (!std::isfinite(measured)
        || std::abs(measured - expected.at(index))
          > (0.02 * expected.at(index)) + 2.0e-5) {
        ++rejected;
      }
    }
    EXPECT_GT(rejected, 0U);
    RecordProperty("probe_count", inputs.size());
    RecordProperty("physical_probe_schema", 1U);
    RecordProperty("physical_budget_failures", physical_failures.size());
    RecordProperty("physical_failure_details", physical_failures.dump());
    RecordProperty("maximum_physical_budget_fraction", maximum_budget_fraction);
    RecordProperty("missing_precision_negative_controls", rejected);
  }
} // namespace
} // namespace oxygen::vortex::testing
