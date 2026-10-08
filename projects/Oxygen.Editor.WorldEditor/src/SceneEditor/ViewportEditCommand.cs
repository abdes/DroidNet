// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.LevelEditor;

/// <summary>An edit the pane's keyboard shortcuts ask the scene to make.</summary>
public enum ViewportEditCommand
{
    /// <summary>Delete the selection (Delete).</summary>
    Delete,

    /// <summary>Duplicate the selection beside itself (Ctrl+D).</summary>
    Duplicate,

    /// <summary>Undo (Ctrl+Z).</summary>
    Undo,

    /// <summary>Redo (Ctrl+Y).</summary>
    Redo,
}
