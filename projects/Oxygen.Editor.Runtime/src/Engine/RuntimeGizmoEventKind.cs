// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>What happened in a gizmo interaction.</summary>
public enum RuntimeGizmoEventKind
{
    /// <summary>The pointer entered or left the gizmo's handles in a view.</summary>
    Hover = 0,

    /// <summary>A drag started; nothing has moved yet.</summary>
    Begin = 1,

    /// <summary>The drag's results changed.</summary>
    Update = 2,

    /// <summary>The drag ended with its final results.</summary>
    Commit = 3,

    /// <summary>The drag was cancelled; the targets keep their starting transforms.</summary>
    Cancel = 4,

    /// <summary>An orientation triad axis was clicked; the handle names it.</summary>
    ViewAxis = 5,
}
