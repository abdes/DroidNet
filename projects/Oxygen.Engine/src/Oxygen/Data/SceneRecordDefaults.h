//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Core/Types/CameraAspectMode.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>

namespace oxygen::data::pak::world {

//! A scene component record holding the scene schema's defaults.
/*!
 The component records of `PakFormat_world.h` are value-initialized: they own
 no default. A writer that starts from the schema's defaults, such as the
 cooker for a property its source omits, takes them from here, where each field
 reads `Generated.SceneComponents.h`. `node_index` stays zero.

 Defined for `PerspectiveCameraRecord`, `OrthographicCameraRecord`,
 `DirectionalLightRecord`, `PointLightRecord`, `SpotLightRecord` and
 `LocalFogVolumeRecord`.
*/
template <typename Record>
[[nodiscard]] constexpr auto MakeDefaultRecord() -> Record = delete;

// Camera records hold Core's aspect mode, whose enumerators are the schema's
// members.
static_assert(nostd::to_underlying(CameraAspectMode::kAuto)
  == nostd::to_underlying(schema::CameraAspectMode::kAuto));
static_assert(nostd::to_underlying(CameraAspectMode::kFixed)
  == nostd::to_underlying(schema::CameraAspectMode::kFixed));
static_assert(schema::kCameraAspectModeMembers.size() == 2U);

//! The Core aspect mode of the schema member `mode`.
[[nodiscard]] constexpr auto ToCameraAspectMode(
  const schema::CameraAspectMode mode) noexcept -> CameraAspectMode
{
  return static_cast<CameraAspectMode>(nostd::to_underlying(mode));
}

namespace detail {
  [[nodiscard]] constexpr auto ToUint32(const bool value) noexcept -> uint32_t
  {
    return value ? 1U : 0U;
  }

