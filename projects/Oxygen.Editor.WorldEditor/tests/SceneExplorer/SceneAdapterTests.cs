// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Generic;
using System.ComponentModel;
using AwesomeAssertions;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Moq;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.WorldEditor.SceneExplorer.Tests;

[TestClass]
public class SceneAdapterTests
{
    [TestMethod]
    public async Task LoadChildren_UsesExplorerLayoutAndAddsMissingRoots()
    {
        var project = new Mock<IProject>().Object;
        var scene = new Scene(project) { Name = "Scene" };
        var nodeInFolder = new SceneNode(scene) { Name = "InFolder" };
        var missingRoot = new SceneNode(scene) { Name = "Missing" };
        scene.RootNodes.Add(nodeInFolder);
        scene.RootNodes.Add(missingRoot);

        scene.ExplorerLayout = new List<ExplorerEntryData>
        {
            new()
            {
                Type = "Folder",
                Name = "Group",
                Children = new List<ExplorerEntryData>
                {
                    new() { Type = "Node", NodeId = nodeInFolder.Id },
                },
            },
        };

        var adapter = new SceneAdapter(scene);
        var children = await adapter.Children.ConfigureAwait(false);

        _ = children.Count.Should().Be(2);

        var folder = children[0] as FolderAdapter;
        _ = folder.Should().NotBeNull();
        _ = folder!.Label.Should().Be("Group");
        _ = folder.ChildAdapters.Count.Should().Be(1);

        var folderChild = folder.ChildAdapters[0] as SceneNodeAdapter;
        _ = folderChild.Should().NotBeNull();
        _ = folderChild!.AttachedObject.Should().Be(nodeInFolder);

        var fallback = children[1] as SceneNodeAdapter;
        _ = fallback.Should().NotBeNull();
        _ = fallback!.AttachedObject.Should().Be(missingRoot);
        _ = fallback.Parent.Should().Be(adapter);
    }

    [TestMethod]
    public async Task AddChildAdapter_WhenExpandedAddsToChildrenCollection()
    {
        var folder = new FolderAdapter(Guid.NewGuid(), "Folder") { IsExpanded = true };
        var scene = new Scene(new Mock<IProject>().Object) { Name = "Scene" };
        var node = new SceneNode(scene) { Name = "Node" };
        var childAdapter = new SceneNodeAdapter(node);

        folder.AddChildAdapter(childAdapter);

        var children = await folder.Children.ConfigureAwait(false);

        _ = folder.ChildAdapters.Count.Should().Be(1);
        _ = folder.ChildAdapters[0].Should().Be(childAdapter);
        _ = children.Count.Should().Be(1);
        _ = children[0].Should().Be(childAdapter);
        _ = childAdapter.Parent.Should().Be(folder);
        _ = folder.IconGlyph.Should().Be("\uE838");
    }

    [TestMethod]
    public async Task LoadChildren_WhenLayoutMissingDefaultsToRootNodesOnly()
    {
        var project = new Mock<IProject>().Object;
        var scene = new Scene(project) { Name = "Scene" };
        var root1 = new SceneNode(scene) { Name = "Root1" };
        var root2 = new SceneNode(scene) { Name = "Root2" };
        scene.RootNodes.Add(root1);
        scene.RootNodes.Add(root2);

        var adapter = new SceneAdapter(scene);
        var children = await adapter.Children.ConfigureAwait(false);

        _ = children.Count.Should().Be(2);
        _ = (children[0] as SceneNodeAdapter).Should().NotBeNull();
        _ = ((SceneNodeAdapter)children[0]).AttachedObject.Should().Be(root1);
        _ = (children[1] as SceneNodeAdapter).Should().NotBeNull();
        _ = ((SceneNodeAdapter)children[1]).AttachedObject.Should().Be(root2);
    }

    [TestMethod]
    public async Task RemoveChildAdapter_WhenExpandedRemovesFromBothCollections()
    {
        var folder = new FolderAdapter(Guid.NewGuid(), "Folder") { IsExpanded = true };
        var scene = new Scene(new Mock<IProject>().Object) { Name = "Scene" };
        var node = new SceneNode(scene) { Name = "Node" };
        var childAdapter = new SceneNodeAdapter(node);

        folder.AddChildAdapter(childAdapter);
        var children = await folder.Children.ConfigureAwait(false);
        _ = children.Count.Should().Be(1);

        var removed = folder.RemoveChildAdapter(childAdapter);

        _ = removed.Should().BeTrue();
        _ = folder.ChildAdapters.Count.Should().Be(0);
        _ = children.Count.Should().Be(0);
        _ = childAdapter.Parent.Should().BeNull();
    }

