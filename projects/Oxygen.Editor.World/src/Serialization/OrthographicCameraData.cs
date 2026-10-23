// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// DTO for an orthographic camera component.
/// </summary>
public record OrthographicCameraData : CameraComponentData
{
    /// <summary>Initializes a new instance of the <see cref="OrthographicCameraData"/> class with the scene schema's creation values.</summary>
    public OrthographicCameraData()
        : base(CameraSchema.Orthographic)
    {
    }

    /// <summary>Gets or sets half of the visible height, in meters.</summary>
    public float OrthographicSize { get; set; } = SceneSchema.OrthographicCamera.Defaults.OrthographicSize;

    /// <summary>Gets or sets the retained Fixed aspect ratio (width / height).</summary>
    public float AspectRatio { get; set; } = SceneSchema.OrthographicCamera.Defaults.AspectRatio;

    /// <summary>Gets the authored framing policy.</summary>
    public required CameraAspectMode AspectMode { get; init; }
}
