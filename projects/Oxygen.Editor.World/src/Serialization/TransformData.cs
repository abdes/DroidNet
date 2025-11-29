// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using System.Text.Json.Serialization;

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// Data transfer object for transform component data.
/// </summary>
public record TransformData : ComponentData
{
    /// <summary>
    /// Gets or initializes the local position. Default is <see cref="Vector3.Zero" />.
    /// </summary>
    [JsonPropertyName("LocalPosition")]
    public Vector3 Position { get; init; }

    /// <summary>
    /// Gets or initializes the local rotation (as a quaternion). Default is <see cref="Quaternion.Identity" />.
    /// </summary>
    [JsonPropertyName("LocalRotation")]
    public Quaternion Rotation { get; init; } = Quaternion.Identity;

    /// <summary>
    /// Gets or initializes the local scale. Default is <see cref="Vector3.One" />.
    /// </summary>
    [JsonPropertyName("LocalScale")]
    public Vector3 Scale { get; init; } = Vector3.One;
}
