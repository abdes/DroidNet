// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.SceneInspector.Components.Rendering;
using Oxygen.Editor.Scenes.Testing;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneInspector.Unit.Tests;

/// <summary>
/// The Rendering section edits the node-flags component through the scene document: the value lands in the node's
/// flags, as one undo step. Visibility and the shadow flags are source modes: index 0 is Inherit, 1 the local on value
/// and 2 the local off value, a mixed selection selects none, and under Inherit the field offers to select the
/// ancestor the value comes from.
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

    [TestMethod]
    public async Task RenderingSection_ChoosingHidden_WritesTheModeAndIsUndoable()
    {
        using var testScene = Open(out var node);
        using var editor = new NodeRenderingViewModel(() => testScene.Authoring);
        editor.UpdateValues([node]);
        _ = editor.Visibility.SelectedIndex.Should().Be((int)VisibilitySource.Inherit);

        editor.Visibility.SelectedIndex = (int)VisibilitySource.Hidden;
        await editor.PendingEdits.ConfigureAwait(false);

        _ = node.Flags.Visibility.Should().Be(VisibilitySource.Hidden);
        _ = editor.Visibility.SelectedIndex.Should().Be((int)VisibilitySource.Hidden);
        _ = testScene.Document.History.UndoStack.Should().ContainSingle();
        _ = await testScene.Authoring.UndoAsync().ConfigureAwait(false);
        _ = node.Flags.Visibility.Should().Be(VisibilitySource.Inherit);
    }

    [TestMethod]
    public async Task RenderingSection_MixedModes_SelectNoneAndChoosingOneWritesEveryNode()
    {
        using var testScene = OpenFamily(out var parent, out var child);
        child.Flags.CastsShadows = ShadowSource.Off;
        using var editor = new NodeRenderingViewModel(() => testScene.Authoring);
        editor.UpdateValues([parent, child]);
        _ = editor.CastsShadows.SelectedIndex.Should().Be(-1);
        _ = editor.CastsShadows.Caption.Should().NotBeEmpty("a mixed mode counts the modes");

        editor.CastsShadows.SelectedIndex = (int)ShadowSource.Inherit;
        await editor.PendingEdits.ConfigureAwait(false);

        _ = parent.Flags.CastsShadows.Should().Be(ShadowSource.Inherit);
        _ = child.Flags.CastsShadows.Should().Be(ShadowSource.Inherit);
        _ = testScene.Document.History.UndoStack.Should().ContainSingle();
    }

    [TestMethod]
    public void RenderingSection_InheritUnderALocalParent_SelectsTheParent()
    {
        using var testScene = OpenFamily(out var parent, out var child);
        parent.Flags.Visibility = VisibilitySource.Hidden;
        var selected = new List<NodeId>();
        using var editor = new NodeRenderingViewModel(() => testScene.Authoring, selected.Add);
        editor.UpdateValues([child]);

        editor.Visibility.SelectSource();

        _ = editor.Visibility.SourceToolTip.Should().NotBeEmpty();
        _ = selected.Should().Equal(parent.Id);
    }

    [TestMethod]
    public void RenderingSection_InheritOnARoot_HasNoSourceToSelect()
    {
        using var testScene = Open(out var node);
        var selected = new List<NodeId>();
        using var editor = new NodeRenderingViewModel(() => testScene.Authoring, selected.Add);
        editor.UpdateValues([node]);

        editor.Visibility.SelectSource();

        _ = editor.Visibility.Caption.Should().NotBeEmpty("Inherit names the scene default");
        _ = editor.Visibility.SourceToolTip.Should().BeEmpty();
        _ = selected.Should().BeEmpty();
    }

    // The caller disposes the opened scene.
    private static TestScene OpenFamily(out SceneNode parent, out SceneNode child)
    {
        var scene = new Scene { Name = "Inspector Scene" };
        var root = new SceneNode(scene, "Parent");
        root.Hierarchy.AddChild(new SceneNode(scene, "Child"));
        scene.RootNodes.Add(root);
        var testScene = TestScenes.Open(scene);
        parent = testScene.NodeOf(root);
        child = parent.Hierarchy.Children.Single();
        return testScene;
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
