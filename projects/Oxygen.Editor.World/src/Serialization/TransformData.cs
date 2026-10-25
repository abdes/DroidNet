// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using System.Text.Json.Serialization;
using Oxygen.Editor.World.Utils;

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// Data transfer object for transform component data.
/// </summary>
public record TransformData : ComponentData
{
    /// <summary>
    /// Gets or initializes the local position; the scene schema's default is the origin.
    /// </summary>
    [JsonPropertyName("LocalPosition")]
    public Vector3 Position { get; init; } = new(
        SceneSchema.Transform.Defaults.PositionX,
        SceneSchema.Transform.Defaults.PositionY,
        SceneSchema.Transform.Defaults.PositionZ);

    /// <summary>
    /// Gets or sets the local rotation (as a quaternion); the scene schema's default Euler angles give the identity.
    /// </summary>
    [JsonPropertyName("LocalRotation")]
    public Quaternion Rotation { get; set; } = TransformConverter.EulerDegreesToQuaternion(SceneSchema.Transform.Defaults.Rotation);

    /// <summary>
    /// Gets or sets the local scale; the scene schema's default is one on every axis.
    /// </summary>
    [JsonPropertyName("LocalScale")]
    public Vector3 Scale { get; set; } = new(
        SceneSchema.Transform.Defaults.ScaleX,
        SceneSchema.Transform.Defaults.ScaleY,
        SceneSchema.Transform.Defaults.ScaleZ);
}
