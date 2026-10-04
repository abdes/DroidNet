// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Documents;
using DroidNet.Routing;
using Microsoft.UI;
using Moq;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.SceneExplorer;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Editor.WorldEditor.Documents.Selection;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.SceneExplorer;

/// <summary>Ensures scene search reveals collapsed matches and restores expansion on clear.</summary>
[TestClass]
public sealed class SceneExplorerSearchTests
{
    [TestMethod]
    public async Task SearchAsync_RevealsCollapsedDescendantAndRestoresExpansion()
    {
        var project = new Project(new ProjectInfo("Search", Category.Games, "H:/SearchTests", "preview.png")) { Name = "Search" };
        var scene = new Scene(project) { Name = "Scene" };
        var parent = new SceneNode(scene) { Name = "Parent", IsExpanded = false };
        var child = new SceneNode(scene) { Name = "FindMe" };
        parent.AddChild(child);
        scene.RootNodes.Add(parent);
        project.Scenes.Add(scene);
        var metadata = new SceneDocumentMetadata(scene.Id);
        var manager = new Mock<IProjectManagerService>(MockBehavior.Strict);
        _ = manager.SetupGet(value => value.CurrentProject).Returns(project);
        var documents = new Mock<IDocumentService>();
        _ = documents.Setup(value => value.GetOpenDocuments(It.IsAny<WindowId>())).Returns([metadata]);
        _ = documents.Setup(value => value.GetActiveDocumentId(It.IsAny<WindowId>())).Returns(metadata.DocumentId);
        var sync = new Mock<ISceneEngineSync>();
        _ = sync.Setup(value => value.GetDocumentScene(metadata)).Returns(scene);
        _ = sync.Setup(value => value.RegisterDocument(It.IsAny<Scene>(), metadata)).Returns(value: true);
        _ = sync.Setup(value => value.SyncSceneWhenReadyAsync(It.IsAny<Scene>(), It.IsAny<CancellationToken>())).ReturnsAsync(value: false);
        using var explorer = new SceneExplorerViewModel(manager.Object, new StrongReferenceMessenger(), Mock.Of<IRouter>(), documents.Object, default, sync.Object, new SceneSelectionService(), Mock.Of<ISceneDocumentCommandService>());

        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);

        var count = await explorer.SearchAsync("FindMe").ConfigureAwait(false);

        _ = count.Should().Be(1);
        var parentAdapter = await explorer.FindAdapterByNodeIdAsync(parent.Id).ConfigureAwait(false);
        _ = parentAdapter!.IsExpanded.Should().BeTrue("search should reveal the collapsed matching descendant");

        await explorer.ClearSearchAsync().ConfigureAwait(false);

