//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cmath>
#include <cstddef>
#include <expected>
#include <limits>
#include <numbers>

#include <glm/ext/vector_float3.hpp>

#include <Oxygen/Vortex/Lighting/Internal/LightPhotometry.h>

namespace oxygen::vortex::lighting::internal {
namespace {

  constexpr double kSphereSolidAngle = 4.0 * std::numbers::pi;
  constexpr float kHemisphereHalfAngle = std::numbers::pi_v<float> / 2.0F;
  constexpr double kMinimumNormal = std::numeric_limits<float>::min();
  constexpr double kMaximumFinite = std::numeric_limits<float>::max();

  auto IsNonnegativeFinite(const double value) -> bool
  {
    return std::isfinite(value) && value >= 0.0;
  }

  auto ResolveRgb(const float authored_strength, const double solid_angle_sr,
    const LightPhotometryModifiers& modifiers)
    -> std::expected<glm::vec3, LightPhotometryError>
  {
    if (!IsNonnegativeFinite(authored_strength)
      || !std::isfinite(solid_angle_sr) || solid_angle_sr <= 0.0
      || !std::isfinite(modifiers.exposure_compensation_ev)
      || !IsNonnegativeFinite(modifiers.color_rgb.r)
      || !IsNonnegativeFinite(modifiers.color_rgb.g)
      || !IsNonnegativeFinite(modifiers.color_rgb.b)) {
      return std::unexpected(LightPhotometryError::kInvalidInput);
    }

    const auto colors = std::array {
      modifiers.color_rgb.r,
      modifiers.color_rgb.g,
      modifiers.color_rgb.b,
    };
    auto result = std::array<float, 3> {};
    for (std::size_t channel = 0; channel < result.size(); ++channel) {
      const auto tint = colors.at(channel);
      if (authored_strength == 0.0F || tint == 0.0F) {
        continue;
      }

      // Resolve each tinted component, never a possibly overflowing untinted
      // float scalar. The log check bounds the subsequent exponent conversion.
      const auto unscaled
        = static_cast<double>(authored_strength) * tint / solid_angle_sr;
      const auto log_value = std::log2(unscaled)
        + static_cast<double>(modifiers.exposure_compensation_ev);
      if (!std::isfinite(log_value) || log_value < std::log2(kMinimumNormal)
        || log_value > std::log2(kMaximumFinite)) {
        return std::unexpected(LightPhotometryError::kUnrepresentable);
      }
      const auto exponent
        = std::floor(static_cast<double>(modifiers.exposure_compensation_ev));
      const auto scaled = std::scalbn(unscaled, static_cast<int>(exponent))
        * std::exp2(modifiers.exposure_compensation_ev - exponent);
      if (!std::isfinite(scaled) || scaled < kMinimumNormal
        || scaled > kMaximumFinite) {
        return std::unexpected(LightPhotometryError::kUnrepresentable);
      }
      result.at(channel) = static_cast<float>(scaled);
    }
    return glm::vec3 { result.at(0), result.at(1), result.at(2) };
  }

  auto SquaredHalfAngleSine(const float angle) -> double
  {
    // The authored float32 half-pi endpoint denotes exactly 90 degrees.
    if (angle == kHemisphereHalfAngle) {
      return 0.5;
    }
    const auto sine = std::sin(static_cast<double>(angle) / 2.0);
    return sine * sine;
  }

} // namespace

auto ResolveSpotConeProfile(
  const float inner_half_angle_radians, const float outer_half_angle_radians)
  -> std::expected<SpotConeProfile, LightPhotometryError>
{
  if (!IsNonnegativeFinite(inner_half_angle_radians)
    || !std::isfinite(outer_half_angle_radians)
    || outer_half_angle_radians <= 0.0F
    || inner_half_angle_radians > outer_half_angle_radians
    || outer_half_angle_radians > kHemisphereHalfAngle
    || (inner_half_angle_radians == outer_half_angle_radians
      && outer_half_angle_radians == kHemisphereHalfAngle)) {
    return std::unexpected(LightPhotometryError::kInvalidInput);
  }
  const auto inner = SquaredHalfAngleSine(inner_half_angle_radians);
  const auto outer = SquaredHalfAngleSine(outer_half_angle_radians);
  const auto inner_gpu = static_cast<float>(1.0 - 2.0 * inner);
  const auto outer_gpu = static_cast<float>(1.0 - 2.0 * outer);
  const auto hard = inner_half_angle_radians == outer_half_angle_radians;
  if (outer_gpu >= 1.0F || (!hard && inner_gpu <= outer_gpu)) {
    return std::unexpected(LightPhotometryError::kUnrepresentable);
  }
  const auto inverse_width = hard ? 0.0F : 1.0F / (inner_gpu - outer_gpu);
  constexpr double kSquaredRampIntegral = 1.0 / 3.0;
  return SpotConeProfile {
    .outer_cosine = outer_gpu,
    .inverse_cosine_width = inverse_width,
    .solid_angle_sr
    = kSphereSolidAngle * (inner + ((outer - inner) * kSquaredRampIntegral)),
  };
}

auto ResolveDirectionalIlluminanceRgb(
  const float illuminance_lux, const LightPhotometryModifiers& modifiers)
  -> std::expected<glm::vec3, LightPhotometryError>
{
  return ResolveRgb(illuminance_lux, 1.0, modifiers);
}

auto ResolvePointIntensityRgb(
  const float luminous_flux_lm, const LightPhotometryModifiers& modifiers)
  -> std::expected<glm::vec3, LightPhotometryError>
{
  return ResolveRgb(luminous_flux_lm, kSphereSolidAngle, modifiers);
}

auto ResolveSpotIntensityRgb(const float luminous_flux_lm,
  const SpotConeProfile& cone, const LightPhotometryModifiers& modifiers)
  -> std::expected<glm::vec3, LightPhotometryError>
{
  return ResolveRgb(luminous_flux_lm, cone.solid_angle_sr, modifiers);
}

} // namespace oxygen::vortex::lighting::internal
