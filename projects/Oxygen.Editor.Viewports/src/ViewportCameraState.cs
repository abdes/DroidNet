// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using Oxygen.Editor.Runtime.Engine;

namespace Oxygen.Editor.Viewports;

/// <summary>
/// A pane's editor camera, stored as plain fields: the settings serializer does not write the
/// fields of <see cref="Vector3"/> and <see cref="Quaternion"/>.
/// </summary>
/// <param name="PositionX">The position X.</param>
/// <param name="PositionY">The position Y.</param>
/// <param name="PositionZ">The position Z.</param>
/// <param name="RotationX">The rotation quaternion X.</param>
/// <param name="RotationY">The rotation quaternion Y.</param>
/// <param name="RotationZ">The rotation quaternion Z.</param>
/// <param name="RotationW">The rotation quaternion W.</param>
/// <param name="FocusX">The orbit focus point X.</param>
/// <param name="FocusY">The orbit focus point Y.</param>
/// <param name="FocusZ">The orbit focus point Z.</param>
/// <param name="OrthographicSize">The orthographic half-height.</param>
public sealed record ViewportCameraState(
    float PositionX,
    float PositionY,
    float PositionZ,
    float RotationX,
    float RotationY,
    float RotationZ,
    float RotationW,
    float FocusX,
    float FocusY,
    float FocusZ,
    float OrthographicSize)
{
    /// <summary>Creates the stored form of a runtime editor camera.</summary>
    /// <param name="camera">The runtime camera, or <see langword="null"/>.</param>
    /// <returns>The stored camera, or <see langword="null"/>.</returns>
    public static ViewportCameraState? From(RuntimeEditorCamera? camera)
        => camera is null ? null : new(
            camera.Position.X,
            camera.Position.Y,
            camera.Position.Z,
            camera.Rotation.X,
            camera.Rotation.Y,
            camera.Rotation.Z,
            camera.Rotation.W,
            camera.FocusPoint.X,
            camera.FocusPoint.Y,
            camera.FocusPoint.Z,
            camera.OrthographicSize);

    /// <summary>Creates the runtime editor camera this state describes.</summary>
    /// <returns>The runtime camera.</returns>
    public RuntimeEditorCamera ToRuntime()
        => new(
            new Vector3(this.PositionX, this.PositionY, this.PositionZ),
            new Quaternion(this.RotationX, this.RotationY, this.RotationZ, this.RotationW),
            new Vector3(this.FocusX, this.FocusY, this.FocusZ),
            this.OrthographicSize);

    /// <summary>Gets whether every component is a finite number.</summary>
    /// <returns><see langword="true"/> when the state is usable.</returns>
    public bool IsFinite()
        => new[] { this.PositionX, this.PositionY, this.PositionZ, this.RotationX, this.RotationY, this.RotationZ, this.RotationW, this.FocusX, this.FocusY, this.FocusZ, this.OrthographicSize }
            .All(float.IsFinite);
}
