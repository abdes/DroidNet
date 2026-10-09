// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Messages;
using Windows.System;

namespace Oxygen.Editor.World.Workspace;

/// <summary>
/// The editor-wide scene key table. These keys work from any workspace pane, after the focused
/// pane declined them; a pane's navigation keys (arrows, Home, End, Enter, Space, Escape) stay
/// with the pane.
/// </summary>
internal static class SceneShortcuts
{
    /// <summary>Maps a key chord to its scene shortcut.</summary>
    /// <param name="key">The pressed key.</param>
    /// <param name="control">Whether Control is held.</param>
    /// <param name="shift">Whether Shift is held.</param>
    /// <param name="alt">Whether Alt is held.</param>
    /// <returns>The shortcut, or <see langword="null"/> when the chord has none.</returns>
    public static SceneShortcut? Map(VirtualKey key, bool control, bool shift, bool alt)
        => (key, control, shift, alt) switch
        {
            (VirtualKey.Q, false, false, false) => SceneShortcut.SelectTool,
            (VirtualKey.W, false, false, false) => SceneShortcut.MoveTool,
            (VirtualKey.E, false, false, false) => SceneShortcut.RotateTool,
            (VirtualKey.R, false, false, false) => SceneShortcut.ScaleTool,
            (VirtualKey.F, false, false, false) => SceneShortcut.FrameSelection,
            (VirtualKey.F, false, true, false) => SceneShortcut.FrameAll,
            (VirtualKey.D, true, false, false) => SceneShortcut.Duplicate,
            (VirtualKey.F2, false, false, false) => SceneShortcut.Rename,
            (VirtualKey.F, true, false, false) => SceneShortcut.FindInExplorer,
            _ => null,
        };
}