  [[nodiscard]] constexpr auto ToUint8(const bool value) noexcept -> uint8_t
  {
    return value ? uint8_t { 1U } : uint8_t { 0U };
  }
} // namespace detail

template <>
[[nodiscard]] constexpr auto MakeDefaultRecord<PerspectiveCameraRecord>()
  -> PerspectiveCameraRecord
{
  namespace camera = schema::perspective_camera;
  auto record = PerspectiveCameraRecord {};
  record.fov_y = camera::kFieldOfViewYRadiansDefault;
  record.aspect_ratio = camera::kAspectRatioDefault;
  record.near_plane = camera::kNearPlaneDefault;
  record.far_plane = camera::kFarPlaneDefault;
  record.aperture_f = camera::kApertureFDefault;
  record.shutter_rate = camera::kShutterRateDefault;
  record.iso = camera::kIsoDefault;
  record.aspect_mode = ToCameraAspectMode(camera::kAspectModeDefault);
  return record;
}

//! The volume is the schema's half height and aspect ratio around the origin.
template <>
[[nodiscard]] constexpr auto MakeDefaultRecord<OrthographicCameraRecord>()
  -> OrthographicCameraRecord
{
  namespace camera = schema::orthographic_camera;
  auto record = OrthographicCameraRecord {};
  const auto half_height = camera::kOrthographicSizeDefault;
  const auto half_width = half_height * camera::kAspectRatioDefault;
  record.left = -half_width;
  record.right = half_width;
  record.bottom = -half_height;
  record.top = half_height;
  record.near_plane = camera::kNearPlaneDefault;
  record.far_plane = camera::kFarPlaneDefault;
  record.aperture_f = camera::kApertureFDefault;
  record.shutter_rate = camera::kShutterRateDefault;
  record.iso = camera::kIsoDefault;
  record.aspect_mode = ToCameraAspectMode(camera::kAspectModeDefault);
  return record;
}

template <>
[[nodiscard]] constexpr auto MakeDefaultRecord<DirectionalLightRecord>()
  -> DirectionalLightRecord
{
  namespace light = schema::directional_light;
  auto record = DirectionalLightRecord {};
  record.common.affects_world = detail::ToUint32(light::kAffectsWorldDefault);
  record.common.color_rgb[0] = light::kColorRDefault;
  record.common.color_rgb[1] = light::kColorGDefault;
  record.common.color_rgb[2] = light::kColorBDefault;
  record.common.casts_shadows = detail::ToUint8(light::kCastsShadowsDefault);
  record.common.shadow.bias = light::kShadowBiasDefault;
  record.common.shadow.normal_bias = light::kShadowNormalBiasDefault;
  record.common.shadow.contact_shadows
    = detail::ToUint32(light::kContactShadowsDefault);
  record.common.shadow.resolution_hint
    = nostd::to_underlying(light::kShadowResolutionHintDefault);
  record.common.exposure_compensation_ev = light::kExposureCompensationDefault;
  record.angular_size_radians = light::kAngularSizeRadiansDefault;
  record.atmosphere_light_slot
    = nostd::to_underlying(light::kAtmosphereLightSlotDefault);
  record.use_per_pixel_atmosphere_transmittance
    = detail::ToUint8(light::kUsePerPixelAtmosphereTransmittanceDefault);
  record.atmosphere_disk_luminance_scale_rgb[0] = light::kDiskScaleRDefault;
  record.atmosphere_disk_luminance_scale_rgb[1] = light::kDiskScaleGDefault;
  record.atmosphere_disk_luminance_scale_rgb[2] = light::kDiskScaleBDefault;
  record.cascade_count = static_cast<uint32_t>(light::kCascadeCountDefault);
  record.cascade_distances[0] = light::kCascadeDistance0Default;
  record.cascade_distances[1] = light::kCascadeDistance1Default;
  record.cascade_distances[2] = light::kCascadeDistance2Default;
  record.cascade_distances[3] = light::kCascadeDistance3Default;
  record.distribution_exponent = light::kDistributionExponentDefault;
  record.split_mode = nostd::to_underlying(light::kSplitModeDefault);
  record.max_shadow_distance = light::kMaxShadowDistanceDefault;
  record.transition_fraction = light::kTransitionFractionDefault;
  record.distance_fadeout_fraction = light::kDistanceFadeoutFractionDefault;
  record.intensity_lux = light::kIntensityLuxDefault;
  return record;
}

template <>
[[nodiscard]] constexpr auto MakeDefaultRecord<PointLightRecord>()
  -> PointLightRecord
{
  namespace light = schema::point_light;
  auto record = PointLightRecord {};
  record.common.affects_world = detail::ToUint32(light::kAffectsWorldDefault);
  record.common.color_rgb[0] = light::kColorRDefault;
  record.common.color_rgb[1] = light::kColorGDefault;
  record.common.color_rgb[2] = light::kColorBDefault;
  record.common.casts_shadows = detail::ToUint8(light::kCastsShadowsDefault);
  record.common.shadow.bias = light::kShadowBiasDefault;
  record.common.shadow.normal_bias = light::kShadowNormalBiasDefault;
  record.common.shadow.contact_shadows
    = detail::ToUint32(light::kContactShadowsDefault);
  record.common.shadow.resolution_hint
    = nostd::to_underlying(light::kShadowResolutionHintDefault);
  record.common.exposure_compensation_ev = light::kExposureCompensationDefault;
  record.range = light::kRangeDefault;
  record.source_radius = light::kSourceRadiusDefault;
  record.luminous_flux_lm = light::kLuminousFluxLmDefault;
  return record;
}

template <>
[[nodiscard]] constexpr auto MakeDefaultRecord<SpotLightRecord>()
  -> SpotLightRecord
{
  namespace light = schema::spot_light;
  auto record = SpotLightRecord {};
  record.common.affects_world = detail::ToUint32(light::kAffectsWorldDefault);
  record.common.color_rgb[0] = light::kColorRDefault;
  record.common.color_rgb[1] = light::kColorGDefault;
  record.common.color_rgb[2] = light::kColorBDefault;
  record.common.casts_shadows = detail::ToUint8(light::kCastsShadowsDefault);
  record.common.shadow.bias = light::kShadowBiasDefault;
  record.common.shadow.normal_bias = light::kShadowNormalBiasDefault;
  record.common.shadow.contact_shadows
    = detail::ToUint32(light::kContactShadowsDefault);
  record.common.shadow.resolution_hint
    = nostd::to_underlying(light::kShadowResolutionHintDefault);
  record.common.exposure_compensation_ev = light::kExposureCompensationDefault;
  record.range = light::kRangeDefault;
  record.inner_cone_angle_radians = light::kInnerConeAngleRadiansDefault;
  record.outer_cone_angle_radians = light::kOuterConeAngleRadiansDefault;
  record.source_radius = light::kSourceRadiusDefault;
  record.luminous_flux_lm = light::kLuminousFluxLmDefault;
  return record;
}

template <>
[[nodiscard]] constexpr auto MakeDefaultRecord<LocalFogVolumeRecord>()
  -> LocalFogVolumeRecord
{
  namespace fog = schema::local_fog_volume;
  auto record = LocalFogVolumeRecord {};
  record.enabled = detail::ToUint32(fog::kEnabledDefault);
  record.radial_fog_extinction = fog::kRadialFogExtinctionDefault;
  record.height_fog_extinction = fog::kHeightFogExtinctionDefault;
  record.height_fog_falloff = fog::kHeightFogFalloffDefault;
  record.height_fog_offset = fog::kHeightFogOffsetDefault;
  record.fog_phase_g = fog::kFogPhaseGDefault;
  record.fog_albedo[0] = fog::kFogAlbedoRDefault;
  record.fog_albedo[1] = fog::kFogAlbedoGDefault;
  record.fog_albedo[2] = fog::kFogAlbedoBDefault;
  record.fog_emissive[0] = fog::kFogEmissiveRDefault;
  record.fog_emissive[1] = fog::kFogEmissiveGDefault;
  record.fog_emissive[2] = fog::kFogEmissiveBDefault;
  record.sort_priority = fog::kSortPriorityDefault;
  return record;
}

} // namespace oxygen::data::pak::world
