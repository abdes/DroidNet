//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, on)

namespace Oxygen::Interop::World {

//! Authored Sky Sphere backdrop, in the editor scene model's units.
public value struct SkySphereEnvironmentManaged {
  bool Enabled;
  //! SkySphereSource: 0 cubemap, 1 solid color.
  int Source;
  //! Virtual path of the cooked cube texture descriptor; null binds none.
  System::String^ CubemapPath;
  System::Numerics::Vector3 SolidColorRgb;
  //! Calibrated upward illuminance in lux; zero keeps the imported radiance.
  float IlluminanceLux;
  float Intensity;
  float RotationRadians;
  System::Numerics::Vector3 TintRgb;
};

//! Authored Sky Light image-based lighting, in the editor scene model's units.
public value struct SkyLightEnvironmentManaged {
  bool Enabled;
  //! SkyLightSource: 0 captured scene, 1 specified cubemap.
  int Source;
  //! Virtual path of the cooked cube texture descriptor; null binds none.
  System::String^ CubemapPath;
  //! Calibrated upward illuminance of the cubemap in lux; zero keeps it raw.
  float CubemapIlluminanceLux;
  float Intensity;
  System::Numerics::Vector3 TintRgb;
  float DiffuseIntensity;
  float SpecularIntensity;
  float CubemapAngleRadians;
  System::Numerics::Vector3 LowerHemisphereColor;
  bool LowerHemisphereIsSolidColor;
  float LowerHemisphereBlendAlpha;
  float VolumetricScatteringIntensity;
  bool AffectReflections;
};

//! Authored display-only backdrop color, in linear SDR RGB.
public value struct BackgroundEnvironmentManaged {
  bool Enabled;
  System::Numerics::Vector3 ColorRgb;
};

} // namespace Oxygen::Interop::World

#pragma managed(pop)
