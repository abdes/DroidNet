// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;

namespace Oxygen.Editor.LevelEditor;

/// <summary>What the pane shows next to the pointer while a gizmo drag runs.</summary>
/// <param name="Text">The applied value, for example "X 1.250 m".</param>
/// <param name="PointerPixel">The pointer, in physical pixels of the pane.</param>
/// <param name="PivotPixel">The gizmo pivot, in physical pixels, when in front of the camera.</param>
/// <param name="IsRejected">The pointer asks for a result the nodes cannot take.</param>
public sealed record GizmoFeedback(string Text, Vector2 PointerPixel, Vector2? PivotPixel, bool IsRejected);
