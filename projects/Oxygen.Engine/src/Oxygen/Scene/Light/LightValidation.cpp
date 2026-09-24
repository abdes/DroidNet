//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>
#include <limits>
#include <numbers>
#include <string_view>

#include <Oxygen/Core/Lighting/LightPhotometry.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Light/LightValidation.h>
#include <Oxygen/Scene/Light/PointLight.h>
#include <Oxygen/Scene/Light/SpotLight.h>

namespace oxygen::scene {
namespace {

  auto Reject(std::string_view field, std::string_view message)
    -> std::optional<LightValidationError>
  {
    return LightValidationError { .field = std::string(field),
      .message = std::string(message) };
  }

  auto Nonnegative(float value) -> bool
  {
    return std::isfinite(value) && value >= 0.0F;
  }

  auto ValidateCommon(const CommonLightProperties& common)
    -> std::optional<LightValidationError>
  {
    if (!Nonnegative(common.color_rgb.x) || !Nonnegative(common.color_rgb.y)
      || !Nonnegative(common.color_rgb.z)) {
      return Reject("color_rgb", "Color components must be finite and nonnegative");
    }
    if (!std::isfinite(common.exposure_compensation_ev)) {
      return Reject("exposure_compensation_ev", "Exposure compensation must be finite");
    }
    if (common.mobility != LightMobility::kRealtime) {
      return Reject("mobility", "Only realtime light authoring is supported");
    }
    if (!Nonnegative(common.shadow.bias)) {
      return Reject("shadow.bias", "Shadow bias must be finite and nonnegative");
    }
    if (!Nonnegative(common.shadow.normal_bias)) {
      return Reject("shadow.normal_bias", "Normal bias must be finite and nonnegative");
    }
    if (common.shadow.resolution_hint > ShadowResolutionHint::kUltra) {
      return Reject("shadow.resolution_hint", "Unknown shadow resolution hint");
    }
    return {};
  }

  auto Modifiers(const CommonLightProperties& common) -> lighting::LightPhotometryModifiers
  {
    return { .color_rgb = common.color_rgb,
      .exposure_compensation_ev = common.exposure_compensation_ev };
  }

  auto ValidateLocal(float range, float source_radius)
    -> std::optional<LightValidationError>
  {
    if (!Nonnegative(range)) {
      return Reject("range", "Range must be finite and nonnegative");
    }
    const auto inverse = range == 0.0F ? 0.0 : 1.0 / double(range);
    if (inverse > std::numeric_limits<float>::max()
      || (inverse > 0.0 && inverse < std::numeric_limits<float>::min())) {
      return Reject("range", "Inverse range is not representable by the renderer");
    }
    if (!Nonnegative(source_radius)
      || double(range) + source_radius > std::numeric_limits<float>::max()) {
      return Reject("source_radius", "Source radius must be finite, nonnegative and representable");
    }
    return {};
  }

