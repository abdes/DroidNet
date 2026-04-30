// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// DTO for authored directional light data.
/// </summary>
public sealed record DirectionalLightData : LightComponentData
{
    /// <summary>
    /// Gets directional-light illuminance, in lux.
    /// </summary>
    public float IntensityLux { get; init; } = 100_000f;

    /// <summary>
    /// Gets the apparent angular size, in radians.
    /// </summary>
    public float AngularSizeRadians { get; init; } = 0.00935f;

    /// <summary>
    /// Gets a value indicating whether this light contributes to the scene environment.
    /// </summary>
    public bool EnvironmentContribution { get; init; } = true;

    /// <summary>
    /// Gets a value indicating whether this directional light is the active sun.
    /// </summary>
    public bool IsSunLight { get; init; } = true;

    /// <summary>
    /// Gets the number of directional shadow cascades.
    /// </summary>
    public int CascadeCount { get; init; } = 4;

    /// <summary>
    /// Gets how CSM split distances are produced.
    /// </summary>
    public DirectionalCsmSplitMode SplitMode { get; init; } = DirectionalCsmSplitMode.Generated;

    /// <summary>
    /// Gets the maximum directional shadow distance, in meters.
    /// </summary>
    public float MaxShadowDistance { get; init; } = 160f;

    /// <summary>
    /// Gets the manual cascade distances, in meters.
    /// </summary>
    public Vector4 CascadeDistances { get; init; } = new(8f, 24f, 64f, 160f);

    /// <summary>
    /// Gets the generated CSM distribution exponent.
    /// </summary>
    public float DistributionExponent { get; init; } = 3f;

    /// <summary>
    /// Gets the cascade transition fraction.
    /// </summary>
    public float TransitionFraction { get; init; } = 0.1f;

    /// <summary>
    /// Gets the shadow distance fadeout fraction.
    /// </summary>
    public float DistanceFadeoutFraction { get; init; } = 0.1f;
}
