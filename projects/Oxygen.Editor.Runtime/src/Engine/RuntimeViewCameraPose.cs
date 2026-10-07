// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>
/// Pose that places a scene node at a view's editor camera, in the node's parent space.
/// </summary>
/// <param name="Position">The local position.</param>
/// <param name="RotationEulerDegrees">The local rotation, in the editor's Euler-degree convention.</param>
/// <param name="Scale">The node's current local scale.</param>
/// <param name="OrthographicSize">
///     The orthographic half-height for an orthographic camera node, or <see langword="null"/> when the
///     authored size should stay unchanged.
/// </param>
/// <param name="FieldOfViewDegrees">
///     The view's vertical field of view for a perspective camera node when the view is perspective, or
///     <see langword="null"/> when the authored field of view should stay unchanged.
/// </param>
public sealed record RuntimeViewCameraPose(
    Vector3 Position,
    Vector3 RotationEulerDegrees,
    Vector3 Scale,
    float? OrthographicSize,
    float? FieldOfViewDegrees = null);
