// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// DTO for an orthographic camera component.
/// </summary>
public record OrthographicCameraData : CameraComponentData
{
    /// <summary>
    /// Gets the orthographic half-size (extent) of the camera.
    /// </summary>
    public float OrthographicSize { get; init; }
}
