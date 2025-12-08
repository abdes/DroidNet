// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Moq;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.WorldEditor.SceneExplorer.Operations;

namespace Oxygen.Editor.WorldEditor.SceneExplorer.Tests;

[TestClass]
public class SceneOrganizerTests
{
    private readonly SceneOrganizer organizer = new(NullLogger<SceneOrganizer>.Instance);

    [TestMethod]
    public void CreateFolderFromSelection_MovesEntriesIntoNewFolder()
    {
        var scene = CreateScene();
        var sceneAdapter = new SceneAdapter(scene);
        var node1 = new SceneNode(scene) { Name = "Node1" };
        var node2 = new SceneNode(scene) { Name = "Node2" };
        scene.RootNodes.Add(node1);
        scene.RootNodes.Add(node2);
        scene.ExplorerLayout =
        [
            new ExplorerEntryData { Type = "Node", NodeId = node1.Id },
            new ExplorerEntryData { Type = "Node", NodeId = node2.Id },
        ];

        var change = this.organizer.CreateFolderFromSelection(new HashSet<Guid> { node1.Id }, scene, sceneAdapter);

        _ = change.OperationName.Should().Be("CreateFolderFromSelection");
        _ = change.NewFolder.Should().NotBeNull();
        _ = change.NewLayout.Should().HaveCount(2);
        _ = scene.ExplorerLayout.Should().BeSameAs(change.NewLayout, "organizer must update scene layout reference");
        var folderEntry = change.NewLayout.First();
        _ = folderEntry.Type.Should().Be("Folder");
        _ = folderEntry.Children.Should().NotBeNull();
        _ = folderEntry.Children!.Should().HaveCount(1);
        _ = folderEntry.Children!.First().NodeId.Should().Be(node1.Id);
        _ = scene.RootNodes.Any(n => n.Parent is not null).Should().BeFalse("Organizer must not alter scene graph");
        _ = change.NewLayout.Any(e => string.Equals(e.Type, "Node", StringComparison.Ordinal) && e.NodeId == node2.Id).Should().BeTrue();
    }

    [TestMethod]
    public void CreateFolderFromSelection_WhenSelectionEmptyThrows()
    {
        var scene = CreateScene();
        var adapter = new SceneAdapter(scene);

        Action act = () => _ = this.organizer.CreateFolderFromSelection(new HashSet<Guid>(), scene, adapter);

        _ = act.Should().Throw<InvalidOperationException>().WithMessage("Cannot create folder with empty selection.");
    }

    [TestMethod]
    public void MoveNodeToFolder_RehomesNodeEntry()
    {
        var scene = CreateScene();
        var folderId = Guid.NewGuid();
        var node = new SceneNode(scene) { Name = "Node" };
        scene.RootNodes.Add(node);
        scene.ExplorerLayout =
        [
            new ExplorerEntryData { Type = "Folder", FolderId = folderId, Name = "Folder", Children = new List<ExplorerEntryData>() },
            new ExplorerEntryData { Type = "Node", NodeId = node.Id },
        ];

        var change = this.organizer.MoveNodeToFolder(node.Id, folderId, scene);

        _ = scene.ExplorerLayout.Should().BeSameAs(change.NewLayout, "organizer must update scene layout reference");
        var folder = change.NewLayout.First(e => e.FolderId == folderId);
        _ = folder.Children.Should().NotBeNull();
        _ = folder.Children!.Should().HaveCount(1);
        _ = folder.Children![0].NodeId.Should().Be(node.Id);
        _ = change.NewLayout.Any(e => string.Equals(e.Type, "Node", StringComparison.OrdinalIgnoreCase) && e.NodeId == node.Id && e.FolderId is null).Should().BeFalse();
    }

    [TestMethod]
    public void MoveNodeToFolder_DoesNotChangeSceneHierarchy()
    {
        var scene = CreateScene();
        var folderId = Guid.NewGuid();
        var parent = new SceneNode(scene) { Name = "Parent" };
        var child = new SceneNode(scene) { Name = "Child" };
        parent.AddChild(child);
        scene.RootNodes.Add(parent);

        // Layout lists child at root; moving to a root folder must not alter scene parenting.
        scene.ExplorerLayout =
        [
            new ExplorerEntryData { Type = "Node", NodeId = child.Id },
            new ExplorerEntryData { Type = "Folder", FolderId = folderId, Name = "Target", Children = new List<ExplorerEntryData>() },
        ];

        var change = this.organizer.MoveNodeToFolder(child.Id, folderId, scene);

        _ = scene.ExplorerLayout.Should().BeSameAs(change.NewLayout, "organizer must update scene layout reference");
        var folder = change.NewLayout.First(e => e.FolderId == folderId);
        _ = folder.Children.Should().NotBeNull();
        _ = folder.Children!.Single().NodeId.Should().Be(child.Id);

        _ = child.Parent.Should().Be(parent, "layout-only move must not reparent scene graph");
        _ = scene.RootNodes.Should().Contain(parent);
        _ = scene.RootNodes.Should().NotContain(child);
    }

