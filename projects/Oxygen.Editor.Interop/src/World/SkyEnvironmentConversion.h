//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, on)

#include <cstddef>
#include <filesystem>
#include <string>

#include <Commands/SetEnvironmentCommand.h>
#include <World/SkyEnvironmentManaged.h>

namespace Oxygen::Interop::World {

namespace detail {

  inline auto ToWide(System::String^ value) -> std::wstring
  {
    auto result = std::wstring(static_cast<std::size_t>(value->Length), L' ');
    for (int index = 0; index < value->Length; ++index) {
      result[static_cast<std::size_t>(index)] = value[index];
    }
    return result;
  }

  inline auto ToNativeVec3(System::Numerics::Vector3 value) -> oxygen::Vec3
  {
    return { value.X, value.Y, value.Z };
  }

  inline auto ToManagedVec3(const oxygen::Vec3& value)
    -> System::Numerics::Vector3
  {
    return System::Numerics::Vector3(value.x, value.y, value.z);
  }

  inline auto ToNativeCubemap(CubemapReferenceManaged value, const char* name)
    -> oxygen::interop::module::CubemapSource
  {
    auto result = oxygen::interop::module::CubemapSource {};
    if (value.DescriptorRelativePath == nullptr) {
      return result;
    }
    if (System::String::IsNullOrWhiteSpace(value.CookedRoot)
      || System::String::IsNullOrWhiteSpace(value.DescriptorRelativePath)) {
      throw gcnew System::ArgumentException(gcnew System::String(name)
        + " requires both a cooked source and a relative descriptor path.");
    }
    result.locator = oxygen::content::TextureResourceLocator {
      .cooked_root = std::filesystem::path(
        ToWide(value.CookedRoot)),
      .descriptor_relative_path = std::filesystem::path(
        ToWide(value.DescriptorRelativePath)),
    };
    if (value.ProjectMount != nullptr) {
      result.project_mount = ToWide(value.ProjectMount->ToUpperInvariant());
    }
    return result;
  }

} // namespace detail

//! Copies the authored sky into native command parameters without unit
//! changes.
inline auto ToNativeSky(SkySphereEnvironmentManaged sphere,
  SkyLightEnvironmentManaged light, BackgroundEnvironmentManaged background)
  -> oxygen::interop::module::SkyParams
{
  auto result = oxygen::interop::module::SkyParams {};
  auto& native_sphere = result.sky_sphere;
  native_sphere.enabled = sphere.Enabled;
  native_sphere.source = sphere.Source;
  native_sphere.cubemap
    = detail::ToNativeCubemap(sphere.Cubemap, "A sky sphere cubemap");
  native_sphere.solid_color_rgb = detail::ToNativeVec3(sphere.SolidColorRgb);
  native_sphere.illuminance_lux = sphere.IlluminanceLux;
  native_sphere.intensity = sphere.Intensity;
  native_sphere.rotation_radians = sphere.RotationRadians;
  native_sphere.tint_rgb = detail::ToNativeVec3(sphere.TintRgb);

  auto& native_light = result.sky_light;
  native_light.enabled = light.Enabled;
  native_light.source = light.Source;
  native_light.cubemap
    = detail::ToNativeCubemap(light.Cubemap, "A sky light cubemap");
  native_light.illuminance_lux = light.CubemapIlluminanceLux;
  native_light.intensity = light.Intensity;
  native_light.tint_rgb = detail::ToNativeVec3(light.TintRgb);
  native_light.diffuse_intensity = light.DiffuseIntensity;
  native_light.specular_intensity = light.SpecularIntensity;
  native_light.cubemap_angle_radians = light.CubemapAngleRadians;
  native_light.lower_hemisphere_color
    = detail::ToNativeVec3(light.LowerHemisphereColor);
  native_light.lower_hemisphere_is_solid_color
    = light.LowerHemisphereIsSolidColor;
  native_light.lower_hemisphere_blend_alpha = light.LowerHemisphereBlendAlpha;
  native_light.volumetric_scattering_intensity
    = light.VolumetricScatteringIntensity;
  native_light.affect_reflections = light.AffectReflections;

  result.background.enabled = background.Enabled;
  result.background.color_rgb = detail::ToNativeVec3(background.ColorRgb);
  return result;
}

//! Copies an observed native sky sphere into its managed transport value. The
//! bound cubemap is reported separately as a resource key.
inline auto ToManagedSkySphere(
  const oxygen::interop::module::SkySphereParams& value)
  -> SkySphereEnvironmentManaged
{
  SkySphereEnvironmentManaged result;
  result.Enabled = value.enabled;
  result.Source = value.source;
  result.SolidColorRgb = detail::ToManagedVec3(value.solid_color_rgb);
  result.IlluminanceLux = value.illuminance_lux;
  result.Intensity = value.intensity;
  result.RotationRadians = value.rotation_radians;
  result.TintRgb = detail::ToManagedVec3(value.tint_rgb);
  return result;
}

//! Copies an observed native sky light into its managed transport value. The
//! bound cubemap is reported separately as a resource key.
inline auto ToManagedSkyLight(
  const oxygen::interop::module::SkyLightParams& value)
  -> SkyLightEnvironmentManaged
{
  SkyLightEnvironmentManaged result;
  result.Enabled = value.enabled;
  result.Source = value.source;
  result.CubemapIlluminanceLux = value.illuminance_lux;
  result.Intensity = value.intensity;
  result.TintRgb = detail::ToManagedVec3(value.tint_rgb);
  result.DiffuseIntensity = value.diffuse_intensity;
  result.SpecularIntensity = value.specular_intensity;
  result.CubemapAngleRadians = value.cubemap_angle_radians;
  result.LowerHemisphereColor
    = detail::ToManagedVec3(value.lower_hemisphere_color);
  result.LowerHemisphereIsSolidColor = value.lower_hemisphere_is_solid_color;
  result.LowerHemisphereBlendAlpha = value.lower_hemisphere_blend_alpha;
  result.VolumetricScatteringIntensity = value.volumetric_scattering_intensity;
  result.AffectReflections = value.affect_reflections;
  return result;
}

//! Copies an observed native background into its managed transport value.
inline auto ToManagedBackground(
  const oxygen::interop::module::BackgroundParams& value)
  -> BackgroundEnvironmentManaged
{
  BackgroundEnvironmentManaged result;
  result.Enabled = value.enabled;
  result.ColorRgb = detail::ToManagedVec3(value.color_rgb);
  return result;
}

} // namespace Oxygen::Interop::World

#pragma managed(pop)
