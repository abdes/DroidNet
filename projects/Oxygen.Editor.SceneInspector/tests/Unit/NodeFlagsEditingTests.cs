// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.SceneInspector.Components.Rendering;
using Oxygen.Editor.Scenes.Testing;
using Oxygen.Editor.World;

namespace Oxygen.Editor.SceneInspector.Unit.Tests;

/// <summary>
/// The Rendering section edits the node-flags component through the scene document: the value lands in the node's
/// flags, as one undo step.
/// </summary>
[TestClass]
[TestCategory("Inspector")]
public sealed class NodeFlagsEditingTests
{
    [TestMethod]
    public async Task RenderingSection_SettingStatic_WritesTheNodeFlagsAndIsUndoable()
    {
        using var testScene = Open(out var node);
        using var editor = new NodeRenderingViewModel(() => testScene.Authoring);
        editor.UpdateValues([node]);

        editor.IsStatic = true;
        await editor.PendingEdits.ConfigureAwait(false);

        _ = node.Flags.IsStatic.Should().BeTrue();
        _ = testScene.Document.History.UndoStack.Should().ContainSingle();
        _ = await testScene.Authoring.UndoAsync().ConfigureAwait(false);
        _ = node.Flags.IsStatic.Should().BeFalse();
    }

    [TestMethod]
    public async Task RenderingSection_SettingRayCastingSelectable_WritesTheNodeFlags()
    {
        using var testScene = Open(out var node);
        using var editor = new NodeRenderingViewModel(() => testScene.Authoring);
        editor.UpdateValues([node]);
        _ = node.Flags.IsRayCastingSelectable.Should().BeTrue();

        editor.IsRayCastingSelectable = false;
        await editor.PendingEdits.ConfigureAwait(false);

        _ = node.Flags.IsRayCastingSelectable.Should().BeFalse();
        _ = testScene.Document.History.UndoStack.Should().ContainSingle();
    }

    // The caller disposes the opened scene.
    private static TestScene Open(out SceneNode opened)
    {
        var scene = new Scene { Name = "Inspector Scene" };
        var node = new SceneNode(scene, "Group");
        scene.RootNodes.Add(node);
        var testScene = TestScenes.Open(scene);
        opened = testScene.NodeOf(node);
        return testScene;
    }
}
