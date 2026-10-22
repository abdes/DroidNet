// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;

using System.Text.Json.Serialization;

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// DTO for authored directional light data.
/// </summary>
[JsonUnmappedMemberHandling(JsonUnmappedMemberHandling.Disallow)]
public sealed record DirectionalLightData : LightComponentData
{
    /// <summary>
    /// Gets or sets directional-light illuminance, in lux.
    /// </summary>
    public float IntensityLux { get; set; } = 100_000f;

    /// <summary>
    /// Gets or sets the apparent angular size, in radians.
    /// </summary>
    public float AngularSizeRadians { get; set; } = 0.00935f;

    /// <summary>Gets or sets the explicitly assigned atmosphere source slot.</summary>
    public AtmosphereLightSlot AtmosphereSlot { get; set; } = AtmosphereLightSlot.None;

    /// <summary>Gets a value indicating whether surfaces evaluate atmosphere transmittance per pixel.</summary>
    public bool UsePerPixelAtmosphereTransmittance { get; init; }

    /// <summary>Gets or sets the RGB luminance multiplier for the analytic disk.</summary>
    public Vector3 AtmosphereDiskLuminanceScaleRgb { get; set; } = Vector3.One;

    /// <summary>
    /// Gets or sets the number of directional shadow cascades.
    /// </summary>
    public int CascadeCount { get; set; } = 4;

    /// <summary>
    /// Gets or sets how CSM split distances are produced.
    /// </summary>
    public DirectionalCsmSplitMode SplitMode { get; set; } = DirectionalCsmSplitMode.Generated;

    /// <summary>
    /// Gets or sets the maximum directional shadow distance, in meters.
    /// </summary>
    public float MaxShadowDistance { get; set; } = 160f;

    /// <summary>
    /// Gets or sets the manual cascade distances, in meters.
    /// </summary>
    public Vector4 CascadeDistances { get; set; } = new(8f, 24f, 64f, 160f);

    /// <summary>
    /// Gets or sets the generated CSM distribution exponent.
    /// </summary>
    public float DistributionExponent { get; set; } = 3f;

    /// <summary>
    /// Gets or sets the cascade transition fraction.
    /// </summary>
    public float TransitionFraction { get; set; } = 0.1f;

    /// <summary>
    /// Gets or sets the shadow distance fadeout fraction.
    /// </summary>
    public float DistanceFadeoutFraction { get; set; } = 0.1f;
}
