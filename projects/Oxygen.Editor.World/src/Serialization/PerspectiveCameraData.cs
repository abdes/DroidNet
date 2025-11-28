// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// DTO for a perspective camera component.
/// </summary>
public record PerspectiveCameraData : CameraComponentData
{
    /// <summary>
    /// Gets the vertical field of view in degrees.
    /// </summary>
    public float FieldOfView { get; init; }

    /// <summary>
    /// Gets the camera aspect ratio (width / height).
    /// </summary>
    public float AspectRatio { get; init; }
}