    [TestMethod]
    public void Label_WhenChangedUpdatesSceneNodeNameAndNotifies()
    {
        var scene = new Scene(new Mock<IProject>().Object) { Name = "Scene" };
        var node = new SceneNode(scene) { Name = "Original" };
        var adapter = new SceneNodeAdapter(node);

        string? lastProperty = null;
        adapter.PropertyChanged += (_, args) => lastProperty = args.PropertyName;

        adapter.Label = "Updated";

        _ = node.Name.Should().Be("Updated");
        _ = adapter.Label.Should().Be("Updated");
        _ = lastProperty.Should().Be(nameof(adapter.Label));

        lastProperty = null;
        node.Name = "FromNode";

        _ = lastProperty.Should().Be(nameof(adapter.Label));
        _ = adapter.Label.Should().Be("FromNode");
    }

    [TestMethod]
    public async Task BuildLayoutTree_ProducesFolderAndLayoutNodeAdapters()
    {
        var project = new Mock<IProject>().Object;
        var scene = new Scene(project) { Name = "Scene" };
        var nodeInFolder = new SceneNode(scene) { Name = "InFolder" };
        var nodeAtRoot = new SceneNode(scene) { Name = "InLayout" };
        var missingRoot = new SceneNode(scene) { Name = "Missing" };
        scene.RootNodes.Add(nodeInFolder);
        scene.RootNodes.Add(nodeAtRoot);
        scene.RootNodes.Add(missingRoot);

        scene.ExplorerLayout =
        [
            new ExplorerEntryData
            {
                Type = "Folder",
                Name = "Group",
                Children = new List<ExplorerEntryData>
                {
                    new() { Type = "Node", NodeId = nodeInFolder.Id },
                },
            },
            new ExplorerEntryData { Type = "Node", NodeId = nodeAtRoot.Id },
        ];

        var adapter = SceneAdapter.BuildLayoutTree(scene);

        var children = await adapter.Children.ConfigureAwait(false);

        _ = children.Count.Should().Be(3);

        var folder = children[0] as FolderAdapter;
        _ = folder.Should().NotBeNull();
        _ = folder!.Label.Should().Be("Group");
        _ = folder.ChildAdapters.Count.Should().Be(1);
        _ = folder.ChildAdapters[0].Should().BeOfType<LayoutNodeAdapter>();
        _ = ((LayoutNodeAdapter)folder.ChildAdapters[0]).AttachedObject.AttachedObject.Should().Be(nodeInFolder);

        _ = children[1].Should().BeOfType<LayoutNodeAdapter>();
        _ = ((LayoutNodeAdapter)children[1]).AttachedObject.AttachedObject.Should().Be(nodeAtRoot);

        _ = children[2].Should().BeOfType<LayoutNodeAdapter>();
        _ = ((LayoutNodeAdapter)children[2]).AttachedObject.AttachedObject.Should().Be(missingRoot);
    }

    [TestMethod]
    public async Task SceneNodeAdapter_LoadsChildrenFromSceneGraph()
    {
        var scene = new Scene(new Mock<IProject>().Object) { Name = "Scene" };
        var parent = new SceneNode(scene) { Name = "Parent" };
        var child = new SceneNode(scene) { Name = "Child" };
        parent.AddChild(child);

        var adapter = new SceneNodeAdapter(parent);
        var children = await adapter.Children.ConfigureAwait(false);

        _ = children.Count.Should().Be(1);
        _ = (children[0] as SceneNodeAdapter).Should().NotBeNull();
        _ = ((SceneNodeAdapter)children[0]).AttachedObject.Should().Be(child);
    }

    [TestMethod]
    public async Task LayoutNodeAdapter_LoadsChildrenFromWrappedSceneNode()
    {
        var scene = new Scene(new Mock<IProject>().Object) { Name = "Scene" };
        var parent = new SceneNode(scene) { Name = "Parent" };
        var child = new SceneNode(scene) { Name = "Child" };
        parent.AddChild(child);

        var sceneNodeAdapter = new SceneNodeAdapter(parent);
        var layoutNodeAdapter = new LayoutNodeAdapter(sceneNodeAdapter);

        var children = await layoutNodeAdapter.Children.ConfigureAwait(false);

        _ = children.Count.Should().Be(1);
        _ = (children[0] as LayoutNodeAdapter).Should().NotBeNull();
        _ = ((LayoutNodeAdapter)children[0]).AttachedObject.AttachedObject.Should().Be(child);
    }
}
