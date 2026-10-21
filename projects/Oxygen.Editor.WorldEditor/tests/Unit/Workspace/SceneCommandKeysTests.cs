// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Shell.Commands;
using Oxygen.Editor.World.SceneEditor;
using Windows.System;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Workspace;

[TestClass]
[TestCategory("Viewport Tools")]
public sealed class SceneCommandKeysTests
{
    [TestMethod]
    public void SceneEditorCommands_BindTheEditorWideChordsAfterTheFocusedPart()
    {
        AssertKey(SceneEditorCommands.SelectTool, VirtualKey.Q, VirtualKeyModifiers.None);
        AssertKey(SceneEditorCommands.MoveTool, VirtualKey.W, VirtualKeyModifiers.None);
        AssertKey(SceneEditorCommands.RotateTool, VirtualKey.E, VirtualKeyModifiers.None);
        AssertKey(SceneEditorCommands.ScaleTool, VirtualKey.R, VirtualKeyModifiers.None);
        AssertKey(SceneEditorCommands.FrameSelection, VirtualKey.F, VirtualKeyModifiers.None);
        AssertKey(SceneEditorCommands.FrameAll, VirtualKey.F, VirtualKeyModifiers.Shift);
        AssertKey(SceneEditorCommands.Duplicate, VirtualKey.D, VirtualKeyModifiers.Control);
        AssertKey(SceneEditorCommands.Play, VirtualKey.F5, VirtualKeyModifiers.None);
        AssertKey(SceneEditorCommands.StopPlay, VirtualKey.F5, VirtualKeyModifiers.Shift);
    }

    [TestMethod]
    public void SceneEditorCommands_HaveUniqueIdentities()
    {
        var ids = SceneEditorCommands.All.Select(static command => command.Id).ToArray();

        _ = ids.Should().OnlyHaveUniqueItems();
    }

    private static void AssertKey(CommandDescriptor command, VirtualKey key, VirtualKeyModifiers modifiers)
    {
        var gesture = command.KeyGestures.Should().ContainSingle().Subject;
        _ = gesture.Key.Should().Be(key);
        _ = gesture.Modifiers.Should().Be(modifiers);
        _ = gesture.AfterFocusedPart.Should().BeTrue();
    }
}
