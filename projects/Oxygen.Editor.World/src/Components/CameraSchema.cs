// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Components;

/// <summary>
/// The scene schema's identities and creation values of the properties every camera has, for one camera kind: each kind
/// declares its own, so a camera reads the identities, ranges and defaults of its kind.
/// </summary>
/// <param name="NearPlaneId">The near plane property.</param>
/// <param name="FarPlaneId">The far plane property.</param>
/// <param name="ApertureFId">The aperture property.</param>
/// <param name="ShutterRateId">The shutter rate property.</param>
/// <param name="IsoId">The ISO property.</param>
/// <param name="NearPlane">The creation value of the near plane.</param>
/// <param name="FarPlane">The creation value of the far plane.</param>
/// <param name="ApertureF">The creation value of the aperture.</param>
/// <param name="ShutterRate">The creation value of the shutter rate.</param>
/// <param name="Iso">The creation value of the ISO sensitivity.</param>
internal sealed record CameraSchema(
    PropertyId<float> NearPlaneId,
    PropertyId<float> FarPlaneId,
    PropertyId<float> ApertureFId,
    PropertyId<float> ShutterRateId,
    PropertyId<float> IsoId,
    float NearPlane,
    float FarPlane,
    float ApertureF,
    float ShutterRate,
    float Iso)
{
    /// <summary>Gets the schema of a perspective camera.</summary>
    public static CameraSchema Perspective { get; } = new(
        SceneSchema.PerspectiveCamera.NearPlane,
        SceneSchema.PerspectiveCamera.FarPlane,
        SceneSchema.PerspectiveCamera.ApertureF,
        SceneSchema.PerspectiveCamera.ShutterRate,
        SceneSchema.PerspectiveCamera.Iso,
        SceneSchema.PerspectiveCamera.Defaults.NearPlane,
        SceneSchema.PerspectiveCamera.Defaults.FarPlane,
        SceneSchema.PerspectiveCamera.Defaults.ApertureF,
        SceneSchema.PerspectiveCamera.Defaults.ShutterRate,
        SceneSchema.PerspectiveCamera.Defaults.Iso);

    /// <summary>Gets the schema of an orthographic camera.</summary>
    public static CameraSchema Orthographic { get; } = new(
        SceneSchema.OrthographicCamera.NearPlane,
        SceneSchema.OrthographicCamera.FarPlane,
        SceneSchema.OrthographicCamera.ApertureF,
        SceneSchema.OrthographicCamera.ShutterRate,
        SceneSchema.OrthographicCamera.Iso,
        SceneSchema.OrthographicCamera.Defaults.NearPlane,
        SceneSchema.OrthographicCamera.Defaults.FarPlane,
        SceneSchema.OrthographicCamera.Defaults.ApertureF,
        SceneSchema.OrthographicCamera.Defaults.ShutterRate,
        SceneSchema.OrthographicCamera.Defaults.Iso);
}
