//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, on)

namespace Oxygen::Interop::World {

//! A cooked cube texture: a null descriptor path binds none. A project mount
//! resolves the cooked root through the mount's current generation.
public value struct CubemapReferenceManaged {
  System::String^ CookedRoot;
  System::String^ DescriptorRelativePath;
  System::String^ ProjectMount;
};

//! Authored Sky Sphere backdrop, in the editor scene model's units.
public value struct SkySphereEnvironmentManaged {
  bool Enabled;
  //! SkySphereSource: 0 cubemap, 1 solid color.
  int Source;
  CubemapReferenceManaged Cubemap;
  System::Numerics::Vector3 SolidColorRgb;
  float Intensity;
  float RotationRadians;
  System::Numerics::Vector3 TintRgb;
};

//! Authored Sky Light image-based lighting, in the editor scene model's units.
public value struct SkyLightEnvironmentManaged {
  bool Enabled;
  //! SkyLightSource: 0 captured scene, 1 specified cubemap.
  int Source;
  CubemapReferenceManaged Cubemap;
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
