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

}
