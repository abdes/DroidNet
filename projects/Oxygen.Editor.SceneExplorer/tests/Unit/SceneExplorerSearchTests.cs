// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using AwesomeAssertions;
using DroidNet.Routing;
using Moq;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Scenes.Layout;
using Oxygen.Editor.Scenes.Selection;
using Oxygen.Editor.Scenes.Testing;
using Oxygen.Editor.Shell.Testing;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneExplorer.Unit.Tests;

/// <summary>Ensures scene search reveals collapsed matches and restores expansion on clear.</summary>
[TestClass]
public sealed partial class SceneExplorerSearchTests
{
    [TestMethod]
    public async Task SearchAsync_RevealsCollapsedDescendantAndRestoresExpansion()
    {
        var scene = new Scene { Name = "Scene" };
        var parent = new SceneNode(scene, "Parent");
        var child = new SceneNode(scene, "FindMe");
        parent.Hierarchy.AddChild(child);
        scene.RootNodes.Add(parent);
        using var metadata = TestScenes.Open(scene);
        scene = metadata.Scene;
        parent = metadata.NodeOf(parent);
        child = metadata.NodeOf(child);
        var documents = new TestDocumentArea();
        documents.Activate(metadata.Document);
        using var explorer = new SceneExplorerViewModel(Mock.Of<IRouter>(), documents, metadata.Documents, new DocumentScenes(metadata.Documents), new SceneSelectionService());

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
        using var harness = CreateHarness();
        var scene = harness.Scene;
        var node = new SceneNode(scene, "FindMe");
        scene.RootNodes.Add(node);
        var outerId = FolderId.New();
        var innerId = FolderId.New();
        harness.Metadata.ReplaceLayout(
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
        _ = explorer.ShownItems.Should().Contain(nodeAdapter!, "the match must be made visible");
        _ = explorer.FilteredItems.Should().Contain(nodeAdapter, "the filter must report the revealed match");

        await explorer.ClearSearchAsync().ConfigureAwait(false);

        _ = outer.IsExpanded.Should().BeFalse("clearing search should restore the authored collapsed state");
        _ = inner.IsExpanded.Should().BeFalse("clearing search should restore nested collapsed folders");
    }

    [TestMethod]
    public async Task SearchAsync_CountsCollapsedFolderMatchesFromLayoutDomain()
    {
        using var harness = CreateHarness();
        var scene = harness.Scene;
        var collapsedFolderId = FolderId.New();
        var matchingFolderId = FolderId.New();
        var unresolvedFolderId = FolderId.New();
        harness.Metadata.ReplaceLayout(
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
        _ = explorer.ShownItems.Should().Contain(matching!, "the matching folder must be visible after its ancestors expand");
        _ = (await explorer.FindFolderAdapterAsync(unresolvedFolderId).ConfigureAwait(false))
            .Should().BeNull("a folder beneath an unrealized node seat remains authored but cannot be shown");

        await explorer.ClearSearchAsync().ConfigureAwait(false);
        _ = collapsed.IsExpanded.Should().BeFalse("clearing search must restore the authored collapsed ancestor");
    }

    [TestMethod]
    public async Task SearchAsync_RestoresExpansionAcrossSuccessiveQueriesWithoutAuthoringLayout()
    {
        using var harness = CreateHarness();
        var scene = harness.Scene;
        var first = new SceneNode(scene, "FindOne");
        var second = new SceneNode(scene, "FindTwo");
        scene.RootNodes.Add(first);
        scene.RootNodes.Add(second);
        var collapsedId = FolderId.New();
        var expandedId = FolderId.New();
        var nestedId = FolderId.New();
        SetCollapsedAndNestedLayout(harness.Metadata, (first.Id, collapsedId), (second.Id, expandedId, nestedId));
        using var explorer = harness.Build();

        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);

        var layoutBefore = JsonSerializer.Serialize(harness.Metadata.Layout);
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
        _ = JsonSerializer.Serialize(harness.Metadata.Layout).Should().Be(layoutBefore, "search must not write authored layout state");
        _ = harness.Metadata.Document.IsDirty.Should().BeFalse("search must not dirty the document");
    }

    [TestMethod]
    public async Task SearchAsync_FolderEntryWithoutFolderId_ToleratedBySearchAndClear()
    {
        using var harness = CreateHarness();
        var scene = harness.Scene;
        var node = new SceneNode(scene, "FindMe");
        scene.RootNodes.Add(node);
        harness.Metadata.ReplaceLayout(
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
        _ = explorer.ShownItems.Should().NotContain(nodeAdapter!, "a folder without a stable id cannot be addressed for transient expansion");

        await explorer.ClearSearchAsync().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task ReconcileDuringSearch_KeepsTransientExpansionUnauthoredAndRestorable()
    {
        using var harness = CreateHarness();
        var scene = harness.Scene;
        var find = new SceneNode(scene, "FindMe");
        var other = new SceneNode(scene, "Other");
        scene.RootNodes.Add(find);
        scene.RootNodes.Add(other);
        var folderId = FolderId.New();
        harness.Metadata.ReplaceLayout(
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

        bool? StoredExpansion() => harness.Metadata.Layout.Single(entry => entry.FolderId == folderId).IsExpanded;
        _ = (await explorer.SearchAsync("FindMe").ConfigureAwait(false)).Should().Be(1);
        var folder = await explorer.FindFolderAdapterAsync(folderId).ConfigureAwait(false);
        _ = folder!.IsExpanded.Should().BeTrue("search expands the collapsed match ancestry");

        await explorer.AddEntityCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);

        var rebuiltFolder = await explorer.FindFolderAdapterAsync(folderId).ConfigureAwait(false);
        _ = rebuiltFolder.Should().NotBeNull();
        _ = ReferenceEquals(rebuiltFolder, folder).Should().BeFalse("the rebuild after an edit retires the old adapters");
        _ = rebuiltFolder!.IsExpanded.Should().BeTrue("the transient search expansion must survive the rebuild on the fresh adapter");
        _ = StoredExpansion().Should().BeFalse("restoring expansion during reconcile must not re-author entry.IsExpanded");

        await explorer.ClearSearchAsync().ConfigureAwait(false);

        _ = rebuiltFolder.IsExpanded.Should().BeFalse("clearing after a rebuild restores the prior collapsed view through the live adapter");
        _ = StoredExpansion().Should().BeFalse("clearing after a rebuild must still author nothing");
        _ = harness.Metadata.Document.History.UndoStack.Should().ContainSingle("only the added node is authored");
    }

    // A collapsed folder holding one node, and an expanded folder holding a collapsed folder that holds the other.
    private static void SetCollapsedAndNestedLayout(TestScene testScene, (NodeId Node, FolderId Folder) collapsed, (NodeId Node, FolderId Outer, FolderId Folder) nested)
        => testScene.ReplaceLayout(
            [
                new ExplorerEntryData
                {
                    Type = "Folder",
                    FolderId = collapsed.Folder,
                    Name = "Collapsed",
                    IsExpanded = false,
                    Children = [new ExplorerEntryData { Type = "Node", NodeId = collapsed.Node }],
                },
                new ExplorerEntryData
                {
                    Type = "Folder",
                    FolderId = nested.Outer,
                    Name = "Expanded",
                    IsExpanded = true,
                    Children =
                    [
                        new ExplorerEntryData
                        {
                            Type = "Folder",
                            FolderId = nested.Folder,
                            Name = "Nested",
                            IsExpanded = false,
                            Children = [new ExplorerEntryData { Type = "Node", NodeId = nested.Node }],
                        },
                    ],
                },
            ]);

    private static Harness CreateHarness()
    {
        var project = new Project(new ProjectInfo("Search", Category.Games, "H:/SearchTests", "preview.png"));
        var scene = new Scene { Name = "Scene" };
        return new Harness(project, scene);
    }

    private sealed partial class Harness : IDisposable
    {
        public Harness(Project project, Scene scene)
        {
            this.Project = project;
            this.Metadata = TestScenes.Open(scene);
            this.Scene = this.Metadata.Scene;

            var documents = new TestDocumentArea();
            documents.Activate(this.Metadata.Document);
            this.Documents = documents;
        }

        public Project Project { get; }

        public Scene Scene { get; }

        public TestScene Metadata { get; }

        public TestDocumentArea Documents { get; }

        /// <summary>Gets the open documents that hold the harness's first scene.</summary>
        public Editor.Documents.OpenDocuments OpenDocuments => this.Metadata.Documents;

        public void Dispose() => this.Metadata.Dispose();

        public SceneExplorerViewModel Build()
            => new(
                Mock.Of<IRouter>(),
                this.Documents,
                this.OpenDocuments,
                new DocumentScenes(this.OpenDocuments),
                new SceneSelectionService());
    }
}
