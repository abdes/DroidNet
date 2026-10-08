// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>What the editing views show a transform gizmo for.</summary>
/// <param name="Tool">The viewport tool; Select shows no gizmo.</param>
/// <param name="Space">The axes translate and rotate follow.</param>
/// <param name="Snap">The snapping increments.</param>
/// <param name="Targets">The nodes the gizmo moves; none shows no gizmo.</param>
/// <param name="ActiveNodeId">The node whose origin is the pivot; the last target when absent.</param>
/// <param name="DisplayScale">Physical pixels per device-independent pixel, which sizes the gizmo.</param>
public sealed record RuntimeTransformGizmo(
    RuntimeTransformTool Tool,
    RuntimeTransformSpace Space,
    RuntimeTransformSnap Snap,
    IReadOnlyList<Guid> Targets,
    Guid? ActiveNodeId,
    float DisplayScale);
