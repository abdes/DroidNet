// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Moq;
using Oxygen.Editor.World;
using Oxygen.Editor.World.SceneExplorer.Operations;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.SceneExplorer;

/// <summary>Ensures layout overlay operations lazily initialize the layout on a freshly loaded scene.</summary>
[TestClass]
public class SceneOrganizerTests
{
    private readonly SceneOrganizer organizer = new(NullLogger<SceneOrganizer>.Instance);

    [TestMethod]
    public void CreateFolder_OnLayoutlessScene_InitializesLayoutAndCreatesFolder()
    {
        var scene = CreateScene();
        scene.RootNodes.Add(new SceneNode(scene) { Name = "Node" });

        var change = this.organizer.CreateFolder(parentFolderId: null, "Folder", scene);

        _ = scene.ExplorerLayout.Should().NotBeNull();
        _ = change.NewFolder.Should().NotBeNull();
        _ = change.NewFolder!.Name.Should().Be("Folder");
        _ = scene.ExplorerLayout.Should().Contain(entry => entry.Type == "Folder" && entry.FolderId == change.NewFolder.FolderId);
    }

    [TestMethod]
    public void MoveNodeToFolder_OnLayoutlessScene_InitializesLayoutAndGroupsNode()
    {
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Node" };
        scene.RootNodes.Add(node);
        var folder = this.organizer.CreateFolder(parentFolderId: null, "Group", scene);
        var folderId = folder.NewFolder!.FolderId!.Value;

        _ = this.organizer.MoveNodeToFolder(node.Id, folderId, scene);

        var folderEntry = FindFolder(scene.ExplorerLayout!, folderId);
        _ = folderEntry.Should().NotBeNull();
        _ = folderEntry!.Children.Should().Contain(entry => entry.Type == "Node" && entry.NodeId == node.Id);
        _ = scene.ExplorerLayout.Should().NotContain(entry => entry.Type == "Node" && entry.NodeId == node.Id, "node entry should have moved out of the root list into the folder");
    }

    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public void RemoveNodeFromLayout_WithOmittedNodeChildren_PreservesUntouchedEntryShape(bool entryExists)
    {
        var scene = CreateScene();
        var removed = new SceneNode(scene) { Name = "Removed" };
        var retained = new SceneNode(scene) { Name = "Retained" };
        scene.RootNodes.Add(removed);
        scene.RootNodes.Add(retained);
        scene.SetExplorerLayout([new ExplorerEntryData { NodeId = removed.Id }, new ExplorerEntryData { NodeId = retained.Id }]);

        _ = this.organizer.RemoveNodeFromLayout(entryExists ? removed.Id : Guid.NewGuid(), scene);

        _ = scene.ExplorerLayout.Should().HaveCount(entryExists ? 1 : 2);
        _ = scene.ExplorerLayout.Should().OnlyContain(entry => entry.Children == null, "removing a node must not materialize omitted children on unrelated entries");
        _ = scene.ExplorerLayout!.Last().NodeId.Should().Be(retained.Id);
    }

    private static Scene CreateScene()
    {
        var project = new Mock<IProject>().Object;
        return new Scene(project) { Name = "TestScene" };
    }

    private static Oxygen.Editor.World.Serialization.ExplorerEntryData? FindFolder(
        IList<Oxygen.Editor.World.Serialization.ExplorerEntryData> entries,
        Guid folderId)
    {
        foreach (var entry in entries)
        {
            if (entry.Type == "Folder" && entry.FolderId == folderId)
            {
                return entry;
            }

            if (entry.Children is not null)
            {
                var found = FindFolder(entry.Children, folderId);
                if (found is not null)
                {
                    return found;
                }
            }
        }

        return null;
    }
}
