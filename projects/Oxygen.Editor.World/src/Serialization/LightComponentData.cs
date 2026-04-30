// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using System.Text.Json.Serialization;

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// DTO for common authored light component values.
/// </summary>
[JsonDerivedType(typeof(DirectionalLightData), "DirectionalLight")]
[JsonDerivedType(typeof(PointLightData), "PointLight")]
[JsonDerivedType(typeof(SpotLightData), "SpotLight")]
public abstract record LightComponentData : ComponentData
{
    /// <summary>
    /// Gets a value indicating whether the light contributes to world lighting.
    /// </summary>
    public bool AffectsWorld { get; init; } = true;

    /// <summary>
    /// Gets the light color multiplier in linear RGB.
    /// </summary>
    public Vector3 Color { get; init; } = Vector3.One;

    /// <summary>
    /// Gets the runtime participation mode for this light.
    /// </summary>
    public LightMobility Mobility { get; init; } = LightMobility.Realtime;

    /// <summary>
    /// Gets a value indicating whether the light casts shadows.
    /// </summary>
    public bool CastsShadows { get; init; }

    /// <summary>
    /// Gets the authored shadow settings.
    /// </summary>
    public LightShadowSettingsData? Shadow { get; init; } = new();

    /// <summary>
    /// Gets exposure compensation in EV stops.
    /// </summary>
    public float ExposureCompensation { get; init; }
}
