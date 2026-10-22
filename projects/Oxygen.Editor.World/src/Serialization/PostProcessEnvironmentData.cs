// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// Authored post-process parameters that map to Oxygen's native PostProcessVolume.
/// </summary>
public sealed record PostProcessEnvironmentData
{
    /// <summary>
    /// Gets or sets the tone mapper.
    /// </summary>
    public ToneMappingMode ToneMapper { get; set; } = ToneMappingMode.AcesFitted;

    /// <summary>
    /// Gets or sets the exposure mode.
    /// </summary>
    public ExposureMode ExposureMode { get; set; } = ExposureMode.Manual;

    /// <summary>
    /// Gets or sets a value indicating whether exposure is applied.
    /// </summary>
    public bool ExposureEnabled { get; set; } = true;

    /// <summary>
    /// Gets exposure compensation in EV stops.
    /// </summary>
    public float ExposureCompensationEv { get; init; }

    /// <summary>
    /// Gets or sets the display key scale applied after EV-to-linear calibration.
    /// </summary>
    public float ExposureKey { get; set; } = 12.5f;

    /// <summary>
    /// Gets or sets manual exposure in EV100.
    /// </summary>
    public float ManualExposureEv { get; set; } = 13.0f;

    /// <summary>
    /// Gets or sets the minimum auto-exposure EV.
    /// </summary>
    public float AutoExposureMinEv { get; set; } = -6.0f;

    /// <summary>
    /// Gets or sets the maximum auto-exposure EV.
    /// </summary>
    public float AutoExposureMaxEv { get; set; } = 16.0f;

    /// <summary>
    /// Gets or sets auto-exposure speed up in EV per second.
    /// </summary>
    public float AutoExposureSpeedUp { get; set; } = 3.0f;

    /// <summary>
    /// Gets or sets auto-exposure speed down in EV per second.
    /// </summary>
    public float AutoExposureSpeedDown { get; set; } = 1.0f;

    /// <summary>
    /// Gets or sets auto-exposure metering mode.
    /// </summary>
    public MeteringMode AutoExposureMeteringMode { get; set; } = MeteringMode.Average;

    /// <summary>
    /// Gets or sets the low histogram percentile used by auto exposure.
    /// </summary>
    public float AutoExposureLowPercentile { get; set; } = 0.1f;

    /// <summary>
    /// Gets or sets the high histogram percentile used by auto exposure.
    /// </summary>
    public float AutoExposureHighPercentile { get; set; } = 0.9f;

    /// <summary>
    /// Gets or sets the minimum log2 luminance used by auto exposure.
    /// </summary>
    public float AutoExposureMinLogLuminance { get; set; } = -12.0f;

    /// <summary>
    /// Gets or sets the log2 luminance range used by auto exposure.
    /// </summary>
    public float AutoExposureLogLuminanceRange { get; set; } = 25.0f;

    /// <summary>
    /// Gets or sets the target average luminance used by auto exposure.
    /// </summary>
    public float AutoExposureTargetLuminance { get; set; } = 0.18f;

    /// <summary>
    /// Gets or sets the spot-meter radius used by auto exposure.
    /// </summary>
    public float AutoExposureSpotMeterRadius { get; set; } = 0.2f;

    /// <summary>Gets the influence of dark histogram samples in [0, 1].</summary>
    public float AutoExposureBlackInfluence { get; init; }

    /// <summary>Gets or sets the hybrid adaptation transition distance in EV stops.</summary>
    public float AutoExposureTransitionDistanceEv { get; set; } = 1.5f;

    /// <summary>Gets the authored texture descriptor URI, or null for no mask.</summary>
    public AssetPath? AutoExposureMeteringMask { get; init; }

    /// <summary>Gets or sets the ordered exposure compensation keys.</summary>
    public ImmutableArray<ExposureCompensationKeyData> AutoExposureCompensationCurve { get; set; } = [];

    /// <summary>
    /// Gets bloom intensity.
    /// </summary>
    public float BloomIntensity { get; init; }

    /// <summary>
    /// Gets or sets bloom threshold in linear HDR units.
    /// </summary>
    public float BloomThreshold { get; set; } = 1.0f;

    /// <summary>
    /// Gets or sets color grading saturation multiplier.
    /// </summary>
    public float Saturation { get; set; } = 1.0f;

    /// <summary>
    /// Gets or sets color grading contrast multiplier.
    /// </summary>
    public float Contrast { get; set; } = 1.0f;

    /// <summary>
    /// Gets vignette intensity in [0, 1].
    /// </summary>
    public float VignetteIntensity { get; init; }

    /// <summary>
    /// Gets or sets display gamma applied after tone mapping.
    /// </summary>
    public float DisplayGamma { get; set; } = 2.2f;
}
