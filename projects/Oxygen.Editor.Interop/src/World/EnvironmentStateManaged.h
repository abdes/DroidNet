//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, on)

namespace Oxygen::Interop::World {

//! One compensation key with EV100 input and EV-stop output.
public value struct ExposureCompensationKeyManaged {
  float MeteredEv;
  float CompensationEv;
};

//! Native environment presence and stored values at one mutation boundary.
public value struct EnvironmentStateManaged {
  bool Exists;
  bool AtmosphereExists;
  bool PostProcessExists;
  bool AtmosphereEnabled;
  bool SunDiskEnabled;
  float PlanetRadiusMeters;
  float AtmosphereHeightMeters;
  System::Numerics::Vector3 GroundAlbedoRgb;
  float RayleighScaleHeightMeters;
  float MieScaleHeightMeters;
  float MieAnisotropy;
  System::Numerics::Vector3 SkyLuminanceFactorRgb;
  float AerialPerspectiveDistanceScale;
  float AerialScatteringStrength;
  float AerialPerspectiveStartDepthMeters;
  float HeightFogContribution;
  int ExposureMode;
  bool ExposureEnabled;
  float ExposureKey;
  float ManualExposureEv;
  float ExposureCompensation;
  int ToneMapping;
  int AutoExposureMeteringMode;
  float AutoExposureMinEv;
  float AutoExposureMaxEv;
  float AutoExposureSpeedUp;
  float AutoExposureSpeedDown;
  float AutoExposureLowPercentile;
  float AutoExposureHighPercentile;
  float AutoExposureMinLogLuminance;
  float AutoExposureLogLuminanceRange;
  float AutoExposureTargetLuminance;
  float AutoExposureSpotMeterRadius;
  float AutoExposureBlackInfluence;
  System::UInt64 AutoExposureMeteringMask;
  bool ExposureMaskPending;
  System::String^ ExposureMaskError;
  float AutoExposureTransitionDistanceEv;
  cli::array<ExposureCompensationKeyManaged>^ AutoExposureCompensationCurve;
  float BloomIntensity;
  float BloomThreshold;
  float Saturation;
  float Contrast;
  float VignetteIntensity;
  float DisplayGamma;
};

} // namespace Oxygen::Interop::World

#pragma managed(pop)
