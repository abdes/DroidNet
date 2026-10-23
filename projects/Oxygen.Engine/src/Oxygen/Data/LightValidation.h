//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <span>
#include <type_traits>
#include <utility>

#include <Oxygen/Core/Lighting/LightPhotometry.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Data/Schema/PropertyRange.h>

namespace oxygen::data {

//! The scene schema's ranges of the properties every light kind shares.
/*!
 Each light kind declares its own; the `k<Kind>LightCommonRanges` constants
 gather them for the validators below and for the cooker.
*/
struct LightCommonRanges {
  std::array<schema::PropertyRange<float>, 3> color;
  schema::PropertyRange<float> exposure_compensation;
  schema::PropertyRange<float> shadow_bias;
  schema::PropertyRange<float> shadow_normal_bias;
  std::span<const std::uint8_t> shadow_resolution_hints;
};

inline constexpr auto kDirectionalLightCommonRanges = LightCommonRanges {
  .color = {
    schema::directional_light::kColorRRange,
    schema::directional_light::kColorGRange,
    schema::directional_light::kColorBRange,
  },
  .exposure_compensation
  = schema::directional_light::kExposureCompensationRange,
  .shadow_bias = schema::directional_light::kShadowBiasRange,
  .shadow_normal_bias = schema::directional_light::kShadowNormalBiasRange,
  .shadow_resolution_hints
  = schema::directional_light::kShadowResolutionHintValues,
};

inline constexpr auto kPointLightCommonRanges = LightCommonRanges {
  .color = {
    schema::point_light::kColorRRange,
    schema::point_light::kColorGRange,
    schema::point_light::kColorBRange,
  },
  .exposure_compensation = schema::point_light::kExposureCompensationRange,
  .shadow_bias = schema::point_light::kShadowBiasRange,
  .shadow_normal_bias = schema::point_light::kShadowNormalBiasRange,
  .shadow_resolution_hints = schema::point_light::kShadowResolutionHintValues,
};

inline constexpr auto kSpotLightCommonRanges = LightCommonRanges {
  .color = {
    schema::spot_light::kColorRRange,
    schema::spot_light::kColorGRange,
    schema::spot_light::kColorBRange,
  },
  .exposure_compensation = schema::spot_light::kExposureCompensationRange,
  .shadow_bias = schema::spot_light::kShadowBiasRange,
  .shadow_normal_bias = schema::spot_light::kShadowNormalBiasRange,
  .shadow_resolution_hints = schema::spot_light::kShadowResolutionHintValues,
};

namespace detail {
  [[nodiscard]] inline auto LightModifiers(
    const pak::world::LightCommonRecord& common)
    -> lighting::LightPhotometryModifiers
  {
    return {
      .color_rgb
      = { common.color_rgb[0], common.color_rgb[1], common.color_rgb[2] },
      .exposure_compensation_ev = common.exposure_compensation_ev,
    };
  }

