// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Controls;
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
    public async Task ReconcileLayoutAsync_AttachesFolderAndMovesNode()
    {
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Node" };
        scene.RootNodes.Add(node);

        var folderId = Guid.NewGuid();
        var layout = new List<ExplorerEntryData>
        {
            new()
            {
                Type = "Folder",
                FolderId = folderId,
                Name = "Folder",
                Children = new List<ExplorerEntryData>
                {
                    new() { Type = "Node", NodeId = node.Id },
                },
            },
        };

        scene.ExplorerLayout = layout;
        var sceneAdapter = SceneAdapter.BuildLayoutTree(scene);
        var layoutContext = new TestLayoutContext();

        await this.organizer.ReconcileLayoutAsync(sceneAdapter, scene, layout, layoutContext, preserveNodeExpansion: true).ConfigureAwait(false);

        var children = await sceneAdapter.Children.ConfigureAwait(false);
        var folder = children.OfType<FolderAdapter>().FirstOrDefault();
        _ = folder.Should().NotBeNull("Folder entry should be attached to the scene adapter");
        _ = folder!.Id.Should().Be(folderId);

        _ = folder.ChildAdapters.OfType<SceneNodeAdapter>().Should()
            .ContainSingle(lna => ReferenceEquals(lna.AttachedObject, node), "Node should move under the new folder");

        _ = layoutContext.RefreshCount.Should().Be(1);
    }

    [TestMethod]
    public async Task ReconcileLayoutAsync_RoundTripsLayoutThroughUndoRedoShape()
    {
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Node" };
        scene.RootNodes.Add(node);

        var previousLayout = new List<ExplorerEntryData> { new() { Type = "Node", NodeId = node.Id } };
        scene.ExplorerLayout = previousLayout;
        var sceneAdapter = SceneAdapter.BuildLayoutTree(scene);

        var folderId = Guid.NewGuid();
        var newLayout = new List<ExplorerEntryData>
        {
            new()
            {
                Type = "Folder",
                FolderId = folderId,
                Name = "Folder",
                Children = new List<ExplorerEntryData> { new() { Type = "Node", NodeId = node.Id } },
            },
        };

        var layoutContext = new TestLayoutContext();

        await this.organizer.ReconcileLayoutAsync(sceneAdapter, scene, newLayout, layoutContext, preserveNodeExpansion: true).ConfigureAwait(false);
        _ = scene.ExplorerLayout.Should().BeEquivalentTo(newLayout, options => options.WithStrictOrdering());

        // Undo simulation: restore previous layout
        await this.organizer.ReconcileLayoutAsync(sceneAdapter, scene, previousLayout, layoutContext, preserveNodeExpansion: true).ConfigureAwait(false);
        _ = scene.ExplorerLayout.Should().BeEquivalentTo(previousLayout, options => options.WithStrictOrdering());

        // Redo simulation: apply folder layout again
        await this.organizer.ReconcileLayoutAsync(sceneAdapter, scene, newLayout, layoutContext, preserveNodeExpansion: true).ConfigureAwait(false);

        var children = await sceneAdapter.Children.ConfigureAwait(false);
        var folder = children.OfType<FolderAdapter>().Single();
        var folderChildren = await folder.Children.ConfigureAwait(false);
        _ = folderChildren.OfType<SceneNodeAdapter>().Should().ContainSingle(lna => ReferenceEquals(lna.AttachedObject, node));
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

    [TestMethod]
    public void CloneLayout_DeepCopiesStructure()
    {
        var layout = new List<ExplorerEntryData>
        {
            new()
            {
                Type = "Folder",
                FolderId = Guid.NewGuid(),
                Name = "Folder",
                Children = new List<ExplorerEntryData>
                {
                    new() { Type = "Node", NodeId = Guid.NewGuid() }
                }
            }
        };

        var clone = this.organizer.CloneLayout(layout);

        _ = clone.Should().NotBeSameAs(layout);
        _ = clone.Should().BeEquivalentTo(layout);
        _ = clone![0].Children.Should().NotBeSameAs(layout[0].Children);
    }

    [TestMethod]
    public void GetExpandedFolderIds_ReturnsExpandedFoldersRecursively()
    {
        var f1 = Guid.NewGuid();
        var f2 = Guid.NewGuid();
        var f3 = Guid.NewGuid();

        var layout = new List<ExplorerEntryData>
        {
            new()
            {
                Type = "Folder",
                FolderId = f1,
                IsExpanded = true,
                Children = new List<ExplorerEntryData>
                {
                    new()
                    {
                        Type = "Folder",
                        FolderId = f2,
                        IsExpanded = false,
                        Children = new List<ExplorerEntryData>
                        {
                            new() { Type = "Folder", FolderId = f3, IsExpanded = true }
                        }
                    }
                }
            }
        };

        var expanded = this.organizer.GetExpandedFolderIds(layout);

        _ = expanded.Should().Contain(f1);
        _ = expanded.Should().NotContain(f2);
        _ = expanded.Should().Contain(f3);
    }

    [TestMethod]
    public void BuildFolderOnlyLayout_ConstructsLayoutWithOnlyFolders()
    {
        var f1 = Guid.NewGuid();
        var f2 = Guid.NewGuid();
        var n1 = Guid.NewGuid();

        var layout = new List<ExplorerEntryData>
        {
            new()
            {
                Type = "Folder",
                FolderId = f1,
                Children = new List<ExplorerEntryData>
                {
                    new() { Type = "Node", NodeId = n1 },
                    new() { Type = "Folder", FolderId = f2 }
                }
            },
            new() { Type = "Node", NodeId = Guid.NewGuid() }
        };

        var change = new LayoutChangeRecord("Test", null, layout, layout[0]);
        var folderOnly = this.organizer.BuildFolderOnlyLayout(change);

        _ = folderOnly.Should().HaveCount(1);
        _ = folderOnly[0].FolderId.Should().Be(f1);
        _ = folderOnly[0].Children.Should().BeEmpty();
    }

    [TestMethod]
    public void FilterTopLevelSelectedNodeIds_ReturnsOnlyTopLevelNodesInSelection()
    {
        var scene = CreateScene();
        var parent = new SceneNode(scene) { Name = "Parent" };
        var child = new SceneNode(scene) { Name = "Child" };
        parent.AddChild(child);
        scene.RootNodes.Add(parent);

        var selection = new HashSet<Guid> { parent.Id, child.Id };
        var filtered = this.organizer.FilterTopLevelSelectedNodeIds(selection, scene);

        _ = filtered.Should().Contain(parent.Id);
        _ = filtered.Should().NotContain(child.Id);
    }

    [TestMethod]
    public async Task ReconcileLayoutAsync_ReusesAdaptersAndRefreshesOnce()
    {
        var scene = CreateScene();
        var nodeA = new SceneNode(scene) { Name = "A" };
        var nodeB = new SceneNode(scene) { Name = "B" };
        scene.RootNodes.Add(nodeA);
        scene.RootNodes.Add(nodeB);

        var initialLayout = new List<ExplorerEntryData>
        {
            new() { Type = "Node", NodeId = nodeA.Id },
            new()
            {
                Type = "Folder",
                FolderId = Guid.NewGuid(),
                Name = "Folder",
                Children = new List<ExplorerEntryData> { new() { Type = "Node", NodeId = nodeB.Id } },
            },
        };

        scene.ExplorerLayout = initialLayout;
        var sceneAdapter = SceneAdapter.BuildLayoutTree(scene);
        var initialChildren = await sceneAdapter.Children.ConfigureAwait(false);

        var adapterA = initialChildren.OfType<SceneNodeAdapter>().First(a => a.AttachedObject == nodeA);
        var folder = initialChildren.OfType<FolderAdapter>().Single();
        var adapterB = folder.ChildAdapters.OfType<SceneNodeAdapter>().Single();

        var newFolderId = Guid.NewGuid();
        var newLayout = new List<ExplorerEntryData>
        {
            new() { Type = "Node", NodeId = nodeB.Id },
            new()
            {
                Type = "Folder",
                FolderId = newFolderId,
                Name = "NewFolder",
                Children = new List<ExplorerEntryData> { new() { Type = "Node", NodeId = nodeA.Id } },
            },
        };

        var context = new TestLayoutContext();

        await this.organizer.ReconcileLayoutAsync(sceneAdapter, scene, newLayout, context, preserveNodeExpansion: true)
            .ConfigureAwait(false);

        var reconciledChildren = await sceneAdapter.Children.ConfigureAwait(false);

        _ = reconciledChildren.Should().HaveCount(2);
        _ = reconciledChildren[0].Should().BeSameAs(adapterB, "node B adapter should be reused at root");

        var newFolder = reconciledChildren[1] as FolderAdapter;
        _ = newFolder.Should().NotBeNull();
        _ = newFolder!.Id.Should().Be(newFolderId);
        _ = newFolder.ChildAdapters.Should().HaveCount(1);
        _ = newFolder.ChildAdapters[0].Should().BeSameAs(adapterA, "node A adapter should be reused under new folder");

        _ = scene.ExplorerLayout.Should().NotBeSameAs(newLayout, "layout should be cloned");
        _ = scene.ExplorerLayout.Should().BeEquivalentTo(newLayout, options => options.WithStrictOrdering());
        _ = context.RefreshCount.Should().Be(1, "reconcile should request a single refresh");
    }

    private sealed class TestLayoutContext : ILayoutContext
    {
        public int RefreshCount { get; private set; }

        public int? GetShownIndex(ITreeItem item)
        {
            _ = item;
            return null;
        }

        public bool TryRemoveShownItem(ITreeItem item)
        {
            _ = item;
            return false;
        }

        public void InsertShownItem(int index, ITreeItem item)
        {
            _ = index;
            _ = item;
        }

        public Task RefreshTreeAsync(SceneAdapter sceneAdapter)
        {
            _ = sceneAdapter;
            ++this.RefreshCount;
            return Task.CompletedTask;
        }

        public bool TryGetVisibleSpan(ITreeItem root, out int startIndex, out int count)
        {
            _ = root;
            startIndex = 0;
            count = 0;
            return false;
        }

        public void ApplyShownDelta(ShownItemsDelta delta)
        {
            _ = delta;
        }
    }
}
