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
using Oxygen.Editor.World.Documents.Commands;
using Oxygen.Editor.World.SceneExplorer;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Editor.WorldEditor.Documents.Selection;
using Oxygen.Editor.WorldEditor.TestSupport;
using Oxygen.Managed.Core;

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
        var metadata = SceneTabs.Open(scene);
        scene = metadata.Scene;
        parent = metadata.NodeOf(parent);
        child = metadata.NodeOf(child);
        var manager = new Mock<IProjectManagerService>(MockBehavior.Strict);
        _ = manager.SetupGet(value => value.CurrentProject).Returns(project);
        var documents = new TestDocumentArea();
        documents.Activate(metadata.Document);
        var sync = new Mock<ISceneEngineSync>();
        _ = sync.Setup(value => value.GetDocumentScene(metadata.DocumentId)).Returns(scene);
        _ = sync.Setup(value => value.RegisterDocument(It.IsAny<Scene>(), metadata.DocumentId)).Returns(value: true);
        _ = sync.Setup(value => value.SyncSceneWhenReadyAsync(It.IsAny<Scene>(), It.IsAny<CancellationToken>())).ReturnsAsync(value: false);
        using var explorer = new SceneExplorerViewModel(manager.Object, new StrongReferenceMessenger(), Mock.Of<IRouter>(), documents, metadata.Documents, default, sync.Object, new SceneSelectionService(), Mock.Of<ISceneDocumentCommandService>());

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
        var outerId = FolderId.New();
        var innerId = FolderId.New();
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
        var collapsedFolderId = FolderId.New();
        var matchingFolderId = FolderId.New();
        var unresolvedFolderId = FolderId.New();
        scene.SetExplorerLayout(
            [
                new ExplorerEntryData
                {
                    Type = "Folder",
                    FolderId = collapsedFolderId,
                    Name = "Alpha",
                    IsExpanded = false,
                    Children =
                    [
                        new ExplorerEntryData { Type = "Folder", FolderId = matchingFolderId, Name = "ZebraRealized", IsExpanded = false },
                    ],
                },
                new ExplorerEntryData
                {
                    Type = "Node",
                    NodeId = NodeId.New(), // unknown to the scene graph: this seat never realizes
                    Children =
                    [
                        new ExplorerEntryData { Type = "Folder", FolderId = unresolvedFolderId, Name = "ZebraAuthored" },
                    ],
                },
            ]);
        using var explorer = harness.Build();

        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);

        var count = await explorer.SearchAsync("Zebra").ConfigureAwait(false);

        _ = count.Should().Be(2, "folder matches must be counted from the authored layout domain, not only from realized adapters");
        var collapsed = await explorer.FindFolderAdapterAsync(collapsedFolderId).ConfigureAwait(false);
        var matching = await explorer.FindFolderAdapterAsync(matchingFolderId).ConfigureAwait(false);
        _ = collapsed!.IsExpanded.Should().BeTrue("search must reveal collapsed ancestors of a matching folder");
        _ = matching.Should().NotBeNull();
        _ = explorer.ShownItems.Contains(matching!).Should().BeTrue("the matching folder must be visible after its ancestors expand");
        _ = (await explorer.FindFolderAdapterAsync(unresolvedFolderId).ConfigureAwait(false))
            .Should().BeNull("a folder beneath an unrealized node seat remains authored but cannot be shown");

        await explorer.ClearSearchAsync().ConfigureAwait(false);
        _ = collapsed.IsExpanded.Should().BeFalse("clearing search must restore the authored collapsed ancestor");
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
        var collapsedId = FolderId.New();
        var expandedId = FolderId.New();
        var nestedId = FolderId.New();
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
        _ = harness.Metadata.Document.IsDirty.Should().BeFalse("search must not dirty the document");
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

    [TestMethod]
    public async Task ReconcileDuringSearch_KeepsTransientExpansionUnauthoredAndRestorable()
    {
        var harness = CreateHarness();
        var scene = harness.Scene;
        var find = new SceneNode(scene) { Name = "FindMe" };
        var other = new SceneNode(scene) { Name = "Other" };
        scene.RootNodes.Add(find);
        scene.RootNodes.Add(other);
        var folderId = FolderId.New();
        scene.SetExplorerLayout(
            [
                new ExplorerEntryData
                {
                    Type = "Folder",
                    FolderId = folderId,
                    Name = "Collapsed",
                    IsExpanded = false,
                    Children = [new ExplorerEntryData { Type = "Node", NodeId = find.Id }],
                },
                new ExplorerEntryData { Type = "Node", NodeId = other.Id },
            ]);
        using var explorer = harness.Build();

        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);

        var layoutBefore = JsonSerializer.Serialize(scene.ExplorerLayout);
        _ = (await explorer.SearchAsync("FindMe").ConfigureAwait(false)).Should().Be(1);
        var folder = await explorer.FindFolderAdapterAsync(folderId).ConfigureAwait(false);
        _ = folder!.IsExpanded.Should().BeTrue("search expands the collapsed match ancestry");

        _ = harness.Commands
            .Setup(value => value.CreateNodeAsync(It.IsAny<SceneDocumentCommandContext>(), null, null, It.IsAny<string>()))
            .ReturnsAsync(SceneCommandResults.Success(new SceneNode(scene) { Name = "Added" }));
        await explorer.AddEntityCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);

        var rebuiltFolder = await explorer.FindFolderAdapterAsync(folderId).ConfigureAwait(false);
        _ = rebuiltFolder.Should().NotBeNull();
        _ = ReferenceEquals(rebuiltFolder, folder).Should().BeFalse("the command rebuild retires the old adapters");
        _ = rebuiltFolder!.IsExpanded.Should().BeTrue("the transient search expansion must survive the rebuild on the fresh adapter");
        _ = JsonSerializer.Serialize(scene.ExplorerLayout).Should().Be(layoutBefore, "restoring expansion during reconcile must not re-author entry.IsExpanded");

        await explorer.ClearSearchAsync().ConfigureAwait(false);

        _ = rebuiltFolder.IsExpanded.Should().BeFalse("clearing after a rebuild restores the prior collapsed view through the live adapter");
        _ = JsonSerializer.Serialize(scene.ExplorerLayout).Should().Be(layoutBefore, "clearing after a rebuild must still author nothing");
        _ = harness.Metadata.Document.IsDirty.Should().BeFalse();
    }

    private static Harness CreateHarness()
    {
        var project = new Project(new ProjectInfo("Search", Category.Games, "H:/SearchTests", "preview.png")) { Name = "Search" };
        var scene = new Scene(project) { Name = "Scene" };
        return new Harness(project, scene);
    }

    private sealed class Harness
    {
        public Harness(Project project, Scene scene)
        {
            this.Project = project;
            this.Metadata = SceneTabs.Open(scene);
            this.Scene = this.Metadata.Scene;

            var manager = new Mock<IProjectManagerService>(MockBehavior.Strict);
            _ = manager.SetupGet(value => value.CurrentProject).Returns(project);
            this.Manager = manager;

            var documents = new TestDocumentArea();
            documents.Activate(this.Metadata.Document);
            this.Documents = documents;

            var sync = new Mock<ISceneEngineSync>();
            _ = sync.Setup(value => value.GetDocumentScene(this.Metadata.DocumentId)).Returns(this.Scene);
            _ = sync.Setup(value => value.RegisterDocument(It.IsAny<Scene>(), this.Metadata.DocumentId)).Returns(value: true);
            _ = sync.Setup(value => value.SyncSceneWhenReadyAsync(It.IsAny<Scene>(), It.IsAny<CancellationToken>())).ReturnsAsync(value: false);
            this.Sync = sync;

            this.Commands = new Mock<ISceneDocumentCommandService>(MockBehavior.Strict);
        }

        public Project Project { get; }

        public Scene Scene { get; }

        public SceneTab Metadata { get; }

        public Mock<IProjectManagerService> Manager { get; }

        public TestDocumentArea Documents { get; }

        /// <summary>Gets the open documents that hold the harness's first scene.</summary>
        public Oxygen.Editor.Documents.OpenDocuments OpenDocuments => this.Metadata.Documents;

        public Mock<ISceneEngineSync> Sync { get; }

        public Mock<ISceneDocumentCommandService> Commands { get; }

        public SceneExplorerViewModel Build()
            => new(
                this.Manager.Object,
                new StrongReferenceMessenger(),
                Mock.Of<IRouter>(),
                this.Documents,
                this.OpenDocuments,
                default,
                this.Sync.Object,
                new SceneSelectionService(),
                this.Commands.Object);
    }
}