        _ = parentAdapter.IsExpanded.Should().BeFalse("clearing search should restore the prior expansion state");
    }

    [TestMethod]
    public async Task SearchAsync_RevealsNodeInsideCollapsedNestedFolders()
    {
        var harness = CreateHarness();
        var scene = harness.Scene;
        var node = new SceneNode(scene) { Name = "FindMe" };
        scene.RootNodes.Add(node);
        var outerId = Guid.NewGuid();
        var innerId = Guid.NewGuid();
        scene.SetExplorerLayout(
            [
                new ExplorerEntryData
                {
                    Type = "Folder",
                    FolderId = outerId,
                    Name = "Outer",
                    IsExpanded = false,
                    Children =
                    [
                        new ExplorerEntryData
                        {
                            Type = "Folder",
                            FolderId = innerId,
                            Name = "Inner",
                            IsExpanded = false,
                            Children = [new ExplorerEntryData { Type = "Node", NodeId = node.Id }],
                        },
                    ],
                },
            ]);
        using var explorer = harness.Build();

        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);

        var count = await explorer.SearchAsync("FindMe").ConfigureAwait(false);

        _ = count.Should().Be(1);
        var outer = await explorer.FindFolderAdapterAsync(outerId).ConfigureAwait(false);
        var inner = await explorer.FindFolderAdapterAsync(innerId).ConfigureAwait(false);
        _ = outer.Should().NotBeNull();
        _ = inner.Should().NotBeNull();
        _ = outer!.IsExpanded.Should().BeTrue("search must expand the visual folder chain of a match");
        _ = inner!.IsExpanded.Should().BeTrue("search must expand nested folders of a match");
        var nodeAdapter = await explorer.FindAdapterByNodeIdAsync(node.Id).ConfigureAwait(false);
        _ = nodeAdapter.Should().NotBeNull();
        _ = explorer.ShownItems.Contains(nodeAdapter!).Should().BeTrue("the match must be made visible");
        _ = explorer.FilteredItems.Contains(nodeAdapter).Should().BeTrue("the filter must report the revealed match");

        await explorer.ClearSearchAsync().ConfigureAwait(false);

        _ = outer.IsExpanded.Should().BeFalse("clearing search should restore the authored collapsed state");
        _ = inner.IsExpanded.Should().BeFalse("clearing search should restore nested collapsed folders");
    }

    [TestMethod]
    public async Task SearchAsync_CountsCollapsedFolderMatchesFromLayoutDomain()
    {
        var harness = CreateHarness();
        var scene = harness.Scene;
        scene.SetExplorerLayout(
            [
                new ExplorerEntryData
                {
                    Type = "Folder",
                    FolderId = Guid.NewGuid(),
                    Name = "Alpha",
                    IsExpanded = false,
                    Children =
                    [
                        new ExplorerEntryData { Type = "Folder", FolderId = Guid.NewGuid(), Name = "ZebraRealized", IsExpanded = false },
                    ],
                },
                new ExplorerEntryData
                {
                    Type = "Node",
                    NodeId = Guid.NewGuid(), // unknown to the scene graph: this seat never realizes
                    Children =
                    [
                        new ExplorerEntryData { Type = "Folder", FolderId = Guid.NewGuid(), Name = "ZebraAuthored" },
                    ],
                },
            ]);
        using var explorer = harness.Build();

        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);

        var count = await explorer.SearchAsync("Zebra").ConfigureAwait(false);

        _ = count.Should().Be(2, "folder matches must be counted from the authored layout domain, not only from realized adapters");

        await explorer.ClearSearchAsync().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task SearchAsync_RestoresExpansionAcrossSuccessiveQueriesWithoutAuthoringLayout()
    {
        var harness = CreateHarness();
        var scene = harness.Scene;
        var first = new SceneNode(scene) { Name = "FindOne" };
        var second = new SceneNode(scene) { Name = "FindTwo" };
        scene.RootNodes.Add(first);
        scene.RootNodes.Add(second);
        var collapsedId = Guid.NewGuid();
        var expandedId = Guid.NewGuid();
        var nestedId = Guid.NewGuid();
        scene.SetExplorerLayout(
            [
                new ExplorerEntryData
                {
                    Type = "Folder",
                    FolderId = collapsedId,
                    Name = "Collapsed",
                    IsExpanded = false,
                    Children = [new ExplorerEntryData { Type = "Node", NodeId = first.Id }],
                },
                new ExplorerEntryData
                {
                    Type = "Folder",
                    FolderId = expandedId,
                    Name = "Expanded",
                    IsExpanded = true,
                    Children =
                    [
                        new ExplorerEntryData
                        {
                            Type = "Folder",
                            FolderId = nestedId,
                            Name = "Nested",
                            IsExpanded = false,
                            Children = [new ExplorerEntryData { Type = "Node", NodeId = second.Id }],
                        },
                    ],
                },
            ]);
        using var explorer = harness.Build();

        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);

        var layoutBefore = JsonSerializer.Serialize(scene.ExplorerLayout);
        var collapsed = await explorer.FindFolderAdapterAsync(collapsedId).ConfigureAwait(false);
        var expanded = await explorer.FindFolderAdapterAsync(expandedId).ConfigureAwait(false);
        var nested = await explorer.FindFolderAdapterAsync(nestedId).ConfigureAwait(false);
        _ = collapsed.Should().NotBeNull();
        _ = expanded.Should().NotBeNull();
        _ = nested.Should().NotBeNull();

        _ = (await explorer.SearchAsync("FindOne").ConfigureAwait(false)).Should().Be(1);
        _ = (await explorer.SearchAsync("FindTwo").ConfigureAwait(false)).Should().Be(1);

        _ = collapsed!.IsExpanded.Should().BeTrue("the first query's transient expansion accumulates until the search clears");
        _ = nested!.IsExpanded.Should().BeTrue("the second query must expand its own collapsed ancestry");
        _ = expanded!.IsExpanded.Should().BeTrue("authored expansion must be untouched by search");

        await explorer.ClearSearchAsync().ConfigureAwait(false);

        _ = collapsed.IsExpanded.Should().BeFalse("clearing search should restore the prior collapsed state");
        _ = nested.IsExpanded.Should().BeFalse("clearing search should restore nested collapsed state");
        _ = expanded.IsExpanded.Should().BeTrue("clearing search must not collapse folders the search did not expand");
        _ = JsonSerializer.Serialize(scene.ExplorerLayout).Should().Be(layoutBefore, "search must not write authored layout state");
        _ = harness.Metadata.IsDirty.Should().BeFalse("search must not dirty the document");
    }

    [TestMethod]
    public async Task SearchAsync_FolderEntryWithoutFolderId_ToleratedBySearchAndClear()
    {
        var harness = CreateHarness();
        var scene = harness.Scene;
        var node = new SceneNode(scene) { Name = "FindMe" };
        scene.RootNodes.Add(node);
        scene.SetExplorerLayout(
            [
                new ExplorerEntryData
                {
                    Type = "Folder",
                    Name = "Legacy FindMe Folder", // no FolderId: not stably addressable
                    IsExpanded = false,
                    Children = [new ExplorerEntryData { Type = "Node", NodeId = node.Id }],
                },
            ]);
        using var explorer = harness.Build();

        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);

        var count = await explorer.SearchAsync("FindMe").ConfigureAwait(false);

        _ = count.Should().Be(2, "the node match and the id-less folder match are both authored-domain matches");
        var nodeAdapter = await explorer.FindAdapterByNodeIdAsync(node.Id).ConfigureAwait(false);
        _ = explorer.ShownItems.Contains(nodeAdapter!).Should().BeFalse("a folder without a stable id cannot be addressed for transient expansion");

        await explorer.ClearSearchAsync().ConfigureAwait(false);
    }

    private static Harness CreateHarness()
    {
        var project = new Project(new ProjectInfo("Search", Category.Games, "H:/SearchTests", "preview.png")) { Name = "Search" };
        var scene = new Scene(project) { Name = "Scene" };
        project.Scenes.Add(scene);
        return new Harness(project, scene);
    }

    private sealed class Harness
    {
        public Harness(Project project, Scene scene)
        {
            this.Project = project;
            this.Scene = scene;
            this.Metadata = new SceneDocumentMetadata(scene.Id);

            var manager = new Mock<IProjectManagerService>(MockBehavior.Strict);
            _ = manager.SetupGet(value => value.CurrentProject).Returns(project);
            this.Manager = manager;

            var documents = new Mock<IDocumentService>();
            _ = documents.Setup(value => value.GetOpenDocuments(It.IsAny<WindowId>())).Returns([this.Metadata]);
            _ = documents.Setup(value => value.GetActiveDocumentId(It.IsAny<WindowId>())).Returns(this.Metadata.DocumentId);
            this.Documents = documents;

            var sync = new Mock<ISceneEngineSync>();
            _ = sync.Setup(value => value.GetDocumentScene(this.Metadata)).Returns(scene);
            _ = sync.Setup(value => value.RegisterDocument(It.IsAny<Scene>(), this.Metadata)).Returns(value: true);
            _ = sync.Setup(value => value.SyncSceneWhenReadyAsync(It.IsAny<Scene>(), It.IsAny<CancellationToken>())).ReturnsAsync(value: false);
            this.Sync = sync;
        }

        public Project Project { get; }

        public Scene Scene { get; }

        public SceneDocumentMetadata Metadata { get; }

        public Mock<IProjectManagerService> Manager { get; }

        public Mock<IDocumentService> Documents { get; }

        public Mock<ISceneEngineSync> Sync { get; }

        public SceneExplorerViewModel Build()
            => new(
                this.Manager.Object,
                new StrongReferenceMessenger(),
                Mock.Of<IRouter>(),
                this.Documents.Object,
                default,
                this.Sync.Object,
                new SceneSelectionService(),
                Mock.Of<ISceneDocumentCommandService>());
    }
}
