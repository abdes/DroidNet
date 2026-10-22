// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// DTO for authored point light data.
/// </summary>
[JsonUnmappedMemberHandling(JsonUnmappedMemberHandling.Disallow)]
public sealed record PointLightData : LightComponentData
{
    /// <summary>
    /// Gets or sets luminous flux, in lumens.
    /// </summary>
    public float LuminousFluxLumens { get; set; } = 800f;

    /// <summary>
    /// Gets or sets the influence range, in meters.
    /// </summary>
    public float Range { get; set; } = 10f;

    /// <summary>
    /// Gets the source radius, in meters.
    /// </summary>
    public float SourceRadius { get; init; }
}
