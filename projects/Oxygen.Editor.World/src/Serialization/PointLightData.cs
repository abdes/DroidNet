// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;
using Oxygen.Editor.World.Components;

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// DTO for authored point light data.
/// </summary>
[JsonUnmappedMemberHandling(JsonUnmappedMemberHandling.Disallow)]
public sealed record PointLightData : LightComponentData
{
    /// <summary>
    /// Initializes a new instance of the <see cref="PointLightData"/> class with the scene schema's creation values.
    /// </summary>
    public PointLightData()
        : base(LightSchema.Point)
    {
    }

    /// <summary>
    /// Gets or sets luminous flux, in lumens.
    /// </summary>
    public float LuminousFluxLumens { get; set; } = SceneSchema.PointLight.Defaults.LuminousFluxLumens;

    /// <summary>
    /// Gets or sets the influence range, in meters.
    /// </summary>
    public float Range { get; set; } = SceneSchema.PointLight.Defaults.Range;

    /// <summary>
    /// Gets or sets the source radius, in meters.
    /// </summary>
    public float SourceRadius { get; set; } = SceneSchema.PointLight.Defaults.SourceRadius;
}
