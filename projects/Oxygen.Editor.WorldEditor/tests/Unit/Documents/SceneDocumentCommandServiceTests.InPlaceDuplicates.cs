// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Documents;

/// <summary>Duplicating nodes beside themselves, as the viewport's Ctrl+D and Alt-drag do.</summary>
public sealed partial class SceneDocumentCommandServiceTests
{
    [TestMethod]
    public async Task DuplicateNodesInPlaceAsync_PutsEachCopyRightAfterItsSourceAsOneUndoStep()
    {
        var fixture = CreateFixture();
        ConfigureHierarchySync(fixture);
        var scene = CreateScene();
        var first = new SceneNode(scene) { Name = "First" };
        var parent = new SceneNode(scene) { Name = "Parent" };
        var child = new SceneNode(scene) { Name = "Child" };
        var last = new SceneNode(scene) { Name = "Last" };
        scene.RootNodes.Add(first);
        scene.RootNodes.Add(parent);
        parent.AddChild(child);
        parent.AddChild(new SceneNode(scene) { Name = "Sibling" });
        scene.RootNodes.Add(last);
        var context = CreateContext(scene);

        var result = await fixture.Sut.DuplicateNodesInPlaceAsync(context, [first.Id, child.Id]).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = result.Value.Should().HaveCount(2);
        _ = scene.RootNodes.Select(static node => node.Name).Should().Equal("First", "First", "Parent", "Last");
        _ = parent.Children.Select(static node => node.Name).Should().Equal("Child", "Child", "Sibling");
        _ = result.Value![1].Parent.Should().BeSameAs(parent);
        _ = context.History.UndoStack.Should().ContainSingle();
    }

    [TestMethod]
    public async Task DuplicateNodesInPlaceAsync_CopiesTakeTheGivenTransformsAndSourcesKeepTheirs()
    {
        var fixture = CreateFixture();
        ConfigureHierarchySync(fixture);
        var scene = CreateScene();
        var source = new SceneNode(scene) { Name = "Source" };
        var sourceTransform = source.Components.OfType<TransformComponent>().Single();
        sourceTransform.LocalPosition = new Vector3(1, 2, 3);
        scene.RootNodes.Add(source);
        var context = CreateContext(scene);
        var moved = new TransformData { Name = "Transform", Position = new Vector3(5, 0, 0), Rotation = Quaternion.Identity, Scale = new Vector3(2) };

        var result = await fixture.Sut.DuplicateNodesInPlaceAsync(context, [source.Id], new Dictionary<Guid, TransformData> { [source.Id] = moved }).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        var copy = result.Value!.Single().Components.OfType<TransformComponent>().Single();
        _ = copy.LocalPosition.Should().Be(new Vector3(5, 0, 0));
        _ = copy.LocalScale.Should().Be(new Vector3(2));
        _ = sourceTransform.LocalPosition.Should().Be(new Vector3(1, 2, 3));
    }

    [TestMethod]
    public async Task DuplicateNodesInPlaceAsync_CopiesANodeWithItsSelectedAncestorOnce()
    {
        var fixture = CreateFixture();
        ConfigureHierarchySync(fixture);
        var scene = CreateScene();
        var parent = new SceneNode(scene) { Name = "Parent" };
        var child = new SceneNode(scene) { Name = "Child" };
        scene.RootNodes.Add(parent);
        parent.AddChild(child);
        var context = CreateContext(scene);

        var result = await fixture.Sut.DuplicateNodesInPlaceAsync(context, [parent.Id, child.Id]).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = result.Value.Should().ContainSingle().Which.Children.Should().ContainSingle();
        _ = parent.Children.Should().ContainSingle();
    }

    [TestMethod]
    public async Task DuplicateNodesInPlaceAsync_KeepsTheCopyInItsSourcesFolder()
    {
        var fixture = CreateFixture();
        ConfigureHierarchySync(fixture);
        var scene = CreateScene();
        var source = new SceneNode(scene) { Name = "Source" };
        scene.RootNodes.Add(source);
        var folderId = Guid.NewGuid();
        scene.SetExplorerLayout(
        [
            new ExplorerEntryData
            {
                Type = "Folder",
                FolderId = folderId,
                Name = "Props",
                Children = [new ExplorerEntryData { Type = "Node", NodeId = source.Id }],
            },
        ]);
        var context = CreateContext(scene);

        var result = await fixture.Sut.DuplicateNodesInPlaceAsync(context, [source.Id]).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        var folder = scene.ExplorerLayout!.Single(entry => entry.FolderId == folderId);
        _ = folder.Children!.Select(static entry => entry.NodeId).Should().Contain(result.Value!.Single().Id);
    }
}
