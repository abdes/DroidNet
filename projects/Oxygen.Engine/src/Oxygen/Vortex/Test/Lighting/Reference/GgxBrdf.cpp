//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <expected>
#include <numbers>

#include <Oxygen/Vortex/Test/Lighting/Reference/GgxBrdf.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/GgxMoments.h>

namespace oxygen::vortex::testing::reference {
namespace {
  constexpr auto kPi = std::numbers::pi_v<double>;
  constexpr double kMinimumRoughness = 0.045;

  auto UnitInterval(const double value) -> bool
  {
    return std::isfinite(value) && value >= 0.0 && value <= 1.0;
  }

  auto ValidMoment(const GgxMomentEstimate& moment) -> bool
  {
    return UnitInterval(moment.directional_albedo)
      && UnitInterval(moment.schlick_moment)
      && moment.schlick_moment <= moment.directional_albedo;
  }
} // namespace

auto EvaluateGgxBrdfChannel(const BrdfQuery& query,
  const BrdfReflectance& material, const BrdfMoments& moments)
  -> std::expected<BrdfLobes, BrdfReferenceError>
{
  const auto nl = query.light.get();
  const auto nv = query.view.get();
  if (!UnitInterval(query.roughness.get()) || !std::isfinite(nl)
    || !std::isfinite(nv) || std::abs(nl) > 1.0 || std::abs(nv) > 1.0
    || !std::isfinite(query.azimuth.get()) || !UnitInterval(material.f0)
    || !UnitInterval(material.diffuse)) {
    return std::unexpected(BrdfReferenceError::kInvalidInput);
  }
  if (nl <= 0.0 || nv <= 0.0) {
    return BrdfLobes {};
  }
  const auto mean_energy = moments.mean.hemispherical_albedo;
  const auto mean_bias = moments.mean.schlick_moment;
  if (!ValidMoment(moments.light) || !ValidMoment(moments.view)
    || !UnitInterval(mean_energy) || mean_energy == 0.0
    || !UnitInterval(mean_bias) || mean_bias > mean_energy) {
    return std::unexpected(BrdfReferenceError::kInvalidInput);
  }
  const auto light_loss = 1.0 - moments.light.directional_albedo;
  const auto view_loss = 1.0 - moments.view.directional_albedo;
  const auto mean_loss = 1.0 - mean_energy;
  if (mean_loss == 0.0 && (light_loss != 0.0 || view_loss != 0.0)) {
    return std::unexpected(BrdfReferenceError::kInvalidInput);
  }

  const auto view_sine = std::sqrt((1.0 - nv) * (1.0 + nv));
  const auto light_sine = std::sqrt((1.0 - nl) * (1.0 + nl));
  const auto half_x = view_sine + (light_sine * std::cos(query.azimuth.get()));
  const auto half_y = light_sine * std::sin(query.azimuth.get());
  const auto half_z = nl + nv;
  const auto half_length = std::hypot(half_x, half_y, half_z);
  const auto hx = half_x / half_length;
  const auto hy = half_y / half_length;
  const auto hz = half_z / half_length;
  const auto vh = std::clamp((view_sine * hx) + (nv * hz), 0.0, 1.0);
  const auto roughness = std::max(query.roughness.get(), kMinimumRoughness);
  const auto alpha = roughness * roughness;
  const auto a2 = alpha * alpha;
  const auto distribution_denominator = (hx * hx) + (hy * hy) + (a2 * hz * hz);
  const auto distribution
    = a2 / (kPi * distribution_denominator * distribution_denominator);
  const auto visibility = 0.5
    / ((nl * std::sqrt((nv * nv) + (a2 * (1.0 - (nv * nv)))))
      + (nv * std::sqrt((nl * nl) + (a2 * (1.0 - (nl * nl))))));
  const auto fresnel
    = material.f0 + ((1.0 - material.f0) * std::pow(1.0 - vh, 5));

  const auto average_fresnel = material.f0 + ((1.0 - material.f0) / 21.0);
  const auto compensation = average_fresnel * average_fresnel * mean_energy
    / ((1.0 - average_fresnel) + (average_fresnel * mean_energy));
  // Positive-term transmission avoids cancellation close to unit reflectance.
  const auto light_transmission = ((1.0 - compensation) * light_loss)
    + ((1.0 - material.f0)
      * (moments.light.directional_albedo - moments.light.schlick_moment));
  const auto view_transmission = ((1.0 - compensation) * view_loss)
    + ((1.0 - material.f0)
      * (moments.view.directional_albedo - moments.view.schlick_moment));
  const auto mean_transmission = ((1.0 - compensation) * mean_loss)
    + ((1.0 - material.f0) * (mean_energy - mean_bias));
  const auto diffuse_denominator
    = (1.0 - material.diffuse) + (material.diffuse * mean_transmission);
  const auto result = BrdfLobes {
    .single_scattering = distribution * visibility * fresnel,
    .multiple_scattering = mean_loss > 0.0
      ? compensation * light_loss * view_loss / (kPi * mean_loss)
      : 0.0,
    .diffuse = diffuse_denominator > 0.0 ? material.diffuse * light_transmission
        * view_transmission / (kPi * diffuse_denominator)
                                         : 0.0,
  };
  if (!std::isfinite(result.single_scattering)
    || !std::isfinite(result.multiple_scattering)
    || !std::isfinite(result.diffuse)) {
    return std::unexpected(BrdfReferenceError::kUnrepresentable);
  }
  return result;
}

} // namespace oxygen::vortex::testing::reference
