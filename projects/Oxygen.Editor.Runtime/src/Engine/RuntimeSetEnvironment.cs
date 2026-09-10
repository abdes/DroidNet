// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Immutable runtime request to SetEnvironment.</summary>
/// <param name="AtmosphereEnabled">The AtmosphereEnabled command value.</param>
/// <param name="SunDiskEnabled">The SunDiskEnabled command value.</param>
/// <param name="PlanetRadiusMeters">The PlanetRadiusMeters command value.</param>
/// <param name="AtmosphereHeightMeters">The AtmosphereHeightMeters command value.</param>
/// <param name="GroundAlbedoRgb">The GroundAlbedoRgb command value.</param>
/// <param name="RayleighScaleHeightMeters">The RayleighScaleHeightMeters command value.</param>
/// <param name="MieScaleHeightMeters">The MieScaleHeightMeters command value.</param>
/// <param name="MieAnisotropy">The MieAnisotropy command value.</param>
/// <param name="SkyLuminanceFactorRgb">The SkyLuminanceFactorRgb command value.</param>
/// <param name="AerialPerspectiveDistanceScale">The AerialPerspectiveDistanceScale command value.</param>
/// <param name="AerialScatteringStrength">The AerialScatteringStrength command value.</param>
/// <param name="AerialPerspectiveStartDepthMeters">The AerialPerspectiveStartDepthMeters command value.</param>
/// <param name="HeightFogContribution">The HeightFogContribution command value.</param>
/// <param name="ExposureMode">The ExposureMode command value.</param>
/// <param name="ExposureEnabled">The ExposureEnabled command value.</param>
/// <param name="ExposureKey">The ExposureKey command value.</param>
/// <param name="ManualExposureEv">The ManualExposureEv command value.</param>
/// <param name="ExposureCompensation">The ExposureCompensation command value.</param>
/// <param name="ToneMapping">The ToneMapping command value.</param>
/// <param name="AutoExposureMeteringMode">The AutoExposureMeteringMode command value.</param>
/// <param name="AutoExposureMinEv">The AutoExposureMinEv command value.</param>
/// <param name="AutoExposureMaxEv">The AutoExposureMaxEv command value.</param>
/// <param name="AutoExposureSpeedUp">The AutoExposureSpeedUp command value.</param>
/// <param name="AutoExposureSpeedDown">The AutoExposureSpeedDown command value.</param>
/// <param name="AutoExposureLowPercentile">The AutoExposureLowPercentile command value.</param>
/// <param name="AutoExposureHighPercentile">The AutoExposureHighPercentile command value.</param>
/// <param name="AutoExposureMinLogLuminance">The AutoExposureMinLogLuminance command value.</param>
/// <param name="AutoExposureLogLuminanceRange">The AutoExposureLogLuminanceRange command value.</param>
/// <param name="AutoExposureTargetLuminance">The AutoExposureTargetLuminance command value.</param>
/// <param name="AutoExposureSpotMeterRadius">The AutoExposureSpotMeterRadius command value.</param>
/// <param name="BloomIntensity">The BloomIntensity command value.</param>
/// <param name="BloomThreshold">The BloomThreshold command value.</param>
/// <param name="Saturation">The Saturation command value.</param>
/// <param name="Contrast">The Contrast command value.</param>
/// <param name="VignetteIntensity">The VignetteIntensity command value.</param>
/// <param name="DisplayGamma">The DisplayGamma command value.</param>
public sealed record RuntimeSetEnvironment(
    bool AtmosphereEnabled,
    bool SunDiskEnabled,
    float PlanetRadiusMeters,
    float AtmosphereHeightMeters,
    Vector3 GroundAlbedoRgb,
    float RayleighScaleHeightMeters,
    float MieScaleHeightMeters,
    float MieAnisotropy,
    Vector3 SkyLuminanceFactorRgb,
    float AerialPerspectiveDistanceScale,
    float AerialScatteringStrength,
    float AerialPerspectiveStartDepthMeters,
    float HeightFogContribution,
    int ExposureMode,
    bool ExposureEnabled,
    float ExposureKey,
    float ManualExposureEv,
    float ExposureCompensation,
    int ToneMapping,
    int AutoExposureMeteringMode,
    float AutoExposureMinEv,
    float AutoExposureMaxEv,
    float AutoExposureSpeedUp,
    float AutoExposureSpeedDown,
    float AutoExposureLowPercentile,
    float AutoExposureHighPercentile,
    float AutoExposureMinLogLuminance,
    float AutoExposureLogLuminanceRange,
    float AutoExposureTargetLuminance,
    float AutoExposureSpotMeterRadius,
    float BloomIntensity,
    float BloomThreshold,
    float Saturation,
    float Contrast,
    float VignetteIntensity,
    float DisplayGamma) : RuntimeWorldCommand;
