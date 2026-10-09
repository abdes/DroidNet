// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging.Messages;

namespace Oxygen.Editor.World.Messages;

/// <summary>An editor-wide scene shortcut, available whichever workspace pane has focus.</summary>
internal enum SceneShortcut
{
    /// <summary>Use the Select tool.</summary>
    SelectTool,

    /// <summary>Use the Move tool.</summary>
    MoveTool,

    /// <summary>Use the Rotate tool.</summary>
    RotateTool,

    /// <summary>Use the Scale tool.</summary>
    ScaleTool,

    /// <summary>Frame the selection in the active viewport.</summary>
    FrameSelection,

    /// <summary>Frame the whole scene in the active viewport.</summary>
    FrameAll,

    /// <summary>Duplicate the selection in place.</summary>
    Duplicate,

    /// <summary>Rename the selected node in the Scene Explorer.</summary>
    Rename,

    /// <summary>Move keyboard focus to the Scene Explorer's search box.</summary>
    FindInExplorer,
}

/// <summary>
/// Asks the workspace's active scene editor or Scene Explorer to run a shortcut. A reply of
/// <see langword="true"/> means a receiver ran it.
/// </summary>
/// <param name="Shortcut">The shortcut.</param>
internal sealed class SceneShortcutMessage(SceneShortcut Shortcut) : RequestMessage<bool>
{
    /// <summary>Gets the shortcut.</summary>
    public SceneShortcut Shortcut { get; } = Shortcut;
}
