// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;
using Oxygen.Editor.World.Components;

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// DTO for camera component base values.
/// </summary>
[JsonDerivedType(typeof(PerspectiveCameraData), "PerspectiveCamera")]
[JsonDerivedType(typeof(OrthographicCameraData), "OrthographicCamera")]
public abstract record CameraComponentData : ComponentData
{
    /// <summary>
    /// Gets the distance to the near clipping plane.
    /// </summary>
    public float NearPlane { get; init; }

    /// <summary>
    /// Gets the distance to the far clipping plane.
    /// </summary>
    public float FarPlane { get; init; }
    /// <summary>Gets the authored aperture as an f-number.</summary>
    public float ApertureF { get; init; } = CameraComponent.DefaultApertureF;

    /// <summary>Gets the authored shutter rate in reciprocal seconds.</summary>
    public float ShutterRate { get; init; } = CameraComponent.DefaultShutterRate;

    /// <summary>Gets the authored ISO sensitivity.</summary>
    public float Iso { get; init; } = CameraComponent.DefaultIso;

}
