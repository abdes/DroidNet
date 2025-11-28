// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// Data transfer object for transform component data.
/// </summary>
public record TransformData
{
    /// <summary>
    /// Gets or initializes the local position.
    /// </summary>
    public Vector3 Position { get; init; }

    /// <summary>
    /// Gets or initializes the local rotation (as a quaternion).
    /// </summary>
    public Quaternion Rotation { get; init; }

    /// <summary>
    /// Gets or initializes the local scale.
    /// </summary>
    public Vector3 Scale { get; init; } = Vector3.One;
}