    [TestMethod]
    public void MoveNodeToFolder_RejectsCrossLineageFolder()
    {
        var scene = CreateScene();
        var lineageFolderId = Guid.NewGuid();
        var otherFolderId = Guid.NewGuid();
        var node = new SceneNode(scene) { Name = "Child" };
        scene.RootNodes.Add(node);

        scene.ExplorerLayout =
        [
            new ExplorerEntryData
            {
                Type = "Folder",
                FolderId = lineageFolderId,
                Name = "ParentFolder",
                Children =
                [
                    new ExplorerEntryData { Type = "Node", NodeId = node.Id },
                ],
            },
            new ExplorerEntryData { Type = "Folder", FolderId = otherFolderId, Name = "Other", Children = new List<ExplorerEntryData>() },
        ];

        var originalLayout = scene.ExplorerLayout;

        Action act = () => _ = this.organizer.MoveNodeToFolder(node.Id, otherFolderId, scene);

        _ = act.Should().Throw<InvalidOperationException>().WithMessage("Cannot move node to a folder outside its lineage.");
        _ = scene.ExplorerLayout.Should().BeEquivalentTo(originalLayout, options => options.WithStrictOrdering(), "failed move must not mutate layout contents");
    }

    [TestMethod]
    public void MoveFolderToParent_MovesToRootWhenParentIsNull()
    {
        var scene = CreateScene();
        var parentId = Guid.NewGuid();
        var childId = Guid.NewGuid();
        scene.ExplorerLayout =
        [
            new ExplorerEntryData
            {
                Type = "Folder",
                FolderId = parentId,
                Name = "Parent",
                Children =
                [
                    new ExplorerEntryData { Type = "Folder", FolderId = childId, Name = "Child", Children = new List<ExplorerEntryData>() },
                ],
            },
        ];

        var change = this.organizer.MoveFolderToParent(childId, newParentFolderId: null, scene);

        _ = scene.ExplorerLayout.Should().BeSameAs(change.NewLayout, "organizer must update scene layout reference");
        _ = change.NewLayout.Should().HaveCount(2);
        _ = change.NewLayout.Any(e => e.FolderId == childId).Should().BeTrue();
        var parentFolder = change.NewLayout.First(e => e.FolderId == parentId);
        _ = (parentFolder.Children is null || parentFolder.Children.All(c => c.FolderId != childId)).Should().BeTrue();
    }

    [TestMethod]
    public void CreateFolderFromSelection_PlacesFolderUnderHighestCommonAncestor()
    {
        var scene = CreateScene();
        var parentFolderId = Guid.NewGuid();
        var node1 = new SceneNode(scene) { Name = "Node1" };
        var node2 = new SceneNode(scene) { Name = "Node2" };
        scene.RootNodes.Add(node1);
        scene.RootNodes.Add(node2);
        scene.ExplorerLayout =
        [
            new ExplorerEntryData
            {
                Type = "Folder",
                FolderId = parentFolderId,
                Name = "Parent",
                Children =
                [
                    new ExplorerEntryData { Type = "Node", NodeId = node1.Id },
                    new ExplorerEntryData { Type = "Node", NodeId = node2.Id },
                ],
            },
        ];

        var change = this.organizer.CreateFolderFromSelection(new HashSet<Guid> { node1.Id, node2.Id }, scene, new SceneAdapter(scene));

        _ = scene.ExplorerLayout.Should().BeSameAs(change.NewLayout, "organizer must update scene layout reference");
        var parentFolder = change.NewLayout.First(e => e.FolderId == parentFolderId);
        _ = parentFolder.Children.Should().NotBeNull();
        _ = parentFolder.Children!.Count.Should().Be(1, "new folder should be inserted under the HCA parent");
        var newFolder = parentFolder.Children!.First();
        _ = newFolder.Type.Should().Be("Folder");
        _ = newFolder.Children.Should().NotBeNull();
        _ = newFolder.Children!.Select(c => c.NodeId).Should().BeEquivalentTo(new[] { node1.Id, node2.Id });
    }

