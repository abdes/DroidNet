// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>
/// Partial edit for a perspective camera component.
/// </summary>
/// <param name="FieldOfViewDegrees">Optional vertical field of view in degrees.</param>
/// <param name="AspectRatio">Optional aspect ratio.</param>
/// <param name="NearPlane">Optional near plane.</param>
/// <param name="FarPlane">Optional far plane.</param>
/// <param name="AspectMode">Optional framing policy.</param>
/// <param name="ApertureF">Optional aperture f-number.</param>
/// <param name="ShutterRate">Optional shutter rate in reciprocal seconds.</param>
/// <param name="Iso">Optional ISO sensitivity.</param>
public sealed record PerspectiveCameraEdit(
    OptionalEditValue<float> FieldOfViewDegrees,
    OptionalEditValue<float> AspectRatio,
    OptionalEditValue<float> NearPlane,
    OptionalEditValue<float> FarPlane,
    OptionalEditValue<Oxygen.Managed.Core.CameraAspectMode> AspectMode = default,
    OptionalEditValue<float> ApertureF = default,
    OptionalEditValue<float> ShutterRate = default,
    OptionalEditValue<float> Iso = default);
