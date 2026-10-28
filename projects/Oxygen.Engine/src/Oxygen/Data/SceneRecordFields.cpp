//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <iterator>
#include <optional>
#include <utility>
#include <variant>

#include <glm/gtc/quaternion.hpp>
#include <glm/mat3x3.hpp>
#include <glm/vec3.hpp>

#include <Oxygen/Core/Types/CameraAspectMode.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneRecordFields.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>

namespace oxygen::data::pak::world {

namespace {

  auto SetFloat(float& target, const FieldValue& value) -> bool
  {
    const auto* number = std::get_if<float>(&value);
    if (number == nullptr) {
      return false;
    }
    target = *number;
    return true;
  }

  //! A bool stored as 0 or 1.
  template <std::unsigned_integral T>
  auto SetFlag(T& target, const FieldValue& value) -> bool
  {
    const auto* flag = std::get_if<bool>(&value);
    if (flag == nullptr) {
      return false;
    }
    target = *flag ? T { 1 } : T { 0 };
    return true;
  }

  //! An int32, or an enum member's wire value, stored in a field of type `T`.
  template <std::integral T>
  auto SetInteger(T& target, const FieldValue& value) -> bool
  {
    const auto* number = std::get_if<std::int32_t>(&value);
    if (number == nullptr || !std::in_range<T>(*number)) {
      return false;
    }
    target = static_cast<T>(*number);
    return true;
  }

  //! Sets the bit `mask` of `flags` from a bool.
  auto SetFlagBit(uint32_t& flags, const uint32_t mask, const FieldValue& value)
    -> bool
  {
    const auto* flag = std::get_if<bool>(&value);
    if (flag == nullptr) {
      return false;
    }
    flags = *flag ? (flags | mask) : (flags & ~mask);
    return true;
  }

  //! Sets an inheritable flag from its source mode: Inherit, a local on
  //! (Shown, On) or a local off (Hidden, Off).
  auto SetFlagSource(
    NodeRecord& record, const uint32_t mask, const FieldValue& value) -> bool
  {
    using schema::ShadowSource;
    using schema::VisibilitySource;
    // Node flag sources share one encoding.
    static_assert(std::to_underlying(VisibilitySource::kInherit)
      == std::to_underlying(ShadowSource::kInherit));
    static_assert(std::to_underlying(VisibilitySource::kShown)
      == std::to_underlying(ShadowSource::kOn));
    static_assert(std::to_underlying(VisibilitySource::kHidden)
      == std::to_underlying(ShadowSource::kOff));
    const auto* mode = std::get_if<std::int32_t>(&value);
    if (mode == nullptr
      || !schema::IsMember(schema::kVisibilitySourceMembers, *mode)) {
      return false;
    }
    record.node_flags &= ~mask;
    record.inherited_flags &= ~mask;
    if (std::cmp_equal(*mode, std::to_underlying(VisibilitySource::kInherit))) {
      record.inherited_flags |= mask;
    } else if (std::cmp_equal(
                 *mode, std::to_underlying(VisibilitySource::kShown))) {
      record.node_flags |= mask;
    }
    return true;
  }

  //! Writes a field every light kind shares; `Field` is a light field set,
  //! whose shared fields have the same names and ids.
  template <typename Field>
  auto WriteLightCommon(LightCommonRecord& common, const Field field,
    const FieldValue& value) -> bool
  {
    if (field == Field::kColorR) {
      return SetFloat(common.color_rgb[0], value);
    }
    if (field == Field::kColorG) {
      return SetFloat(common.color_rgb[1], value);
    }
    if (field == Field::kColorB) {
      return SetFloat(common.color_rgb[2], value);
    }
    if (field == Field::kAffectsWorld) {
      return SetFlag(common.affects_world, value);
    }
    if (field == Field::kCastsShadows) {
      return SetFlag(common.casts_shadows, value);
    }
    if (field == Field::kShadowBias) {
      return SetFloat(common.shadow.bias, value);
    }
    if (field == Field::kShadowNormalBias) {
      return SetFloat(common.shadow.normal_bias, value);
    }
    if (field == Field::kContactShadows) {
      return SetFlag(common.shadow.contact_shadows, value);
    }
    if (field == Field::kShadowResolutionHint) {
      return SetInteger(common.shadow.resolution_hint, value);
    }
    if (field == Field::kExposureCompensation) {
      return SetFloat(common.exposure_compensation_ev, value);
    }
    return false;
  }

