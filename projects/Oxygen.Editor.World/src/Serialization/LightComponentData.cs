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
    /// Gets or sets a value indicating whether the light contributes to world lighting.
    /// </summary>
    public bool AffectsWorld { get; set; } = true;

    /// <summary>
    /// Gets or sets the light color multiplier in linear RGB.
    /// </summary>
    public Vector3 Color { get; set; } = Vector3.One;

    /// <summary>
    /// Gets a value indicating whether the light casts shadows.
    /// </summary>
    public bool CastsShadows { get; init; }

    /// <summary>
    /// Gets or sets the authored shadow settings.
    /// </summary>
    public LightShadowSettingsData? Shadow { get; set; } = new();

    /// <summary>
    /// Gets exposure compensation in EV stops.
    /// </summary>
    public float ExposureCompensation { get; init; }
}
