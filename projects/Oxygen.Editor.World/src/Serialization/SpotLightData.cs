// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// DTO for authored spot light data.
/// </summary>
public sealed record SpotLightData : LightComponentData
{
    /// <summary>
    /// Gets luminous flux, in lumens.
    /// </summary>
    public float LuminousFluxLumens { get; init; } = 800f;

    /// <summary>
    /// Gets the influence range, in meters.
    /// </summary>
    public float Range { get; init; } = 10f;

    /// <summary>
    /// Gets the source radius, in meters.
    /// </summary>
    public float SourceRadius { get; init; }

    /// <summary>
    /// Gets the attenuation decay exponent.
    /// </summary>
    public float DecayExponent { get; init; } = 2f;

    /// <summary>
    /// Gets the inner cone angle, in radians.
    /// </summary>
    public float InnerConeAngleRadians { get; init; } = 0.4f;

    /// <summary>
    /// Gets the outer cone angle, in radians.
    /// </summary>
    public float OuterConeAngleRadians { get; init; } = 0.6f;
}
