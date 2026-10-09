// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.World.Messages;
using Oxygen.Editor.World.Workspace;
using Windows.System;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Workspace;

[TestClass]
[TestCategory("Viewport Tools")]
public sealed class SceneShortcutsTests
{
    [TestMethod]
    [DataRow(VirtualKey.Q, false, false, (int)SceneShortcut.SelectTool)]
    [DataRow(VirtualKey.W, false, false, (int)SceneShortcut.MoveTool)]
    [DataRow(VirtualKey.E, false, false, (int)SceneShortcut.RotateTool)]
    [DataRow(VirtualKey.R, false, false, (int)SceneShortcut.ScaleTool)]
    [DataRow(VirtualKey.F, false, false, (int)SceneShortcut.FrameSelection)]
    [DataRow(VirtualKey.F, false, true, (int)SceneShortcut.FrameAll)]
    [DataRow(VirtualKey.D, true, false, (int)SceneShortcut.Duplicate)]
    [DataRow(VirtualKey.F2, false, false, (int)SceneShortcut.Rename)]
    [DataRow(VirtualKey.F, true, false, (int)SceneShortcut.FindInExplorer)]
    public void Map_BindsTheEditorWideChords(VirtualKey key, bool control, bool shift, int expected)
        => _ = SceneShortcuts.Map(key, control, shift, alt: false).Should().Be((SceneShortcut)expected);

    [TestMethod]
    [DataRow(VirtualKey.Q, false, true, false)]
    [DataRow(VirtualKey.W, true, false, false)]
    [DataRow(VirtualKey.F, false, false, true)]
    [DataRow(VirtualKey.Space, false, false, false)]
    [DataRow(VirtualKey.A, false, false, false)]
    [DataRow(VirtualKey.Up, false, false, false)]
    [DataRow(VirtualKey.Delete, false, false, false)]
    [DataRow(VirtualKey.D, true, true, false)]
    public void Map_LeavesOtherChordsToTheFocusedPane(VirtualKey key, bool control, bool shift, bool alt)
        => _ = SceneShortcuts.Map(key, control, shift, alt).Should().BeNull();
}
