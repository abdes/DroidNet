// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Native environment presence and stored values from one scene mutation boundary.</summary>
public sealed record RuntimeEnvironmentState
{
    /// <summary>Gets a value indicating whether the native scene owns an environment.</summary>
    public bool Exists { get; init; }

    /// <summary>Gets a value indicating whether atmosphere values are present, including a disabled atmosphere.</summary>
    public bool AtmosphereExists { get; init; }

    /// <summary>Gets a value indicating whether post-process values are present.</summary>
    public bool PostProcessExists { get; init; }

    /// <summary>Gets a value indicating whether native atmosphere is enabled.</summary>
    public bool AtmosphereEnabled { get; init; }

    /// <summary>Gets a value indicating whether the native atmosphere draws a sun disk.</summary>
    public bool SunDiskEnabled { get; init; }

    /// <summary>Gets the observed PlanetRadiusMeters value.</summary>
    public float PlanetRadiusMeters { get; init; }

    /// <summary>Gets the observed AtmosphereHeightMeters value.</summary>
    public float AtmosphereHeightMeters { get; init; }

    /// <summary>Gets the observed GroundAlbedoRgb value.</summary>
    public Vector3 GroundAlbedoRgb { get; init; }

    /// <summary>Gets the observed RayleighScaleHeightMeters value.</summary>
    public float RayleighScaleHeightMeters { get; init; }

    /// <summary>Gets the observed MieScaleHeightMeters value.</summary>
    public float MieScaleHeightMeters { get; init; }

    /// <summary>Gets the observed MieAnisotropy value.</summary>
    public float MieAnisotropy { get; init; }

    /// <summary>Gets the observed SkyLuminanceFactorRgb value.</summary>
    public Vector3 SkyLuminanceFactorRgb { get; init; }

    /// <summary>Gets the observed AerialPerspectiveDistanceScale value.</summary>
    public float AerialPerspectiveDistanceScale { get; init; }

    /// <summary>Gets the observed AerialScatteringStrength value.</summary>
    public float AerialScatteringStrength { get; init; }

    /// <summary>Gets the observed AerialPerspectiveStartDepthMeters value.</summary>
    public float AerialPerspectiveStartDepthMeters { get; init; }

    /// <summary>Gets the observed HeightFogContribution value.</summary>
    public float HeightFogContribution { get; init; }

    /// <summary>Gets the observed ExposureMode value.</summary>
    public int ExposureMode { get; init; }

    /// <summary>Gets a value indicating whether native exposure is enabled.</summary>
    public bool ExposureEnabled { get; init; }

    /// <summary>Gets the observed ExposureKey value.</summary>
    public float ExposureKey { get; init; }

    /// <summary>Gets the observed ManualExposureEv value.</summary>
    public float ManualExposureEv { get; init; }

    /// <summary>Gets the observed ExposureCompensation value.</summary>
    public float ExposureCompensation { get; init; }

    /// <summary>Gets the observed ToneMapping value.</summary>
    public int ToneMapping { get; init; }

    /// <summary>Gets the observed AutoExposureMeteringMode value.</summary>
    public int AutoExposureMeteringMode { get; init; }

    /// <summary>Gets the observed AutoExposureMinEv value.</summary>
    public float AutoExposureMinEv { get; init; }

    /// <summary>Gets the observed AutoExposureMaxEv value.</summary>
    public float AutoExposureMaxEv { get; init; }

    /// <summary>Gets the observed AutoExposureSpeedUp value.</summary>
    public float AutoExposureSpeedUp { get; init; }

    /// <summary>Gets the observed AutoExposureSpeedDown value.</summary>
    public float AutoExposureSpeedDown { get; init; }

    /// <summary>Gets the observed AutoExposureLowPercentile value.</summary>
    public float AutoExposureLowPercentile { get; init; }

    /// <summary>Gets the observed AutoExposureHighPercentile value.</summary>
    public float AutoExposureHighPercentile { get; init; }

    /// <summary>Gets the observed AutoExposureMinLogLuminance value.</summary>
    public float AutoExposureMinLogLuminance { get; init; }

    /// <summary>Gets the observed AutoExposureLogLuminanceRange value.</summary>
    public float AutoExposureLogLuminanceRange { get; init; }

    /// <summary>Gets the observed AutoExposureTargetLuminance value.</summary>
    public float AutoExposureTargetLuminance { get; init; }

    /// <summary>Gets the observed AutoExposureSpotMeterRadius value.</summary>
    public float AutoExposureSpotMeterRadius { get; init; }

    /// <summary>Gets the observed BloomIntensity value.</summary>
    public float BloomIntensity { get; init; }

    /// <summary>Gets the observed BloomThreshold value.</summary>
    public float BloomThreshold { get; init; }

    /// <summary>Gets the observed Saturation value.</summary>
    public float Saturation { get; init; }

    /// <summary>Gets the observed Contrast value.</summary>
    public float Contrast { get; init; }

    /// <summary>Gets the observed VignetteIntensity value.</summary>
    public float VignetteIntensity { get; init; }

    /// <summary>Gets the observed DisplayGamma value.</summary>
    public float DisplayGamma { get; init; }
}
