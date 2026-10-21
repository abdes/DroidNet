// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Shell.Commands;
using Windows.System;

namespace Oxygen.Editor.SceneExplorer;

/// <summary>
/// The commands the Scene Explorer runs on the window's active scene wherever the user works. Their keys come after the
/// focused part.
/// </summary>
public static class SceneExplorerCommands
{
    /// <summary>Gets the command that starts renaming the selected item.</summary>
    public static CommandDescriptor Rename { get; } = new("Scene.Rename", "Rename")
    {
        Category = "Scene",
        KeyGestures = [new(VirtualKey.F2, VirtualKeyModifiers.None) { AfterFocusedPart = true }],
    };

    /// <summary>Gets the command that moves the focus to the Scene Explorer's search.</summary>
    public static CommandDescriptor FindInExplorer { get; } = new("Scene.FindInExplorer", "Find in Scene Explorer")
    {
        Category = "Scene",
        KeyGestures = [new(VirtualKey.F, VirtualKeyModifiers.Control) { AfterFocusedPart = true }],
    };

    /// <summary>Gets every command of the Scene Explorer.</summary>
    public static IReadOnlyList<CommandDescriptor> All { get; } = [Rename, FindInExplorer];
}
