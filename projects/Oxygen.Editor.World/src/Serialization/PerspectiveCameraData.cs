// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// DTO for a perspective camera component.
/// </summary>
public record PerspectiveCameraData : CameraComponentData
{
    /// <summary>
    /// Initializes a new instance of the <see cref="PerspectiveCameraData"/> class with the scene schema's creation values.
    /// </summary>
    public PerspectiveCameraData()
        : base(CameraSchema.Perspective)
    {
    }

    /// <summary>
    /// Gets or sets the vertical field of view in degrees.
    /// </summary>
    public float FieldOfView { get; set; } = SceneSchema.PerspectiveCamera.Defaults.FieldOfViewDegrees;

    /// <summary>
    /// Gets or sets the camera aspect ratio (width / height).
    /// </summary>
    public float AspectRatio { get; set; } = SceneSchema.PerspectiveCamera.Defaults.AspectRatio;

    /// <summary>Gets the authored framing policy.</summary>
    public required CameraAspectMode AspectMode { get; init; }
}