    [TestMethod]
    public void CreateFolderFromSelection_DropsDescendantSelectionAndKeepsRootEntry()
    {
        var scene = CreateScene();
        var parent = new SceneNode(scene) { Name = "Parent" };
        var child = new SceneNode(scene) { Name = "Child" };
        parent.AddChild(child);
        scene.RootNodes.Add(parent);

        scene.ExplorerLayout =
        [
            new ExplorerEntryData { Type = "Node", NodeId = parent.Id },
            new ExplorerEntryData { Type = "Node", NodeId = child.Id },
        ];

        var change = this.organizer.CreateFolderFromSelection(new HashSet<Guid> { parent.Id, child.Id }, scene, new SceneAdapter(scene));

        _ = scene.ExplorerLayout.Should().BeSameAs(change.NewLayout, "organizer must update scene layout reference");
        var folder = change.NewLayout.First();
        _ = folder.Type.Should().Be("Folder");
        _ = folder.Children.Should().NotBeNull();
        _ = folder.Children!.Select(c => c.NodeId).Should().Equal([parent.Id]);

        _ = change.NewLayout.Should().HaveCount(2);
        var remainingRootEntry = change.NewLayout[1];
        _ = remainingRootEntry.NodeId.Should().Be(child.Id);
        _ = change.PreviousLayout.Should().HaveCount(2);
        _ = change.PreviousLayout.Any(e => e.NodeId == parent.Id && e.FolderId is null).Should().BeTrue("previous layout retained for undo");
    }

    [TestMethod]
    public void RemoveFolder_PromotesChildren()
    {
        var scene = CreateScene();
        var folderId = Guid.NewGuid();
        var node = new SceneNode(scene) { Name = "Node" };
        scene.RootNodes.Add(node);
        scene.ExplorerLayout =
        [
            new ExplorerEntryData
            {
                Type = "Folder",
                FolderId = folderId,
                Name = "Folder",
                Children =
                [
                    new ExplorerEntryData { Type = "Node", NodeId = node.Id },
                ],
            },
        ];

        var change = this.organizer.RemoveFolder(folderId, promoteChildrenToParent: true, scene);

        _ = scene.ExplorerLayout.Should().BeSameAs(change.NewLayout, "organizer must update scene layout reference");
        _ = change.NewLayout.Should().HaveCount(1);
        var entry = change.NewLayout.First();
        _ = entry.Type.Should().Be("Node");
        _ = entry.NodeId.Should().Be(node.Id);
    }

    [TestMethod]
    public void RemoveFolder_DropsChildrenWhenNotPromoting()
    {
        var scene = CreateScene();
        var folderId = Guid.NewGuid();
        var node = new SceneNode(scene) { Name = "Node" };
        scene.RootNodes.Add(node);
        scene.ExplorerLayout =
        [
            new ExplorerEntryData
            {
                Type = "Folder",
                FolderId = folderId,
                Name = "Folder",
                Children =
                [
                    new ExplorerEntryData { Type = "Node", NodeId = node.Id },
                ],
            },
        ];

        var change = this.organizer.RemoveFolder(folderId, promoteChildrenToParent: false, scene);

        _ = scene.ExplorerLayout.Should().BeSameAs(change.NewLayout, "organizer must update scene layout reference");
        _ = change.NewLayout.Should().BeEmpty();
    }

    [TestMethod]
    public void CreateFolderFromSelection_InitializesLayoutWhenMissing()
    {
        var scene = CreateScene();
        var sceneAdapter = new SceneAdapter(scene);
        var node = new SceneNode(scene) { Name = "Node" };
        scene.RootNodes.Add(node);
        scene.ExplorerLayout = null; // simulate missing layout

        var change = this.organizer.CreateFolderFromSelection(new HashSet<Guid> { node.Id }, scene, sceneAdapter);

        _ = scene.ExplorerLayout.Should().BeSameAs(change.NewLayout, "organizer must populate scene layout");
        _ = change.NewLayout.Should().NotBeNull();
        _ = change.NewLayout.Should().HaveCount(1);
        var folder = change.NewLayout[0];
        _ = folder.Type.Should().Be("Folder");
        _ = folder.Children.Should().NotBeNull();
        _ = folder.Children!.Single().NodeId.Should().Be(node.Id);
    }

    [TestMethod]
    public void RemoveNodeFromFolder_RemovesEntryFromLayout()
    {
        var scene = CreateScene();
        var folderId = Guid.NewGuid();
        var node = new SceneNode(scene) { Name = "Node" };
        scene.RootNodes.Add(node);
        scene.ExplorerLayout =
        [
            new ExplorerEntryData
            {
                Type = "Folder",
                FolderId = folderId,
                Name = "Folder",
                Children = new List<ExplorerEntryData> { new() { Type = "Node", NodeId = node.Id } }
            }
        ];

        var change = this.organizer.RemoveNodeFromFolder(node.Id, folderId, scene);

        _ = scene.ExplorerLayout.Should().BeSameAs(change.NewLayout);
        var folder = change.NewLayout.First(e => e.FolderId == folderId);
        _ = folder.Children.Should().BeEmpty();
        _ = change.ModifiedFolders.Should().Contain(folder);
    }

    private static Scene CreateScene()
    {
        var project = new Mock<IProject>().Object;
        return new Scene(project) { Name = "TestScene" };
    }
}
