// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// Authored post-process parameters that map to Oxygen's native PostProcessVolume.
/// </summary>
public sealed record PostProcessEnvironmentData
{
    /// <summary>
    /// Gets the tone mapper.
    /// </summary>
    public ToneMappingMode ToneMapper { get; init; } = ToneMappingMode.AcesFitted;

    /// <summary>
    /// Gets the exposure mode.
    /// </summary>
    public ExposureMode ExposureMode { get; init; } = ExposureMode.Auto;

    /// <summary>
    /// Gets a value indicating whether exposure is applied.
    /// </summary>
    public bool ExposureEnabled { get; init; } = true;

    /// <summary>
    /// Gets exposure compensation in EV stops.
    /// </summary>
    public float ExposureCompensationEv { get; init; }

    /// <summary>
    /// Gets the display key scale applied after EV-to-linear calibration.
    /// </summary>
    public float ExposureKey { get; init; } = 10.0f;

    /// <summary>
    /// Gets manual exposure in EV100.
    /// </summary>
    public float ManualExposureEv { get; init; } = 9.7f;

    /// <summary>
    /// Gets the minimum auto-exposure EV.
    /// </summary>
    public float AutoExposureMinEv { get; init; } = -6.0f;

    /// <summary>
    /// Gets the maximum auto-exposure EV.
    /// </summary>
    public float AutoExposureMaxEv { get; init; } = 16.0f;

    /// <summary>
    /// Gets auto-exposure speed up in EV per second.
    /// </summary>
    public float AutoExposureSpeedUp { get; init; } = 3.0f;

    /// <summary>
    /// Gets auto-exposure speed down in EV per second.
    /// </summary>
    public float AutoExposureSpeedDown { get; init; } = 1.0f;

    /// <summary>
    /// Gets auto-exposure metering mode.
    /// </summary>
    public MeteringMode AutoExposureMeteringMode { get; init; } = MeteringMode.Average;

    /// <summary>
    /// Gets the low histogram percentile used by auto exposure.
    /// </summary>
    public float AutoExposureLowPercentile { get; init; } = 0.1f;

    /// <summary>
    /// Gets the high histogram percentile used by auto exposure.
    /// </summary>
    public float AutoExposureHighPercentile { get; init; } = 0.9f;

    /// <summary>
    /// Gets the minimum log2 luminance used by auto exposure.
    /// </summary>
    public float AutoExposureMinLogLuminance { get; init; } = -12.0f;

    /// <summary>
    /// Gets the log2 luminance range used by auto exposure.
    /// </summary>
    public float AutoExposureLogLuminanceRange { get; init; } = 25.0f;

    /// <summary>
    /// Gets the target average luminance used by auto exposure.
    /// </summary>
    public float AutoExposureTargetLuminance { get; init; } = 0.18f;

    /// <summary>
    /// Gets the spot-meter radius used by auto exposure.
    /// </summary>
    public float AutoExposureSpotMeterRadius { get; init; } = 0.2f;

    /// <summary>
    /// Gets bloom intensity.
    /// </summary>
    public float BloomIntensity { get; init; }

    /// <summary>
    /// Gets bloom threshold in linear HDR units.
    /// </summary>
    public float BloomThreshold { get; init; } = 1.0f;

    /// <summary>
    /// Gets color grading saturation multiplier.
    /// </summary>
    public float Saturation { get; init; } = 1.0f;

    /// <summary>
    /// Gets color grading contrast multiplier.
    /// </summary>
    public float Contrast { get; init; } = 1.0f;

    /// <summary>
    /// Gets vignette intensity in [0, 1].
    /// </summary>
    public float VignetteIntensity { get; init; }

    /// <summary>
    /// Gets display gamma applied after tone mapping.
    /// </summary>
    public float DisplayGamma { get; init; } = 2.2f;
}
