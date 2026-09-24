//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cmath>
#include <limits>
#include <numbers>

#include <Oxygen/Core/Lighting/LightPhotometry.h>
#include <Oxygen/Data/PakFormat_world.h>

namespace oxygen::data {
namespace detail {
  inline auto NonnegativeLightValue(float value) -> bool
  {
    return std::isfinite(value) && value >= 0.0F;
  }
  inline auto LightModifiers(const pak::world::LightCommonRecord& common)
    -> lighting::LightPhotometryModifiers
  {
    return { .color_rgb = { common.color_rgb[0], common.color_rgb[1], common.color_rgb[2] },
      .exposure_compensation_ev = common.exposure_compensation_ev };
  }
  inline auto ValidLocalLightRange(float range, float radius) -> bool
  {
    const auto inverse = range == 0.0F ? 0.0 : 1.0 / double(range);
    return NonnegativeLightValue(range) && NonnegativeLightValue(radius)
      && (inverse == 0.0 || (inverse >= std::numeric_limits<float>::min()
        && inverse <= std::numeric_limits<float>::max()))
      && double(range) + radius <= std::numeric_limits<float>::max();
  }
} // namespace detail

inline auto IsValidLightRecord(const pak::world::LightShadowSettingsRecord& record) -> bool
{
  return detail::NonnegativeLightValue(record.bias)
    && detail::NonnegativeLightValue(record.normal_bias)
    && record.contact_shadows <= 1U && record.resolution_hint <= 3U;
}

inline auto IsValidLightRecord(const pak::world::LightCommonRecord& record) -> bool
{
  return record.affects_world <= 1U && record.casts_shadows <= 1U
    && detail::NonnegativeLightValue(record.color_rgb[0])
    && detail::NonnegativeLightValue(record.color_rgb[1])
    && detail::NonnegativeLightValue(record.color_rgb[2])
    && std::isfinite(record.exposure_compensation_ev)
    && IsValidLightRecord(record.shadow);
}

inline auto IsValidLightRecord(const pak::world::PointLightRecord& record) -> bool
{
  return IsValidLightRecord(record.common)
    && detail::ValidLocalLightRange(record.range, record.source_radius)
    && lighting::ResolvePointIntensityRgb(record.luminous_flux_lm,
      detail::LightModifiers(record.common)).has_value();
}

inline auto IsValidLightRecord(const pak::world::SpotLightRecord& record) -> bool
{
  if (!IsValidLightRecord(record.common)
    || !detail::ValidLocalLightRange(record.range, record.source_radius)) return false;
  const auto cone = lighting::ResolveSpotConeProfile(
    record.inner_cone_angle_radians, record.outer_cone_angle_radians);
  return cone && lighting::ResolveSpotIntensityRgb(record.luminous_flux_lm,
    *cone, detail::LightModifiers(record.common)).has_value();
}

inline auto IsValidLightRecord(const pak::world::DirectionalLightRecord& record) -> bool
{
  if (!IsValidLightRecord(record.common) || record.atmosphere_light_slot > 2U
    || record.use_per_pixel_atmosphere_transmittance > 1U
    || !detail::NonnegativeLightValue(record.angular_size_radians)
    || record.angular_size_radians > std::numbers::pi_v<float>
    || record.cascade_count == 0U || record.cascade_count > 4U
    || record.split_mode > 1U || !std::isfinite(record.max_shadow_distance)
    || record.max_shadow_distance <= 0.0F
    || !std::isfinite(record.distribution_exponent) || record.distribution_exponent < 1.0F
    || !detail::NonnegativeLightValue(record.transition_fraction) || record.transition_fraction > 1.0F
    || !detail::NonnegativeLightValue(record.distance_fadeout_fraction) || record.distance_fadeout_fraction > 1.0F) return false;
  const auto rgb = lighting::ResolveDirectionalIlluminanceRgb(record.intensity_lux,
    detail::LightModifiers(record.common));
  if (!rgb) return false;
  const auto sine = std::sin(double(record.angular_size_radians) * 0.5);
  const auto projected_solid_angle = std::numbers::pi * sine * sine;
  for (int channel = 0; channel < 3; ++channel) {
    const auto scale = record.atmosphere_disk_luminance_scale_rgb[channel];
    if (!detail::NonnegativeLightValue(scale)) return false;
    if (record.angular_size_radians == 0.0F) continue;
    const auto radiance = double((*rgb)[channel]) * scale / projected_solid_angle;
    if (!std::isfinite(radiance) || radiance > std::numeric_limits<float>::max()
      || (radiance > 0.0 && radiance < std::numeric_limits<float>::min())) return false;
  }
  float previous = 0.0F;
  for (unsigned i = 0U; i < 4U; ++i) {
    const auto distance = record.cascade_distances[i];
    if (!std::isfinite(distance) || (i < record.cascade_count && distance <= previous)) return false;
    previous = distance;
  }
  return true;
}

} // namespace oxygen::data
