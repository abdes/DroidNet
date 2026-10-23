// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;
using Oxygen.Editor.World.Components;

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// DTO for authored spot light data.
/// </summary>
[JsonUnmappedMemberHandling(JsonUnmappedMemberHandling.Disallow)]
public sealed record SpotLightData : LightComponentData
{
    /// <summary>
    /// Initializes a new instance of the <see cref="SpotLightData"/> class with the scene schema's creation values.
    /// </summary>
    public SpotLightData()
        : base(LightSchema.Spot)
    {
    }

    /// <summary>
    /// Gets or sets luminous flux, in lumens.
    /// </summary>
    public float LuminousFluxLumens { get; set; } = SceneSchema.SpotLight.Defaults.LuminousFluxLumens;

    /// <summary>
    /// Gets or sets the influence range, in meters.
    /// </summary>
    public float Range { get; set; } = SceneSchema.SpotLight.Defaults.Range;

    /// <summary>
    /// Gets the source radius, in meters.
    /// </summary>
    public float SourceRadius { get; init; } = SceneSchema.SpotLight.Defaults.SourceRadius;

    /// <summary>
    /// Gets or sets the inner cone angle, in radians.
    /// </summary>
    public float InnerConeAngleRadians { get; set; } = SceneSchema.SpotLight.Defaults.InnerConeAngleRadians;

    /// <summary>
    /// Gets or sets the outer cone angle, in radians.
    /// </summary>
    public float OuterConeAngleRadians { get; set; } = SceneSchema.SpotLight.Defaults.OuterConeAngleRadians;
}
