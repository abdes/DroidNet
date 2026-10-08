// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>A transform gizmo interaction for the editor to apply to the authored scene.</summary>
/// <param name="Kind">What happened.</param>
/// <param name="ViewId">The view of the interaction.</param>
/// <param name="Tool">The dragged gizmo's tool.</param>
/// <param name="Handle">The dragged handle.</param>
/// <param name="Duplicate">The drag began with Alt held: it moves copies of the targets.</param>
/// <param name="Hovering">For <see cref="RuntimeGizmoEventKind.Hover"/>: the pointer is over a handle.</param>
/// <param name="Representable">False while the drag asks for a result the nodes cannot represent.</param>
/// <param name="Targets">For updates and commits: every target's new local transform.</param>
/// <param name="ReadoutAxes">Bits 0 to 2: the X, Y and Z readout values apply.</param>
/// <param name="ReadoutValues">Metres per axis, degrees (in X) or scale factors.</param>
/// <param name="PivotPixel">The pivot in the view's physical pixels, when in front of the camera.</param>
/// <param name="PointerPixel">The pointer in the view's physical pixels.</param>
public sealed record RuntimeGizmoEvent(
    RuntimeGizmoEventKind Kind,
    RuntimeViewId ViewId,
    RuntimeTransformTool Tool,
    RuntimeGizmoHandle Handle,
    bool Duplicate,
    bool Hovering,
    bool Representable,
    IReadOnlyList<RuntimeGizmoTarget> Targets,
    int ReadoutAxes,
    Vector3 ReadoutValues,
    Vector2? PivotPixel,
    Vector2 PointerPixel);
