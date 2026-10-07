// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// DTO for an orthographic camera component.
/// </summary>
public record OrthographicCameraData : CameraComponentData
{
    /// <summary>Gets half of the visible height, in meters.</summary>
    public float OrthographicSize { get; init; } = OrthographicCamera.DefaultOrthographicSize;

    /// <summary>Gets the retained Fixed aspect ratio (width / height).</summary>
    public float AspectRatio { get; init; } = OrthographicCamera.DefaultAspectRatio;

    /// <summary>Gets the authored framing policy.</summary>
    public required CameraAspectMode AspectMode { get; init; }
}
