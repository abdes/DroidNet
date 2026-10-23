// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using System.Text.Json.Serialization;
using Oxygen.Editor.World.Components;

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// DTO for authored directional light data.
/// </summary>
[JsonUnmappedMemberHandling(JsonUnmappedMemberHandling.Disallow)]
public sealed record DirectionalLightData : LightComponentData
{
    /// <summary>
    /// Initializes a new instance of the <see cref="DirectionalLightData"/> class with the scene schema's creation values.
    /// </summary>
    public DirectionalLightData()
        : base(LightSchema.Directional)
    {
    }

    /// <summary>
    /// Gets or sets directional-light illuminance, in lux.
    /// </summary>
    public float IntensityLux { get; set; } = SceneSchema.DirectionalLight.Defaults.IntensityLux;

    /// <summary>
    /// Gets or sets the apparent angular size, in radians.
    /// </summary>
    public float AngularSizeRadians { get; set; } = SceneSchema.DirectionalLight.Defaults.AngularSizeRadians;

    /// <summary>Gets or sets the explicitly assigned atmosphere source slot.</summary>
    public AtmosphereLightSlot AtmosphereSlot { get; set; } = SceneSchema.DirectionalLight.Defaults.AtmosphereSlot;

    /// <summary>Gets or sets a value indicating whether surfaces evaluate atmosphere transmittance per pixel.</summary>
    public bool UsePerPixelAtmosphereTransmittance { get; set; } = SceneSchema.DirectionalLight.Defaults.UsePerPixelAtmosphereTransmittance;

    /// <summary>Gets or sets the RGB luminance multiplier for the analytic disk.</summary>
    public Vector3 AtmosphereDiskLuminanceScaleRgb { get; set; } = SceneSchema.DirectionalLight.Defaults.AtmosphereDiskLuminanceScaleRgb;

    /// <summary>
    /// Gets or sets the number of directional shadow cascades.
    /// </summary>
    public int CascadeCount { get; set; } = SceneSchema.DirectionalLight.Defaults.CascadeCount;

    /// <summary>
    /// Gets or sets how CSM split distances are produced.
    /// </summary>
    public DirectionalCsmSplitMode SplitMode { get; set; } = SceneSchema.DirectionalLight.Defaults.SplitMode;

    /// <summary>
    /// Gets or sets the maximum directional shadow distance, in meters.
    /// </summary>
    public float MaxShadowDistance { get; set; } = SceneSchema.DirectionalLight.Defaults.MaxShadowDistance;

    /// <summary>
    /// Gets or sets the manual cascade distances, in meters.
    /// </summary>
    public Vector4 CascadeDistances { get; set; } = new(
        SceneSchema.DirectionalLight.Defaults.CascadeDistance0,
        SceneSchema.DirectionalLight.Defaults.CascadeDistance1,
        SceneSchema.DirectionalLight.Defaults.CascadeDistance2,
        SceneSchema.DirectionalLight.Defaults.CascadeDistance3);

    /// <summary>
    /// Gets or sets the generated CSM distribution exponent.
    /// </summary>
    public float DistributionExponent { get; set; } = SceneSchema.DirectionalLight.Defaults.DistributionExponent;

    /// <summary>
    /// Gets or sets the cascade transition fraction.
    /// </summary>
    public float TransitionFraction { get; set; } = SceneSchema.DirectionalLight.Defaults.TransitionFraction;

    /// <summary>
    /// Gets or sets the shadow distance fadeout fraction.
    /// </summary>
    public float DistanceFadeoutFraction { get; set; } = SceneSchema.DirectionalLight.Defaults.DistanceFadeoutFraction;
}
