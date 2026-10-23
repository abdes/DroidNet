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
    /// <summary>Initializes a new instance of the <see cref="CameraComponentData"/> class with the creation values of its kind.</summary>
    /// <param name="schema">The scene schema of the camera kind.</param>
    private protected CameraComponentData(CameraSchema schema)
    {
        ArgumentNullException.ThrowIfNull(schema);
        this.NearPlane = schema.NearPlane;
        this.FarPlane = schema.FarPlane;
        this.ApertureF = schema.ApertureF;
        this.ShutterRate = schema.ShutterRate;
        this.Iso = schema.Iso;
    }

    /// <summary>
    /// Gets or sets the distance to the near clipping plane.
    /// </summary>
    public float NearPlane { get; set; }

    /// <summary>
    /// Gets or sets the distance to the far clipping plane.
    /// </summary>
    public float FarPlane { get; set; }

    /// <summary>Gets or sets the authored aperture as an f-number.</summary>
    public float ApertureF { get; set; }

    /// <summary>Gets or sets the authored shutter rate in reciprocal seconds.</summary>
    public float ShutterRate { get; set; }

    /// <summary>Gets or sets the authored ISO sensitivity.</summary>
    public float Iso { get; set; }
}
