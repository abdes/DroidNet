// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Controls;
using DroidNet.Routing;
using Moq;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Scenes.Layout;
using Oxygen.Editor.Scenes.Selection;
using Oxygen.Editor.Scenes.Testing;
using Oxygen.Editor.Shell.Testing;
using Oxygen.Editor.World;

namespace Oxygen.Editor.SceneExplorer.Unit.Tests;

/// <summary>Ensures a drop moves or copies the dragged items in the scene document, and the tree follows.</summary>
[TestClass]
public sealed partial class SceneExplorerDropTests
{
    [TestMethod]
    public async Task CommitDropAsync_NodeOntoNode_ParentsItInOneUndoStep()
    {
        using var harness = CreateHarness(out var scene, out var parent, out var child);
        using var explorer = harness.Build();

        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);
        var childAdapter = await explorer.FindAdapterByNodeIdAsync(child.Id).ConfigureAwait(false);
        var parentAdapter = await explorer.FindAdapterByNodeIdAsync(parent.Id).ConfigureAwait(false);

        var result = await explorer.CommitDropAsync(new TreeDropRequest([childAdapter!], parentAdapter!, 0, TreeDropOperation.Move)).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = result.Items.Should().ContainSingle().Which.Should().BeAssignableTo<SceneNodeAdapter>();
        _ = scene.AllNodes.Single(node => node.Id == child.Id).Hierarchy.Parent!.Id.Should().Be(parent.Id);
        _ = harness.Metadata.Document.History.UndoStack.Should().ContainSingle();
    }

    [TestMethod]
    public async Task CommitDropAsync_NodeOntoFolder_GroupsItInOneUndoStep()
    {
        using var harness = CreateHarness(out var scene, out var parent, out var child, folderId: FolderId.New());
        harness.Metadata.ReplaceLayout(
            [
                new ExplorerEntryData { Type = "Node", NodeId = parent.Id },
                new ExplorerEntryData
                {
                    Type = "Folder",
                    FolderId = harness.FolderId,
                    Name = "Group",
                    Children = [new ExplorerEntryData { Type = "Node", NodeId = child.Id }],
                },
            ]);
        using var explorer = harness.Build();

        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);
        var folder = await explorer.FindFolderAdapterAsync(harness.FolderId).ConfigureAwait(false);
        var parentAdapter = await explorer.FindAdapterByNodeIdAsync(parent.Id).ConfigureAwait(false);

        var result = await explorer.CommitDropAsync(new TreeDropRequest([parentAdapter!], folder!, 0, TreeDropOperation.Move)).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = harness.Metadata.Layout.Single(entry => entry.FolderId == harness.FolderId).Children!.Select(static entry => entry.NodeId).Should().Contain(parent.Id);
        _ = harness.Metadata.Document.History.UndoStack.Should().ContainSingle();
    }

    [TestMethod]
    public async Task CommitDropAsync_CopyDrop_AddsACopyInOneUndoStep()
    {
        using var harness = CreateHarness(out var scene, out _, out var child);
        using var explorer = harness.Build();

        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);
        var childAdapter = await explorer.FindAdapterByNodeIdAsync(child.Id).ConfigureAwait(false);

        var result = await explorer.CommitDropAsync(new TreeDropRequest([childAdapter!], explorer.Scene!, 0, TreeDropOperation.Copy)).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = scene.RootNodes.Should().HaveCount(3);
        _ = harness.Metadata.Document.History.UndoStack.Should().ContainSingle();
    }

    [TestMethod]
    public async Task FindFolderAdapterAsync_ReturnsIndexedFolderWithoutForceLoading()
    {
        using var harness = CreateHarness(out var scene, out var parent, out var child, folderId: FolderId.New());
        harness.Metadata.ReplaceLayout(
            [
                new ExplorerEntryData
                {
                    Type = "Folder",
                    FolderId = harness.FolderId,
                    Name = "Group",
                    Children = [new ExplorerEntryData { Type = "Node", NodeId = parent.Id }],
                },
                new ExplorerEntryData { Type = "Node", NodeId = child.Id },
            ]);
        using var explorer = harness.Build();

        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);

        var folder = await explorer.FindFolderAdapterAsync(harness.FolderId).ConfigureAwait(false);

        _ = folder.Should().NotBeNull();
        _ = folder!.Id.Should().Be(harness.FolderId);
    }

    private static Harness CreateHarness(out Scene scene, out SceneNode parent, out SceneNode child, FolderId? folderId = null)
    {
        var project = new Project(new ProjectInfo("Drop", Category.Games, "H:/DropTreeTests", "preview.png"));
        scene = new Scene { Name = "Scene" };
        parent = new SceneNode(scene, "Parent");
        child = new SceneNode(scene, "Child");
        scene.RootNodes.Add(parent);
        scene.RootNodes.Add(child);
        var harness = new Harness(project, scene, folderId ?? FolderId.New());
        scene = harness.Scene;
        parent = harness.Metadata.NodeOf(parent);
        child = harness.Metadata.NodeOf(child);
        return harness;
    }

    private sealed partial class Harness : IDisposable
    {
        public Harness(Project project, Scene scene, FolderId folderId)
        {
            this.Project = project;
            this.FolderId = folderId;
            this.Metadata = TestScenes.Open(scene);
            this.Scene = this.Metadata.Scene;

            var documents = new TestDocumentArea();
            documents.Activate(this.Metadata.Document);
            this.Documents = documents;
        }

        public Project Project { get; }

        public Scene Scene { get; }

        public FolderId FolderId { get; }

        public TestScene Metadata { get; }

        public TestDocumentArea Documents { get; }

        /// <summary>Gets the open documents that hold the harness's first scene.</summary>
        public Editor.Documents.OpenDocuments OpenDocuments => this.Metadata.Documents;
        public SceneAdapter? SceneAdapter { get; private set; }

        public void Dispose() => this.Metadata.Dispose();

        public SceneExplorerViewModel Build()
        {
            var explorer = new SceneExplorerViewModel(
                Mock.Of<IRouter>(),
                this.Documents,
                this.OpenDocuments,
                new SceneSelectionService());
            this.SceneAdapter = explorer.Scene;
            return explorer;
        }
    }
}
