//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <expected>
#include <numbers>

#include <Oxygen/Vortex/Test/Lighting/Reference/Photometry.h>

namespace oxygen::vortex::testing::reference {
namespace {
  constexpr auto kPi = std::numbers::pi_v<double>;

  auto NonnegativeFinite(const double value) -> bool
  {
    return std::isfinite(value) && value >= 0.0;
  }

  auto ValidCone(const SpotCone& cone) -> bool
  {
    const auto inner = cone.inner.get();
    const auto outer = cone.outer.get();
    return NonnegativeFinite(inner) && std::isfinite(outer) && outer > 0.0
      && inner <= outer && outer <= kPi / 2.0
      && (inner < outer || outer < kPi / 2.0);
  }

  // cos(first)-cos(second), without subtracting nearly equal cosines.
  auto CosineDifference(const double first, const double second) -> double
  {
    return 2.0 * std::sin((first + second) / 2.0)
      * std::sin((second - first) / 2.0);
  }

  auto ScaleQuotient(const double numerator, const double denominator,
    const SourceExposureEv compensation)
    -> std::expected<double, PhotometryError>
  {
    if (!NonnegativeFinite(numerator) || !std::isfinite(compensation.get())) {
      return std::unexpected(PhotometryError::kInvalidInput);
    }
    if (numerator == 0.0) {
      return 0.0;
    }
    // Double input exponents span fewer than 2200 stops. Larger compensation
    // cannot leave a nonzero finite quotient; guard the subsequent int cast.
    constexpr double kMaximumCompensationMagnitude = 4096.0;
    if (std::abs(compensation.get()) > kMaximumCompensationMagnitude) {
      return std::unexpected(PhotometryError::kUnrepresentable);
    }
    int numerator_exponent = 0;
    int denominator_exponent = 0;
    const auto numerator_fraction = std::frexp(numerator, &numerator_exponent);
    const auto denominator_fraction
      = std::frexp(denominator, &denominator_exponent);
    const auto whole_stops = std::floor(compensation.get());
    const auto fraction = numerator_fraction / denominator_fraction
      * std::exp2(compensation.get() - whole_stops);
    const auto value = std::scalbn(fraction,
      numerator_exponent - denominator_exponent
        + static_cast<int>(whole_stops));
    if (!std::isfinite(value) || value == 0.0) {
      return std::unexpected(PhotometryError::kUnrepresentable);
    }
    return value;
  }
} // namespace

auto SpotEffectiveSolidAngle(const SpotCone& cone)
  -> std::expected<double, PhotometryError>
{
  if (!ValidCone(cone)) {
    return std::unexpected(PhotometryError::kInvalidInput);
  }
  const auto inner = cone.inner.get();
  const auto outer = cone.outer.get();
  const auto inner_sine = std::sin(inner / 2.0);
  const auto full_intensity_area = 2.0 * inner_sine * inner_sine;
  const auto transition_area = CosineDifference(inner, outer) / 3.0;
  const auto omega = 2.0 * kPi * (full_intensity_area + transition_area);
  if (omega == 0.0 || !std::isfinite(omega)) {
    return std::unexpected(PhotometryError::kUnrepresentable);
  }
  return omega;
}

auto SpotAngularWeight(const SpotCone& cone, const OffAxisAngleRadians angle)
  -> std::expected<double, PhotometryError>
{
  const auto omega = SpotEffectiveSolidAngle(cone);
  if (!omega) {
    return std::unexpected(omega.error());
  }
  if (!NonnegativeFinite(angle.get()) || angle.get() > kPi) {
    return std::unexpected(PhotometryError::kInvalidInput);
  }
  if (angle.get() <= cone.inner.get()) {
    return 1.0;
  }
  if (angle.get() >= cone.outer.get()) {
    return 0.0;
  }
  const auto width = CosineDifference(cone.inner.get(), cone.outer.get());
  if (width == 0.0) {
    return std::unexpected(PhotometryError::kUnrepresentable);
  }
  const auto fraction = std::clamp(
    CosineDifference(angle.get(), cone.outer.get()) / width, 0.0, 1.0);
  return fraction * fraction;
}

auto ResolvePointIntensity(
  const LuminousFluxLumens flux, const SourceExposureEv compensation)
  -> std::expected<LuminousIntensityCandela, PhotometryError>
{
  return ScaleQuotient(flux.get(), 4.0 * kPi, compensation)
    .transform([](const double value) -> LuminousIntensityCandela {
      return LuminousIntensityCandela { value };
    });
}

auto ResolveSpotPeakIntensity(const LuminousFluxLumens flux,
  const SpotCone& cone, const SourceExposureEv compensation)
  -> std::expected<LuminousIntensityCandela, PhotometryError>
{
  const auto omega = SpotEffectiveSolidAngle(cone);
  if (!omega) {
    return std::unexpected(omega.error());
  }
  return ScaleQuotient(flux.get(), *omega, compensation)
    .transform([](const double value) -> LuminousIntensityCandela {
      return LuminousIntensityCandela { value };
    });
}

auto ResolveDirectionalIlluminance(
  const IlluminanceLux illuminance, const SourceExposureEv compensation)
  -> std::expected<IlluminanceLux, PhotometryError>
{
  return ScaleQuotient(illuminance.get(), 1.0, compensation)
    .transform([](const double value) -> IlluminanceLux {
      return IlluminanceLux { value };
    });
}

auto SpotFluxFromPeakIntensity(const LuminousIntensityCandela intensity,
  const SpotCone& cone) -> std::expected<LuminousFluxLumens, PhotometryError>
{
  const auto omega = SpotEffectiveSolidAngle(cone);
  if (!omega) {
    return std::unexpected(omega.error());
  }
  if (!NonnegativeFinite(intensity.get())) {
    return std::unexpected(PhotometryError::kInvalidInput);
  }
  const auto flux = intensity.get() * *omega;
  if (!std::isfinite(flux) || (intensity.get() > 0.0 && flux == 0.0)) {
    return std::unexpected(PhotometryError::kUnrepresentable);
  }
  return LuminousFluxLumens { flux };
}

auto PunctualDistanceFactor(const DistanceMetres distance,
  const InfluenceRangeMetres range) -> std::expected<double, PhotometryError>
{
  if (!NonnegativeFinite(distance.get()) || !NonnegativeFinite(range.get())) {
    return std::unexpected(PhotometryError::kInvalidInput);
  }
  if (distance.get() == 0.0 || distance.get() >= range.get()) {
    return 0.0;
  }
  const auto ratio = distance.get() / range.get();
  // Factoring 1-ratio^4 retains the small range-boundary difference.
  const auto window = (1.0 - ratio) * (1.0 + ratio) * (1.0 + (ratio * ratio));
  constexpr double kMinimumDistance = 0.001;
  const auto scaled = window / std::max(distance.get(), kMinimumDistance);
  const auto factor = scaled * scaled;
  if (factor == 0.0) {
    return std::unexpected(PhotometryError::kUnrepresentable);
  }
  return factor;
}

} // namespace oxygen::vortex::testing::reference