  auto ValidateDirectional(const DirectionalLight& light)
    -> std::optional<LightValidationError>
  {
    if (const auto error = ValidateCommon(light.Common())) return error;
    const auto rgb = lighting::ResolveDirectionalIlluminanceRgb(
      light.GetIntensityLux(), Modifiers(light.Common()));
    if (!rgb) {
      return Reject("intensity_lux", "Resolved RGB illuminance must be finite, nonnegative and representable");
    }
    const auto diameter = light.GetAngularSizeRadians();
    if (!Nonnegative(diameter) || diameter > std::numbers::pi_v<float>) {
      return Reject("angular_size_radians", "Angular diameter must be in [0, pi]");
    }
    if (light.GetAtmosphereLightSlot() > AtmosphereLightSlot::kSecondary) {
      return Reject("atmosphere_light_slot", "Unknown atmosphere light slot");
    }
    const auto scale = light.GetAtmosphereDiskLuminanceScale();
    const auto sine = std::sin(double(diameter) * 0.5);
    const auto projected_solid_angle = std::numbers::pi * sine * sine;
    for (int channel = 0; channel < 3; ++channel) {
      if (!Nonnegative(scale[channel])) {
        return Reject("atmosphere_disk_luminance_scale_rgb", "Disk scale must be finite and nonnegative");
      }
      if (diameter == 0.0F) continue;
      const auto radiance = double((*rgb)[channel]) * scale[channel] / projected_solid_angle;
      if (!std::isfinite(radiance) || radiance > std::numeric_limits<float>::max()
        || (radiance > 0.0 && radiance < std::numeric_limits<float>::min())) {
        return Reject("atmosphere_disk_luminance_scale_rgb", "Resolved disk radiance is not representable");
      }
    }
    const auto& csm = light.CascadedShadows();
    if (csm.cascade_count == 0U || csm.cascade_count > kMaxShadowCascades) {
      return Reject("cascade_count", "Cascade count must be in [1, 4]");
    }
    if (!IsValidDirectionalCsmSplitMode(csm.split_mode)) {
      return Reject("split_mode", "Unknown cascade split mode");
    }
    if (!std::isfinite(csm.max_shadow_distance) || csm.max_shadow_distance <= 0.0F) {
      return Reject("max_shadow_distance", "Shadow distance must be finite and positive");
    }
    float previous = 0.0F;
    for (std::size_t i = 0U; i < csm.cascade_distances.size(); ++i) {
      const auto distance = csm.cascade_distances[i];
      if (!std::isfinite(distance) || (i < csm.cascade_count && distance <= previous)) {
        return Reject("cascade_distances", "Distances must be finite; active splits must be positive and increasing");
      }
      previous = distance;
    }
    if (!std::isfinite(csm.distribution_exponent) || csm.distribution_exponent < 1.0F) {
      return Reject("distribution_exponent", "Distribution exponent must be finite and at least one");
    }
    if (!Nonnegative(csm.transition_fraction) || csm.transition_fraction > 1.0F) {
      return Reject("transition_fraction", "Transition fraction must be in [0, 1]");
    }
    if (!Nonnegative(csm.distance_fadeout_fraction) || csm.distance_fadeout_fraction > 1.0F) {
      return Reject("distance_fadeout_fraction", "Fadeout fraction must be in [0, 1]");
    }
    return {};
  }
} // namespace

auto ValidateLight(const Component& light) -> std::optional<LightValidationError>
{
  if (light.GetTypeId() == DirectionalLight::ClassTypeId()) {
    return ValidateDirectional(static_cast<const DirectionalLight&>(light));
  }
  if (light.GetTypeId() == PointLight::ClassTypeId()) {
    const auto& point = static_cast<const PointLight&>(light);
    if (const auto error = ValidateCommon(point.Common())) return error;
    if (const auto error = ValidateLocal(point.GetRange(), point.GetSourceRadius())) return error;
    if (!lighting::ResolvePointIntensityRgb(point.GetLuminousFluxLm(), Modifiers(point.Common()))) {
      return Reject("luminous_flux_lm", "Resolved RGB candela must be finite, nonnegative and representable");
    }
    return {};
  }
  if (light.GetTypeId() == SpotLight::ClassTypeId()) {
    const auto& spot = static_cast<const SpotLight&>(light);
    if (const auto error = ValidateCommon(spot.Common())) return error;
    if (const auto error = ValidateLocal(spot.GetRange(), spot.GetSourceRadius())) return error;
    const auto cone = lighting::ResolveSpotConeProfile(
      spot.GetInnerConeAngleRadians(), spot.GetOuterConeAngleRadians());
    if (!cone) {
      return Reject("cone_angles", "Cone pair must be ordered, within a hemisphere and representable");
    }
    if (!lighting::ResolveSpotIntensityRgb(spot.GetLuminousFluxLm(), *cone, Modifiers(spot.Common()))) {
      return Reject("luminous_flux_lm", "Resolved RGB candela must be finite, nonnegative and representable");
    }
    return {};
  }
  return Reject("type", "Unsupported light component type");
}

} // namespace oxygen::scene
