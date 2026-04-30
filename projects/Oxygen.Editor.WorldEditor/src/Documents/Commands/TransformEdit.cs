// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>
/// Partial edit for a transform component.
/// </summary>
/// <param name="Position">Optional local position.</param>
/// <param name="RotationEulerDegrees">Optional local rotation expressed as Euler degrees for inspector editing.</param>
/// <param name="Scale">Optional local scale.</param>
/// <param name="PositionX">Optional local position X axis.</param>
/// <param name="PositionY">Optional local position Y axis.</param>
/// <param name="PositionZ">Optional local position Z axis.</param>
/// <param name="RotationXDegrees">Optional local rotation X axis expressed as Euler degrees.</param>
/// <param name="RotationYDegrees">Optional local rotation Y axis expressed as Euler degrees.</param>
/// <param name="RotationZDegrees">Optional local rotation Z axis expressed as Euler degrees.</param>
/// <param name="ScaleX">Optional local scale X axis.</param>
/// <param name="ScaleY">Optional local scale Y axis.</param>
/// <param name="ScaleZ">Optional local scale Z axis.</param>
public sealed record TransformEdit(
    OptionalEditValue<Vector3> Position,
    OptionalEditValue<Vector3> RotationEulerDegrees,
    OptionalEditValue<Vector3> Scale,
    OptionalEditValue<float> PositionX = default,
    OptionalEditValue<float> PositionY = default,
    OptionalEditValue<float> PositionZ = default,
    OptionalEditValue<float> RotationXDegrees = default,
    OptionalEditValue<float> RotationYDegrees = default,
    OptionalEditValue<float> RotationZDegrees = default,
    OptionalEditValue<float> ScaleX = default,
    OptionalEditValue<float> ScaleY = default,
    OptionalEditValue<float> ScaleZ = default);
