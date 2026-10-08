// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>
/// The state of a view's editor navigation camera: what a pane keeps when its view is released and
/// what a new view starts from.
/// </summary>
/// <param name="Position">The camera position in world space.</param>
/// <param name="Rotation">The camera rotation in world space.</param>
/// <param name="FocusPoint">The point the orbit navigation turns around.</param>
/// <param name="OrthographicSize">The half-height of the orthographic presets' view volume.</param>
public sealed record RuntimeEditorCamera(
    Vector3 Position,
    Quaternion Rotation,
    Vector3 FocusPoint,
    float OrthographicSize);
