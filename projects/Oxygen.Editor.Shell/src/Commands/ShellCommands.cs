// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Windows.System;

namespace Oxygen.Editor.Shell.Commands;

/// <summary>The commands the Shell itself contributes.</summary>
public static class ShellCommands
{
    /// <summary>Gets the command that undoes the latest change of the history the user works in.</summary>
    public static CommandDescriptor Undo { get; } = new("Edit.Undo", "Undo")
    {
        Category = "Edit",
        KeyGestures = [new(VirtualKey.Z, VirtualKeyModifiers.Control)],
    };

    /// <summary>Gets the command that redoes the latest undone change of the history the user works in.</summary>
    public static CommandDescriptor Redo { get; } = new("Edit.Redo", "Redo")
    {
        Category = "Edit",
        KeyGestures = [new(VirtualKey.Y, VirtualKeyModifiers.Control), new(VirtualKey.Z, VirtualKeyModifiers.Control | VirtualKeyModifiers.Shift)],
    };

    /// <summary>Gets the command that moves the selection to the clipboard, to be moved where it is pasted.</summary>
    public static CommandDescriptor Cut { get; } = new("Edit.Cut", "Cut")
    {
        Category = "Edit",
        KeyGestures = [new(VirtualKey.X, VirtualKeyModifiers.Control)],
    };

    /// <summary>Gets the command that copies the selection to the clipboard.</summary>
    public static CommandDescriptor Copy { get; } = new("Edit.Copy", "Copy")
    {
        Category = "Edit",
        KeyGestures = [new(VirtualKey.C, VirtualKeyModifiers.Control)],
    };

    /// <summary>Gets the command that pastes the clipboard where the user works.</summary>
    public static CommandDescriptor Paste { get; } = new("Edit.Paste", "Paste")
    {
        Category = "Edit",
        KeyGestures = [new(VirtualKey.V, VirtualKeyModifiers.Control)],
    };

    /// <summary>Gets the command that deletes the selection of the part the user works in.</summary>
    /// <remarks>It has no key: each part keeps its own Delete key until one Delete key serves every part (#86).</remarks>
    public static CommandDescriptor Delete { get; } = new("Edit.Delete", "Delete") { Category = "Edit" };

    /// <summary>Gets every command the Shell contributes.</summary>
    public static IReadOnlyList<CommandDescriptor> All { get; } = [Undo, Redo, Cut, Copy, Paste, Delete];
}
