// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Scenes.Changes;

/// <summary>Which step of an edit a property change belongs to.</summary>
public enum EditPhase
{
    /// <summary>The change is final: a one-shot edit, an undo or redo, or a committed gesture.</summary>
    Commit = 0,

    /// <summary>The change previews a gesture that is still open; nothing is recorded yet.</summary>
    Preview = 1,

    /// <summary>The change restores the state a cancelled gesture started from.</summary>
    Cancel = 2,
}
