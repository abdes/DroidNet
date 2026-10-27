// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Shell.Commands;
using Windows.System;

namespace Oxygen.Editor.SceneExplorer.Unit.Tests;

[TestClass]
[TestCategory("Scene Explorer")]
public sealed class SceneExplorerCommandsTests
{
    [TestMethod]
    public void SceneExplorerCommands_BindTheExplorerChordsAfterTheFocusedPart()
    {
        AssertKey(SceneExplorerCommands.Rename, VirtualKey.F2, VirtualKeyModifiers.None);
        AssertKey(SceneExplorerCommands.FindInExplorer, VirtualKey.F, VirtualKeyModifiers.Control);
    }

    [TestMethod]
    public void SceneExplorerModule_ContributesThePanelAndTheExplorerCommands()
    {
        var module = new SceneExplorerModule();

        _ = module.Panels.Should().ContainSingle().Which.Id.Should().Be(SceneExplorerModule.PanelId);
        _ = module.Commands.Should().Equal(SceneExplorerCommands.All);
        _ = module.Commands.Should().OnlyHaveUniqueItems(static command => command.Id);
    }

    private static void AssertKey(CommandDescriptor command, VirtualKey key, VirtualKeyModifiers modifiers)
    {
        var gesture = command.KeyGestures.Should().ContainSingle().Subject;
        _ = gesture.Key.Should().Be(key);
        _ = gesture.Modifiers.Should().Be(modifiers);
        _ = gesture.AfterFocusedPart.Should().BeTrue();
    }
}