  //! Whether a local light's positive range has a representable inverse and,
  //! with its source radius, a representable extent.
  [[nodiscard]] inline auto HasRepresentableLocalLightExtent(
    const float range, const float radius) -> bool
  {
    const auto inverse = 1.0 / static_cast<double>(range);
    return inverse >= std::numeric_limits<float>::min()
      && inverse <= std::numeric_limits<float>::max()
      && static_cast<double>(range) + static_cast<double>(radius)
      <= std::numeric_limits<float>::max();
  }
} // namespace detail

//! Whether `record` holds valid values of a light kind's shadow properties.
[[nodiscard]] inline auto IsValidLightRecord(
  const pak::world::LightShadowSettingsRecord& record,
  const LightCommonRanges& ranges) -> bool
{
  return ranges.shadow_bias.Contains(record.bias)
    && ranges.shadow_normal_bias.Contains(record.normal_bias)
    && record.contact_shadows <= 1U
    && schema::IsMember(ranges.shadow_resolution_hints, record.resolution_hint);
}

//! Whether `record` holds valid values of the properties a light kind shares.
[[nodiscard]] inline auto IsValidLightRecord(
  const pak::world::LightCommonRecord& record, const LightCommonRanges& ranges)
  -> bool
{
  return record.affects_world <= 1U && record.casts_shadows <= 1U
    && ranges.color[0].Contains(record.color_rgb[0])
    && ranges.color[1].Contains(record.color_rgb[1])
    && ranges.color[2].Contains(record.color_rgb[2])
    && ranges.exposure_compensation.Contains(record.exposure_compensation_ev)
    && IsValidLightRecord(record.shadow, ranges);
}

//! Whether `record` holds valid values of the point light's schema properties
//! and resolves to a representable light.
[[nodiscard]] inline auto IsValidLightRecord(
  const pak::world::PointLightRecord& record) -> bool
{
  namespace light = schema::point_light;
  return IsValidLightRecord(record.common, kPointLightCommonRanges)
    && light::kRangeRange.Contains(record.range)
    && light::kSourceRadiusRange.Contains(record.source_radius)
    && light::kLuminousFluxLmRange.Contains(record.luminous_flux_lm)
    && detail::HasRepresentableLocalLightExtent(
      record.range, record.source_radius)
    && lighting::ResolvePointIntensityRgb(
      record.luminous_flux_lm, detail::LightModifiers(record.common))
         .has_value();
}

//! Whether `record` holds valid values of the spot light's schema properties,
//! a valid cone pair, and resolves to a representable light.
[[nodiscard]] inline auto IsValidLightRecord(
  const pak::world::SpotLightRecord& record) -> bool
{
  namespace light = schema::spot_light;
  if (!IsValidLightRecord(record.common, kSpotLightCommonRanges)
    || !light::kRangeRange.Contains(record.range)
    || !light::kSourceRadiusRange.Contains(record.source_radius)
    || !light::kLuminousFluxLmRange.Contains(record.luminous_flux_lm)
    || !light::kInnerConeAngleRadiansRange.Contains(
      record.inner_cone_angle_radians)
    || !light::kOuterConeAngleRadiansRange.Contains(
      record.outer_cone_angle_radians)
    || !detail::HasRepresentableLocalLightExtent(
      record.range, record.source_radius)) {
    return false;
  }
  const auto cone = lighting::ResolveSpotConeProfile(
    record.inner_cone_angle_radians, record.outer_cone_angle_radians);
  return cone
    && lighting::ResolveSpotIntensityRgb(
      record.luminous_flux_lm, *cone, detail::LightModifiers(record.common))
         .has_value();
}

//! Whether `record` holds valid values of the directional light's schema
//! properties, increasing cascade distances, and a representable sun disk.
[[nodiscard]] inline auto IsValidLightRecord(
  const pak::world::DirectionalLightRecord& record) -> bool
{
  namespace light = schema::directional_light;
  // The record holds a distance for every cascade the schema allows.
  static_assert(light::kCascadeCountRange.max
    && std::cmp_less_equal(*light::kCascadeCountRange.max,
      std::extent_v<
        decltype(pak::world::DirectionalLightRecord::cascade_distances)>));
  if (!IsValidLightRecord(record.common, kDirectionalLightCommonRanges)
    || !light::kAngularSizeRadiansRange.Contains(record.angular_size_radians)
    || !schema::IsMember(
      light::kAtmosphereLightSlotValues, record.atmosphere_light_slot)
    || record.use_per_pixel_atmosphere_transmittance > 1U
    || !light::kDiskScaleRRange.Contains(
      record.atmosphere_disk_luminance_scale_rgb[0])
    || !light::kDiskScaleGRange.Contains(
      record.atmosphere_disk_luminance_scale_rgb[1])
    || !light::kDiskScaleBRange.Contains(
      record.atmosphere_disk_luminance_scale_rgb[2])
    || !std::in_range<std::int32_t>(record.cascade_count)
    || !light::kCascadeCountRange.Contains(
      static_cast<std::int32_t>(record.cascade_count))
    || !schema::IsMember(light::kSplitModeValues, record.split_mode)
    || !light::kMaxShadowDistanceRange.Contains(record.max_shadow_distance)
    || !light::kCascadeDistance0Range.Contains(record.cascade_distances[0])
    || !light::kCascadeDistance1Range.Contains(record.cascade_distances[1])
    || !light::kCascadeDistance2Range.Contains(record.cascade_distances[2])
    || !light::kCascadeDistance3Range.Contains(record.cascade_distances[3])
    || !light::kDistributionExponentRange.Contains(record.distribution_exponent)
    || !light::kTransitionFractionRange.Contains(record.transition_fraction)
    || !light::kDistanceFadeoutFractionRange.Contains(
      record.distance_fadeout_fraction)
    || !light::kIntensityLuxRange.Contains(record.intensity_lux)) {
    return false;
  }

  // The distances of the cascades in use increase.
  for (std::uint32_t i = 1U; i < record.cascade_count; ++i) {
    if (record.cascade_distances[i] <= record.cascade_distances[i - 1U]) {
      return false;
    }
  }

  const auto rgb = lighting::ResolveDirectionalIlluminanceRgb(
    record.intensity_lux, detail::LightModifiers(record.common));
  if (!rgb) {
    return false;
  }
  if (record.angular_size_radians == 0.0F) {
    return true;
  }
  // The sun disk radiance of every channel is representable.
  const auto sine = std::sin(double(record.angular_size_radians) * 0.5);
  const auto projected_solid_angle = std::numbers::pi * sine * sine;
  for (int channel = 0; channel < 3; ++channel) {
    const auto radiance = double((*rgb)[channel])
      * record.atmosphere_disk_luminance_scale_rgb[channel]
      / projected_solid_angle;
    if (!std::isfinite(radiance) || radiance > std::numeric_limits<float>::max()
      || (radiance > 0.0 && radiance < std::numeric_limits<float>::min())) {
      return false;
    }
  }
  return true;
}

} // namespace oxygen::data