  //! A bool stored as 0 or 1.
  template <std::unsigned_integral T> auto Flag(const T stored) -> FieldValue
  {
    return stored != T { 0 };
  }

  //! An int32, or an enum member's wire value, stored in a field of type `T`.
  template <std::integral T> auto Integer(const T stored) -> FieldValue
  {
    return static_cast<std::int32_t>(stored);
  }

  //! The source mode of an inheritable flag: Inherit, a local on (Shown, On)
  //! or a local off (Hidden, Off).
  auto FlagSource(const NodeRecord& record, const uint32_t mask) -> FieldValue
  {
    using schema::VisibilitySource;
    if ((record.inherited_flags & mask) != 0U) {
      return Integer(std::to_underlying(VisibilitySource::kInherit));
    }
    return Integer(std::to_underlying((record.node_flags & mask) != 0U
        ? VisibilitySource::kShown
        : VisibilitySource::kHidden));
  }

  //! Reads a field every light kind shares; `Field` is a light field set,
  //! whose shared fields have the same names and ids.
  template <typename Field>
  auto ReadLightCommon(const LightCommonRecord& common, const Field field)
    -> std::optional<FieldValue>
  {
    if (field == Field::kColorR) {
      return common.color_rgb[0];
    }
    if (field == Field::kColorG) {
      return common.color_rgb[1];
    }
    if (field == Field::kColorB) {
      return common.color_rgb[2];
    }
    if (field == Field::kAffectsWorld) {
      return Flag(common.affects_world);
    }
    if (field == Field::kCastsShadows) {
      return Flag(common.casts_shadows);
    }
    if (field == Field::kShadowBias) {
      return common.shadow.bias;
    }
    if (field == Field::kShadowNormalBias) {
      return common.shadow.normal_bias;
    }
    if (field == Field::kContactShadows) {
      return Flag(common.shadow.contact_shadows);
    }
    if (field == Field::kShadowResolutionHint) {
      return Integer(common.shadow.resolution_hint);
    }
    if (field == Field::kExposureCompensation) {
      return common.exposure_compensation_ev;
    }
    return std::nullopt;
  }

} // namespace

auto WriteField(NodeRecord& record, const schema::TransformField field,
  const FieldValue& value) -> bool
{
  using schema::TransformField;
  switch (field) {
  case TransformField::kPositionX:
    return SetFloat(record.translation[0], value);
  case TransformField::kPositionY:
    return SetFloat(record.translation[1], value);
  case TransformField::kPositionZ:
    return SetFloat(record.translation[2], value);
  case TransformField::kRotationXDegrees:
  case TransformField::kRotationYDegrees:
  case TransformField::kRotationZDegrees:
    // The record holds a quaternion: WriteRotation writes it whole.
    return false;
  case TransformField::kScaleX:
    return SetFloat(record.scale[0], value);
  case TransformField::kScaleY:
    return SetFloat(record.scale[1], value);
  case TransformField::kScaleZ:
    return SetFloat(record.scale[2], value);
  case TransformField::kCount:
    break;
  }
  return false;
}

auto WriteField(NodeRecord& record, const schema::NodeField field,
  const FieldValue& value) -> bool
{
  using schema::NodeField;
  switch (field) {
  case NodeField::kVisible:
    return SetFlagSource(record, kSceneNodeFlag_Visible, value);
  case NodeField::kCastsShadows:
    return SetFlagSource(record, kSceneNodeFlag_CastsShadows, value);
  case NodeField::kReceivesShadows:
    return SetFlagSource(record, kSceneNodeFlag_ReceivesShadows, value);
  case NodeField::kStatic:
    return SetFlagBit(record.node_flags, kSceneNodeFlag_Static, value);
  case NodeField::kRayCastingSelectable:
    return SetFlagBit(
      record.node_flags, kSceneNodeFlag_RayCastingSelectable, value);
  case NodeField::kIgnoreParentTransform:
    return SetFlagBit(
      record.node_flags, kSceneNodeFlag_IgnoreParentTransform, value);
  case NodeField::kCount:
    break;
  }
  return false;
}

auto WriteField(PerspectiveCameraRecord& record,
  const schema::PerspectiveCameraField field, const FieldValue& value) -> bool
{
  using schema::PerspectiveCameraField;
  switch (field) {
  case PerspectiveCameraField::kFieldOfViewYRadians:
    return SetFloat(record.fov_y, value);
  case PerspectiveCameraField::kAspectRatio:
    return SetFloat(record.aspect_ratio, value);
  case PerspectiveCameraField::kNearPlane:
    return SetFloat(record.near_plane, value);
  case PerspectiveCameraField::kFarPlane:
    return SetFloat(record.far_plane, value);
  case PerspectiveCameraField::kApertureF:
    return SetFloat(record.aperture_f, value);
  case PerspectiveCameraField::kShutterRate:
    return SetFloat(record.shutter_rate, value);
  case PerspectiveCameraField::kIso:
    return SetFloat(record.iso, value);
  case PerspectiveCameraField::kAspectMode: {
    auto mode = std::to_underlying(record.aspect_mode);
    if (const auto* member = std::get_if<std::int32_t>(&value);
      member == nullptr
      || !schema::IsMember(schema::kCameraAspectModeMembers, *member)
      || !SetInteger(mode, value)) {
      return false;
    }
    record.aspect_mode = static_cast<CameraAspectMode>(mode);
    return true;
  }
  case PerspectiveCameraField::kCount:
    break;
  }
  return false;
}

auto WriteField(OrthographicCameraRecord& record,
  const schema::OrthographicCameraField field, const FieldValue& value) -> bool
{
  using schema::OrthographicCameraField;
  // The volume is a half height and an aspect ratio around its centre.
  const auto centre_x = (0.5F * record.left) + (0.5F * record.right);
  const auto centre_y = (0.5F * record.bottom) + (0.5F * record.top);
  const auto half_height = (0.5F * record.top) - (0.5F * record.bottom);
  const auto half_width = (0.5F * record.right) - (0.5F * record.left);
  const auto set_volume = [&](const float height, const float width) -> void {
    record.left = centre_x - width;
    record.right = centre_x + width;
    record.bottom = centre_y - height;
    record.top = centre_y + height;
  };
  switch (field) {
  case OrthographicCameraField::kOrthographicSize: {
    auto size = 0.0F;
    if (!SetFloat(size, value)) {
      return false;
    }
    set_volume(size, size * (half_width / half_height));
    return true;
  }
  case OrthographicCameraField::kAspectRatio: {
    auto aspect = 0.0F;
    if (!SetFloat(aspect, value)) {
      return false;
    }
    set_volume(half_height, half_height * aspect);
    return true;
  }
  case OrthographicCameraField::kNearPlane:
    return SetFloat(record.near_plane, value);
  case OrthographicCameraField::kFarPlane:
    return SetFloat(record.far_plane, value);
  case OrthographicCameraField::kApertureF:
    return SetFloat(record.aperture_f, value);
  case OrthographicCameraField::kShutterRate:
    return SetFloat(record.shutter_rate, value);
  case OrthographicCameraField::kIso:
    return SetFloat(record.iso, value);
  case OrthographicCameraField::kAspectMode: {
    auto mode = std::to_underlying(record.aspect_mode);
    if (const auto* member = std::get_if<std::int32_t>(&value);
      member == nullptr
      || !schema::IsMember(schema::kCameraAspectModeMembers, *member)
      || !SetInteger(mode, value)) {
      return false;
    }
    record.aspect_mode = static_cast<CameraAspectMode>(mode);
    return true;
  }
  case OrthographicCameraField::kCount:
    break;
  }
  return false;
}

auto WriteField(DirectionalLightRecord& record,
  const schema::DirectionalLightField field, const FieldValue& value) -> bool
{
  using schema::DirectionalLightField;
  switch (field) {
  case DirectionalLightField::kColorR:
  case DirectionalLightField::kColorG:
  case DirectionalLightField::kColorB:
  case DirectionalLightField::kAffectsWorld:
  case DirectionalLightField::kCastsShadows:
  case DirectionalLightField::kShadowBias:
  case DirectionalLightField::kShadowNormalBias:
  case DirectionalLightField::kContactShadows:
  case DirectionalLightField::kShadowResolutionHint:
  case DirectionalLightField::kExposureCompensation:
    return WriteLightCommon(record.common, field, value);
  case DirectionalLightField::kIntensityLux:
    return SetFloat(record.intensity_lux, value);
  case DirectionalLightField::kAngularSizeRadians:
    return SetFloat(record.angular_size_radians, value);
  case DirectionalLightField::kCascadeCount:
    return SetInteger(record.cascade_count, value);
  case DirectionalLightField::kSplitMode:
    return SetInteger(record.split_mode, value);
  case DirectionalLightField::kMaxShadowDistance:
    return SetFloat(record.max_shadow_distance, value);
  case DirectionalLightField::kCascadeDistance0:
    return SetFloat(record.cascade_distances[0], value);
  case DirectionalLightField::kCascadeDistance1:
    return SetFloat(record.cascade_distances[1], value);
  case DirectionalLightField::kCascadeDistance2:
    return SetFloat(record.cascade_distances[2], value);
  case DirectionalLightField::kCascadeDistance3:
    return SetFloat(record.cascade_distances[3], value);
  case DirectionalLightField::kDistributionExponent:
    return SetFloat(record.distribution_exponent, value);
  case DirectionalLightField::kTransitionFraction:
    return SetFloat(record.transition_fraction, value);
  case DirectionalLightField::kDistanceFadeoutFraction:
    return SetFloat(record.distance_fadeout_fraction, value);
  case DirectionalLightField::kAtmosphereLightSlot:
    return SetInteger(record.atmosphere_light_slot, value);
  case DirectionalLightField::kUsePerPixelAtmosphereTransmittance:
    return SetFlag(record.use_per_pixel_atmosphere_transmittance, value);
  case DirectionalLightField::kDiskScaleR:
    return SetFloat(record.atmosphere_disk_luminance_scale_rgb[0], value);
  case DirectionalLightField::kDiskScaleG:
    return SetFloat(record.atmosphere_disk_luminance_scale_rgb[1], value);
  case DirectionalLightField::kDiskScaleB:
    return SetFloat(record.atmosphere_disk_luminance_scale_rgb[2], value);
  case DirectionalLightField::kCount:
    break;
  }
  return false;
}

auto WriteField(PointLightRecord& record, const schema::LocalLightField field,
  const FieldValue& value) -> bool
{
  using schema::LocalLightField;
  switch (field) {
  case LocalLightField::kColorR:
  case LocalLightField::kColorG:
  case LocalLightField::kColorB:
  case LocalLightField::kAffectsWorld:
  case LocalLightField::kCastsShadows:
  case LocalLightField::kShadowBias:
  case LocalLightField::kShadowNormalBias:
  case LocalLightField::kContactShadows:
  case LocalLightField::kShadowResolutionHint:
  case LocalLightField::kExposureCompensation:
    return WriteLightCommon(record.common, field, value);
  case LocalLightField::kLuminousFluxLm:
    return SetFloat(record.luminous_flux_lm, value);
  case LocalLightField::kRange:
    return SetFloat(record.range, value);
  case LocalLightField::kSourceRadius:
    return SetFloat(record.source_radius, value);
  case LocalLightField::kInnerConeAngleRadians:
  case LocalLightField::kOuterConeAngleRadians:
    // A point light has no cone.
  case LocalLightField::kCount:
    break;
  }
  return false;
}

auto WriteField(SpotLightRecord& record, const schema::LocalLightField field,
  const FieldValue& value) -> bool
{
  using schema::LocalLightField;
  switch (field) {
  case LocalLightField::kColorR:
  case LocalLightField::kColorG:
  case LocalLightField::kColorB:
  case LocalLightField::kAffectsWorld:
  case LocalLightField::kCastsShadows:
  case LocalLightField::kShadowBias:
  case LocalLightField::kShadowNormalBias:
  case LocalLightField::kContactShadows:
  case LocalLightField::kShadowResolutionHint:
  case LocalLightField::kExposureCompensation:
    return WriteLightCommon(record.common, field, value);
  case LocalLightField::kLuminousFluxLm:
    return SetFloat(record.luminous_flux_lm, value);
  case LocalLightField::kRange:
    return SetFloat(record.range, value);
  case LocalLightField::kSourceRadius:
    return SetFloat(record.source_radius, value);
  case LocalLightField::kInnerConeAngleRadians:
    return SetFloat(record.inner_cone_angle_radians, value);
  case LocalLightField::kOuterConeAngleRadians:
    return SetFloat(record.outer_cone_angle_radians, value);
  case LocalLightField::kCount:
    break;
  }
  return false;
}

auto WriteField(LocalFogVolumeRecord& record,
  const schema::LocalFogVolumeField field, const FieldValue& value) -> bool
{
  using schema::LocalFogVolumeField;
  switch (field) {
  case LocalFogVolumeField::kEnabled:
    return SetFlag(record.enabled, value);
  case LocalFogVolumeField::kRadialFogExtinction:
    return SetFloat(record.radial_fog_extinction, value);
  case LocalFogVolumeField::kHeightFogExtinction:
    return SetFloat(record.height_fog_extinction, value);
  case LocalFogVolumeField::kHeightFogFalloff:
    return SetFloat(record.height_fog_falloff, value);
  case LocalFogVolumeField::kHeightFogOffset:
    return SetFloat(record.height_fog_offset, value);
  case LocalFogVolumeField::kFogPhaseG:
    return SetFloat(record.fog_phase_g, value);
  case LocalFogVolumeField::kFogAlbedoR:
    return SetFloat(record.fog_albedo[0], value);
  case LocalFogVolumeField::kFogAlbedoG:
    return SetFloat(record.fog_albedo[1], value);
  case LocalFogVolumeField::kFogAlbedoB:
    return SetFloat(record.fog_albedo[2], value);
  case LocalFogVolumeField::kFogEmissiveR:
    return SetFloat(record.fog_emissive[0], value);
  case LocalFogVolumeField::kFogEmissiveG:
    return SetFloat(record.fog_emissive[1], value);
  case LocalFogVolumeField::kFogEmissiveB:
    return SetFloat(record.fog_emissive[2], value);
  case LocalFogVolumeField::kSortPriority:
    return SetInteger(record.sort_priority, value);
  case LocalFogVolumeField::kCount:
    break;
  }
  return false;
}

auto ReadField(const NodeRecord& record, const schema::TransformField field)
  -> std::optional<FieldValue>
{
  using schema::TransformField;
  switch (field) {
  case TransformField::kPositionX:
    return record.translation[0];
  case TransformField::kPositionY:
    return record.translation[1];
  case TransformField::kPositionZ:
    return record.translation[2];
  case TransformField::kRotationXDegrees:
  case TransformField::kRotationYDegrees:
  case TransformField::kRotationZDegrees:
    // The record holds a quaternion: QuaternionToEulerXyzDegrees reads it
    // whole.
    return std::nullopt;
  case TransformField::kScaleX:
    return record.scale[0];
  case TransformField::kScaleY:
    return record.scale[1];
  case TransformField::kScaleZ:
    return record.scale[2];
  case TransformField::kCount:
    break;
  }
  return std::nullopt;
}

auto ReadField(const NodeRecord& record, const schema::NodeField field)
  -> std::optional<FieldValue>
{
  using schema::NodeField;
  switch (field) {
  case NodeField::kVisible:
    return FlagSource(record, kSceneNodeFlag_Visible);
  case NodeField::kCastsShadows:
    return FlagSource(record, kSceneNodeFlag_CastsShadows);
  case NodeField::kReceivesShadows:
    return FlagSource(record, kSceneNodeFlag_ReceivesShadows);
  case NodeField::kStatic:
    return Flag(record.node_flags & kSceneNodeFlag_Static);
  case NodeField::kRayCastingSelectable:
    return Flag(record.node_flags & kSceneNodeFlag_RayCastingSelectable);
  case NodeField::kIgnoreParentTransform:
    return Flag(record.node_flags & kSceneNodeFlag_IgnoreParentTransform);
  case NodeField::kCount:
    break;
  }
  return std::nullopt;
}

auto ReadField(const PerspectiveCameraRecord& record,
  const schema::PerspectiveCameraField field) -> std::optional<FieldValue>
{
  using schema::PerspectiveCameraField;
  switch (field) {
  case PerspectiveCameraField::kFieldOfViewYRadians:
    return record.fov_y;
  case PerspectiveCameraField::kAspectRatio:
    return record.aspect_ratio;
  case PerspectiveCameraField::kNearPlane:
    return record.near_plane;
  case PerspectiveCameraField::kFarPlane:
    return record.far_plane;
  case PerspectiveCameraField::kApertureF:
    return record.aperture_f;
  case PerspectiveCameraField::kShutterRate:
    return record.shutter_rate;
  case PerspectiveCameraField::kIso:
    return record.iso;
  case PerspectiveCameraField::kAspectMode:
    return Integer(std::to_underlying(record.aspect_mode));
  case PerspectiveCameraField::kCount:
    break;
  }
  return std::nullopt;
}

auto ReadField(const OrthographicCameraRecord& record,
  const schema::OrthographicCameraField field) -> std::optional<FieldValue>
{
  using schema::OrthographicCameraField;
  // The volume is a half height and an aspect ratio around its centre.
  const auto half_height = (0.5F * record.top) - (0.5F * record.bottom);
  const auto half_width = (0.5F * record.right) - (0.5F * record.left);
  switch (field) {
  case OrthographicCameraField::kOrthographicSize:
    return half_height;
  case OrthographicCameraField::kAspectRatio:
    return half_width / half_height;
  case OrthographicCameraField::kNearPlane:
    return record.near_plane;
  case OrthographicCameraField::kFarPlane:
    return record.far_plane;
  case OrthographicCameraField::kApertureF:
    return record.aperture_f;
  case OrthographicCameraField::kShutterRate:
    return record.shutter_rate;
  case OrthographicCameraField::kIso:
    return record.iso;
  case OrthographicCameraField::kAspectMode:
    return Integer(std::to_underlying(record.aspect_mode));
  case OrthographicCameraField::kCount:
    break;
  }
  return std::nullopt;
}

auto ReadField(const DirectionalLightRecord& record,
  const schema::DirectionalLightField field) -> std::optional<FieldValue>
{
  using schema::DirectionalLightField;
  switch (field) {
  case DirectionalLightField::kColorR:
  case DirectionalLightField::kColorG:
  case DirectionalLightField::kColorB:
  case DirectionalLightField::kAffectsWorld:
  case DirectionalLightField::kCastsShadows:
  case DirectionalLightField::kShadowBias:
  case DirectionalLightField::kShadowNormalBias:
  case DirectionalLightField::kContactShadows:
  case DirectionalLightField::kShadowResolutionHint:
  case DirectionalLightField::kExposureCompensation:
    return ReadLightCommon(record.common, field);
  case DirectionalLightField::kIntensityLux:
    return record.intensity_lux;
  case DirectionalLightField::kAngularSizeRadians:
    return record.angular_size_radians;
  case DirectionalLightField::kCascadeCount:
    return Integer(record.cascade_count);
  case DirectionalLightField::kSplitMode:
    return Integer(record.split_mode);
  case DirectionalLightField::kMaxShadowDistance:
    return record.max_shadow_distance;
  case DirectionalLightField::kCascadeDistance0:
    return record.cascade_distances[0];
  case DirectionalLightField::kCascadeDistance1:
    return record.cascade_distances[1];
  case DirectionalLightField::kCascadeDistance2:
    return record.cascade_distances[2];
  case DirectionalLightField::kCascadeDistance3:
    return record.cascade_distances[3];
  case DirectionalLightField::kDistributionExponent:
    return record.distribution_exponent;
  case DirectionalLightField::kTransitionFraction:
    return record.transition_fraction;
  case DirectionalLightField::kDistanceFadeoutFraction:
    return record.distance_fadeout_fraction;
  case DirectionalLightField::kAtmosphereLightSlot:
    return Integer(record.atmosphere_light_slot);
  case DirectionalLightField::kUsePerPixelAtmosphereTransmittance:
    return Flag(record.use_per_pixel_atmosphere_transmittance);
  case DirectionalLightField::kDiskScaleR:
    return record.atmosphere_disk_luminance_scale_rgb[0];
  case DirectionalLightField::kDiskScaleG:
    return record.atmosphere_disk_luminance_scale_rgb[1];
  case DirectionalLightField::kDiskScaleB:
    return record.atmosphere_disk_luminance_scale_rgb[2];
  case DirectionalLightField::kCount:
    break;
  }
  return std::nullopt;
}

auto ReadField(const PointLightRecord& record,
  const schema::LocalLightField field) -> std::optional<FieldValue>
{
  using schema::LocalLightField;
  switch (field) {
  case LocalLightField::kColorR:
  case LocalLightField::kColorG:
  case LocalLightField::kColorB:
  case LocalLightField::kAffectsWorld:
  case LocalLightField::kCastsShadows:
  case LocalLightField::kShadowBias:
  case LocalLightField::kShadowNormalBias:
  case LocalLightField::kContactShadows:
  case LocalLightField::kShadowResolutionHint:
  case LocalLightField::kExposureCompensation:
    return ReadLightCommon(record.common, field);
  case LocalLightField::kLuminousFluxLm:
    return record.luminous_flux_lm;
  case LocalLightField::kRange:
    return record.range;
  case LocalLightField::kSourceRadius:
    return record.source_radius;
  case LocalLightField::kInnerConeAngleRadians:
  case LocalLightField::kOuterConeAngleRadians:
    // A point light has no cone.
  case LocalLightField::kCount:
    break;
  }
  return std::nullopt;
}

auto ReadField(const SpotLightRecord& record,
  const schema::LocalLightField field) -> std::optional<FieldValue>
{
  using schema::LocalLightField;
  switch (field) {
  case LocalLightField::kColorR:
  case LocalLightField::kColorG:
  case LocalLightField::kColorB:
  case LocalLightField::kAffectsWorld:
  case LocalLightField::kCastsShadows:
  case LocalLightField::kShadowBias:
  case LocalLightField::kShadowNormalBias:
  case LocalLightField::kContactShadows:
  case LocalLightField::kShadowResolutionHint:
  case LocalLightField::kExposureCompensation:
    return ReadLightCommon(record.common, field);
  case LocalLightField::kLuminousFluxLm:
    return record.luminous_flux_lm;
  case LocalLightField::kRange:
    return record.range;
  case LocalLightField::kSourceRadius:
    return record.source_radius;
  case LocalLightField::kInnerConeAngleRadians:
    return record.inner_cone_angle_radians;
  case LocalLightField::kOuterConeAngleRadians:
    return record.outer_cone_angle_radians;
  case LocalLightField::kCount:
    break;
  }
  return std::nullopt;
}

auto ReadField(const LocalFogVolumeRecord& record,
  const schema::LocalFogVolumeField field) -> std::optional<FieldValue>
{
  using schema::LocalFogVolumeField;
  switch (field) {
  case LocalFogVolumeField::kEnabled:
    return Flag(record.enabled);
  case LocalFogVolumeField::kRadialFogExtinction:
    return record.radial_fog_extinction;
  case LocalFogVolumeField::kHeightFogExtinction:
    return record.height_fog_extinction;
  case LocalFogVolumeField::kHeightFogFalloff:
    return record.height_fog_falloff;
  case LocalFogVolumeField::kHeightFogOffset:
    return record.height_fog_offset;
  case LocalFogVolumeField::kFogPhaseG:
    return record.fog_phase_g;
  case LocalFogVolumeField::kFogAlbedoR:
    return record.fog_albedo[0];
  case LocalFogVolumeField::kFogAlbedoG:
    return record.fog_albedo[1];
  case LocalFogVolumeField::kFogAlbedoB:
    return record.fog_albedo[2];
  case LocalFogVolumeField::kFogEmissiveR:
    return record.fog_emissive[0];
  case LocalFogVolumeField::kFogEmissiveG:
    return record.fog_emissive[1];
  case LocalFogVolumeField::kFogEmissiveB:
    return record.fog_emissive[2];
  case LocalFogVolumeField::kSortPriority:
    return Integer(record.sort_priority);
  case LocalFogVolumeField::kCount:
    break;
  }
  return std::nullopt;
}

auto WriteRotation(NodeRecord& record, const Quaternion& rotation) noexcept
  -> void
{
  std::ranges::copy(rotation, std::begin(record.rotation));
}

auto EulerXyzDegreesToQuaternion(const std::array<float, 3>& degrees) noexcept
  -> Quaternion
{
  const auto radians
    = glm::radians(glm::vec3 { degrees[0], degrees[1], degrees[2] });
  const auto rotation
    = glm::angleAxis(radians.z, glm::vec3 { 0.0F, 0.0F, 1.0F })
    * glm::angleAxis(radians.y, glm::vec3 { 0.0F, 1.0F, 0.0F })
    * glm::angleAxis(radians.x, glm::vec3 { 1.0F, 0.0F, 0.0F });
  return { rotation.x, rotation.y, rotation.z, rotation.w };
}

auto QuaternionToEulerXyzDegrees(const Quaternion& rotation) noexcept
  -> std::array<float, 3>
{
  // R = Rz(z) * Ry(y) * Rx(x); glm matrices index [column][row].
  const auto matrix = glm::mat3_cast(
    glm::quat { rotation[3], rotation[0], rotation[1], rotation[2] });
  // Y from its sine and cosine: asin loses precision near ±90°.
  const auto cos_y = std::hypot(matrix[0][0], matrix[0][1]);
  const auto y = std::atan2(-matrix[0][2], cos_y);
  auto x = 0.0F;
  auto z = 0.0F;
  // Within 0.1° of ±90° about Y, float noise outweighs the X and Z split.
  constexpr auto kGimbalLock = 0.0015F;
  if (cos_y > kGimbalLock) {
    x = std::atan2(matrix[1][2], matrix[2][2]);
    z = std::atan2(matrix[0][1], matrix[0][0]);
  } else {
    z = std::atan2(-matrix[1][0], matrix[1][1]);
  }
  const auto degrees = glm::degrees(glm::vec3 { x, y, z });
  return { degrees.x, degrees.y, degrees.z };
}

} // namespace oxygen::data::pak::world
