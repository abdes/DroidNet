//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, on)

namespace Oxygen::Interop::World {

//! Authored height fog and volumetric fog, in the editor scene model's units.
public value struct FogEnvironmentManaged {
  bool Enabled;
  bool HeightFogEnabled;
  float Density;
  float HeightFalloff;
  float HeightOffsetMeters;
  float MaxOpacity;
  System::Numerics::Vector3 InscatteringLuminanceRgb;
  System::Numerics::Vector3 SkyAmbientScaleRgb;
  float SecondDensity;
  float SecondHeightFalloff;
  float SecondHeightOffsetMeters;
  float StartDistanceMeters;
  float EndDistanceMeters;
  float CutoffDistanceMeters;
  System::Numerics::Vector3 DirectionalInscatteringLuminanceRgb;
  float DirectionalInscatteringExponent;
  float DirectionalInscatteringStartDistanceMeters;
  bool VolumetricFogEnabled;
  float VolumetricScatteringDistribution;
  System::Numerics::Vector3 VolumetricAlbedoRgb;
  System::Numerics::Vector3 VolumetricEmissiveRgb;
  float VolumetricExtinctionScale;
  float VolumetricDistanceMeters;
  float VolumetricStartDistanceMeters;
  float VolumetricNearFadeInDistanceMeters;
  float VolumetricStaticLightingScatteringIntensity;
  bool OverrideLightColorsWithFogInscattering;
  bool RenderInMainPass;
  bool Holdout;
  bool VisibleInReflectionCaptures;
  bool VisibleInRealTimeSkyCaptures;
};

} // namespace Oxygen::Interop::World

#pragma managed(pop)
