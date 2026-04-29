// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// Scene-level environment authoring data.
/// </summary>
public sealed record SceneEnvironmentData
{
    /// <summary>
    /// Gets a value indicating whether atmosphere rendering is enabled.
    /// </summary>
    public bool AtmosphereEnabled { get; init; } = true;

    /// <summary>
    /// Gets authored sky atmosphere parameters mirrored by the native scene descriptor.
    /// </summary>
    public SkyAtmosphereEnvironmentData SkyAtmosphere { get; init; } = new();

    /// <summary>
    /// Gets the scene node identity bound as the sun light, or null when no sun is bound.
    /// </summary>
    public Guid? SunNodeId { get; init; }

    /// <summary>
    /// Gets the exposure mode.
    /// </summary>
    public ExposureMode ExposureMode { get; init; } = ExposureMode.Auto;

    /// <summary>
    /// Gets manual exposure in EV100, used when <see cref="ExposureMode"/> is <see cref="ExposureMode.Manual"/>.
    /// </summary>
    public float ManualExposureEv { get; init; } = 9.7f;

    /// <summary>
    /// Gets exposure compensation in EV stops.
    /// </summary>
    public float ExposureCompensation { get; init; }

    /// <summary>
    /// Gets the tone mapping mode.
    /// </summary>
    public ToneMappingMode ToneMapping { get; init; } = ToneMappingMode.AcesFitted;

    /// <summary>
    /// Gets authored post-process parameters mirrored from Oxygen's native PostProcessVolume.
    /// </summary>
    public PostProcessEnvironmentData PostProcess { get; init; } = new();

    /// <summary>
    /// Gets the background color used when atmosphere rendering is disabled.
    /// </summary>
    public Vector3 BackgroundColor { get; init; }
}
