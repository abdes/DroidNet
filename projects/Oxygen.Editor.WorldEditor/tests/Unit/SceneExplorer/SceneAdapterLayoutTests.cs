// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Editor.World.SceneExplorer;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.SceneExplorer;

/// <summary>Ensures layout realization keeps one adapter per scene node and preserves layout order.</summary>
[TestClass]
public sealed class SceneAdapterLayoutTests
{
    [TestMethod]
    public async Task Children_LayoutReferencingSameNodeTwice_RealizesSingleAdapter()
    {
        var scene = CreateScene();
        var parent = new SceneNode(scene) { Name = "Parent" };
        var child = new SceneNode(scene) { Name = "Child" };
        scene.RootNodes.Add(parent);
        parent.AddChild(child);
        scene.SetExplorerLayout(
            [
                new ExplorerEntryData { Type = "Node", NodeId = parent.Id },
                new ExplorerEntryData
                {
                    Type = "Node",
                    NodeId = parent.Id,
                    Children = [new ExplorerEntryData { Type = "Node", NodeId = child.Id }],
                },
            ]);
        var adapter = SceneAdapter.BuildLayoutTree(scene);

        var children = await adapter.Children.ConfigureAwait(false);

        _ = children.Should().ContainSingle("a duplicate layout entry must not realize a second adapter for the same node");
        var parentAdapter = children[0].Should().BeOfType<SceneNodeAdapter>().Which;
        _ = parentAdapter.AttachedObject.Should().BeSameAs(parent);
        _ = parentAdapter.CurrentChildren.Should().ContainSingle("the unseated scene child is realized under its scene parent");
        _ = parentAdapter.CurrentChildren.Single().Should().BeOfType<SceneNodeAdapter>().Which.AttachedObject.Should().BeSameAs(child);
        _ = adapter.RootItems.Should().ContainSingle();
    }

    [TestMethod]
    public async Task Children_LayoutReferencingSameFolderTwice_RealizesSingleAdapter()
    {
        var scene = CreateScene();
        var folderId = Guid.NewGuid();
        scene.SetExplorerLayout(
            [
                new ExplorerEntryData { Type = "Folder", FolderId = folderId, Name = "First" },
                new ExplorerEntryData
                {
                    Type = "Folder",
                    FolderId = folderId,
                    Name = "Duplicate",
                    Children = [new ExplorerEntryData { Type = "Folder", FolderId = Guid.NewGuid(), Name = "Nested" }],
                },
            ]);
        var adapter = SceneAdapter.BuildLayoutTree(scene);

        var children = await adapter.Children.ConfigureAwait(false);

        _ = children.Should().ContainSingle("a duplicate layout entry must not realize a second adapter sharing one folder id");
        var folder = children[0].Should().BeOfType<FolderAdapter>().Which;
        _ = folder.Id.Should().Be(folderId);
        _ = folder.Name.Should().Be("First");
        _ = folder.CurrentChildren.Should().BeEmpty("the skipped duplicate must not recurse into its layout children");
        _ = adapter.RootItems.Should().ContainSingle();
    }

