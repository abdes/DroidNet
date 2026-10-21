// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Shell.Commands;
using Windows.System;

namespace Oxygen.Editor.World.SceneEditor;

/// <summary>
/// The commands the scene editor runs on the window's active scene. Their keys come after the focused part: a list or a
/// tree that uses a letter for its own navigation keeps it.
/// </summary>
public static class SceneEditorCommands
{
    /// <summary>Gets the command that makes the select tool the active transform tool.</summary>
    public static CommandDescriptor SelectTool { get; } = Scene("Scene.Tool.Select", "Select Tool", VirtualKey.Q);

    /// <summary>Gets the command that makes the move tool the active transform tool.</summary>
    public static CommandDescriptor MoveTool { get; } = Scene("Scene.Tool.Move", "Move Tool", VirtualKey.W);

    /// <summary>Gets the command that makes the rotate tool the active transform tool.</summary>
    public static CommandDescriptor RotateTool { get; } = Scene("Scene.Tool.Rotate", "Rotate Tool", VirtualKey.E);

    /// <summary>Gets the command that makes the scale tool the active transform tool.</summary>
    public static CommandDescriptor ScaleTool { get; } = Scene("Scene.Tool.Scale", "Scale Tool", VirtualKey.R);

    /// <summary>Gets the command that frames the selected nodes in the active viewport.</summary>
    public static CommandDescriptor FrameSelection { get; } = Scene("Scene.FrameSelection", "Frame Selection", VirtualKey.F);

    /// <summary>Gets the command that frames the whole scene in the active viewport.</summary>
    public static CommandDescriptor FrameAll { get; } = Scene("Scene.FrameAll", "Frame All", VirtualKey.F, VirtualKeyModifiers.Shift);

    /// <summary>Gets the command that duplicates the selected nodes beside themselves.</summary>
    public static CommandDescriptor Duplicate { get; } = Scene("Scene.Duplicate", "Duplicate", VirtualKey.D, VirtualKeyModifiers.Control);

    /// <summary>Gets the command that plays the active scene.</summary>
    public static CommandDescriptor Play { get; } = Scene("Scene.Play", "Play", VirtualKey.F5);

    /// <summary>Gets the command that stops the scene that plays.</summary>
    public static CommandDescriptor StopPlay { get; } = Scene("Scene.StopPlay", "Stop", VirtualKey.F5, VirtualKeyModifiers.Shift);

    /// <summary>Gets every command of the scene editor.</summary>
    public static IReadOnlyList<CommandDescriptor> All { get; } = [SelectTool, MoveTool, RotateTool, ScaleTool, FrameSelection, FrameAll, Duplicate, Play, StopPlay];

    private static CommandDescriptor Scene(string id, string label, VirtualKey key, VirtualKeyModifiers modifiers = VirtualKeyModifiers.None)
        => new(id, label)
        {
            Category = "Scene",
            KeyGestures = [new(key, modifiers) { AfterFocusedPart = true }],
        };
}
