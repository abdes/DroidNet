// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Carries a transform gizmo interaction.</summary>
/// <param name="gizmoEvent">The interaction.</param>
public sealed class RuntimeGizmoEventArgs(RuntimeGizmoEvent gizmoEvent) : EventArgs
{
    /// <summary>Gets the interaction.</summary>
    public RuntimeGizmoEvent Event { get; } = gizmoEvent;
}