    [TestMethod]
    public async Task Children_NestedFoldersAndUnknownNodeId_RealizeInLayoutOrder()
    {
        var scene = CreateScene();
        var nodeA = new SceneNode(scene) { Name = "A" };
        var nodeB = new SceneNode(scene) { Name = "B" };
        var nodeC = new SceneNode(scene) { Name = "C" };
        scene.RootNodes.Add(nodeA);
        scene.RootNodes.Add(nodeC);
        nodeA.AddChild(nodeB);
        var folderId = Guid.NewGuid();
        var nestedFolderId = Guid.NewGuid();
        scene.SetExplorerLayout(
            [
                new ExplorerEntryData
                {
                    Type = "Folder",
                    FolderId = folderId,
                    Name = "Group",
                    Children =
                    [
                        new ExplorerEntryData
                        {
                            Type = "Folder",
                            FolderId = nestedFolderId,
                            Name = "Nested",
                            Children = [new ExplorerEntryData { Type = "Node", NodeId = nodeB.Id }],
                        },
                        new ExplorerEntryData { Type = "Node", NodeId = nodeA.Id },
                    ],
                },
                new ExplorerEntryData { Type = "Node", NodeId = Guid.NewGuid() },
                new ExplorerEntryData { Type = "Node", NodeId = nodeC.Id },
            ]);
        var adapter = SceneAdapter.BuildLayoutTree(scene);

        var children = await adapter.Children.ConfigureAwait(false);

        _ = children.Should().HaveCount(2, "the layout entry without a matching scene node must be skipped without throwing");
        var folder = children[0].Should().BeOfType<FolderAdapter>().Which;
        _ = folder.Id.Should().Be(folderId);
        _ = folder.CurrentChildren.Should().HaveCount(2);
        var nested = folder.CurrentChildren.ElementAt(0).Should().BeOfType<FolderAdapter>().Which;
        _ = nested.Id.Should().Be(nestedFolderId);
        _ = nested.CurrentChildren.Should().ContainSingle().Which.Should().BeOfType<SceneNodeAdapter>().Which.AttachedObject.Should().BeSameAs(nodeB);
        _ = folder.CurrentChildren.ElementAt(1).Should().BeOfType<SceneNodeAdapter>().Which.AttachedObject.Should().BeSameAs(nodeA);
        _ = children[1].Should().BeOfType<SceneNodeAdapter>().Which.AttachedObject.Should().BeSameAs(nodeC);
    }

    [TestMethod]
    public async Task Children_UnseatedDescendantOfSeatedParent_IsRealizedWithoutMutatingLayout()
    {
        var scene = CreateScene();
        var parent = new SceneNode(scene) { Name = "Parent" };
        var child = new SceneNode(scene) { Name = "Child" };
        scene.RootNodes.Add(parent);
        parent.AddChild(child);
        var parentEntry = new ExplorerEntryData { Type = "Node", NodeId = parent.Id };
        scene.SetExplorerLayout([parentEntry]);
        var adapter = SceneAdapter.BuildLayoutTree(scene);

        var children = await adapter.Children.ConfigureAwait(false);

        var parentAdapter = children.Single().Should().BeOfType<SceneNodeAdapter>().Which;
        _ = parentAdapter.CurrentChildren.Single().Should().BeOfType<SceneNodeAdapter>().Which.AttachedObject.Should().BeSameAs(child);
        _ = parentEntry.Children.Should().BeNull("projection fallback is not an authored layout mutation");
    }

    [TestMethod]
    public async Task Children_UnseatedParentWithSeatedChild_DoesNotDuplicateChild()
    {
        var scene = CreateScene();
        var parent = new SceneNode(scene) { Name = "Parent" };
        var child = new SceneNode(scene) { Name = "Child" };
        scene.RootNodes.Add(parent);
        parent.AddChild(child);
        scene.SetExplorerLayout([new ExplorerEntryData { Type = "Node", NodeId = child.Id }]);
        var adapter = SceneAdapter.BuildLayoutTree(scene);

        _ = await adapter.Children.ConfigureAwait(false);

        var projection = new SceneExplorerProjection();
        projection.Rebuild(adapter);
        _ = projection.Nodes.Should().HaveCount(2);
        _ = projection.GetNode(child.Id).Should().NotBeNull();
        _ = projection.GetNode(parent.Id)!.CurrentChildren.Should().BeEmpty("the child keeps its existing layout seat");
        projection.Clear();
    }

    private static Scene CreateScene()
    {
        var project = new Project(new ProjectInfo("Layout", Category.Games, "H:/LayoutTreeTests", "preview.png")) { Name = "Layout" };
        var scene = new Scene(project) { Name = "Scene" };
        project.Scenes.Add(scene);
        return scene;
    }
}
