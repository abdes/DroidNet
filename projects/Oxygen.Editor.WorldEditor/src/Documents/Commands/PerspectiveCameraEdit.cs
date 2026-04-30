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
public sealed record PerspectiveCameraEdit(
    OptionalEditValue<float> FieldOfViewDegrees,
    OptionalEditValue<float> AspectRatio,
    OptionalEditValue<float> NearPlane,
    OptionalEditValue<float> FarPlane);
