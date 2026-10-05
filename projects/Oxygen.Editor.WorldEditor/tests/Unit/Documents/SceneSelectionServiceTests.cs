// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Moq;
using Oxygen.Editor.World.SceneExplorer;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.WorldEditor.Documents.Selection;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Documents;

[TestClass]
[TestCategory("Scene Selection")]
public sealed class SceneSelectionServiceTests
{
    [TestMethod]
    public void SetSelection_DeduplicatesNodesAndPreservesOrder()
    {
        var scene = CreateScene();
        var first = new SceneNode(scene) { Name = "First" };
        var second = new SceneNode(scene) { Name = "Second" };
        scene.RootNodes.Add(first);
        scene.RootNodes.Add(second);
        var sut = new SceneSelectionService();

        sut.SetSelection(scene.Id, [first, second, first], "test");

        _ = sut.GetSelectedNodes(scene.Id, scene).Should().Equal(first, second);
    }

    [TestMethod]
    public void Reconcile_RemovesNodesThatNoLongerBelongToScene()
    {
        var scene = CreateScene();
        var staleScene = CreateScene();
        var selected = new SceneNode(scene) { Name = "Selected" };
        var stale = new SceneNode(staleScene) { Name = "Stale" };
        scene.RootNodes.Add(selected);
        staleScene.RootNodes.Add(stale);
        var sut = new SceneSelectionService();
        sut.SetSelection(scene.Id, [selected, stale], "test");

        var reconciled = sut.Reconcile(scene.Id, scene);

        _ = reconciled.Should().Equal(selected);
        _ = sut.GetSelectedNodes(scene.Id, scene).Should().Equal(selected);
    }

    [TestMethod]
    public void Clear_RemovesDocumentSelection()
    {
        var scene = CreateScene();
        var selected = new SceneNode(scene) { Name = "Selected" };
        scene.RootNodes.Add(selected);
        var sut = new SceneSelectionService();
        sut.SetSelection(scene.Id, [selected], "test");

        sut.Clear(scene.Id);

        _ = sut.GetSelectedNodes(scene.Id, scene).Should().BeEmpty();
    }

    [TestMethod]
    public void Publish_StoresContextAndClearRemovesIt()
    {
        var documentId = Guid.NewGuid();
        var folderId = Guid.NewGuid();
        var context = new SceneSelectionContext(SceneSelectionKind.Folder, [], [folderId], null, folderId);
        var sut = new SceneSelectionService();

        sut.Publish(documentId, context, "test");

        _ = sut.GetContext(documentId).Should().Be(context);
        sut.Clear(documentId);
        _ = sut.GetContext(documentId).Should().Be(SceneSelectionContext.Empty);
    }

    [TestMethod]
    public void GetContext_WithoutSelection_ReturnsEmpty()
    {
        var sut = new SceneSelectionService();

        _ = sut.GetContext(Guid.NewGuid()).Should().Be(SceneSelectionContext.Empty);
    }

    [TestMethod]
    public void Publish_FiresSelectionChangedWithTheStoredContextAndSource()
    {
        var documentId = Guid.NewGuid();
        var folderId = Guid.NewGuid();
        var context = new SceneSelectionContext(SceneSelectionKind.Folder, [], [folderId], null, folderId);
        var sut = new SceneSelectionService();
        SceneSelectionChangedEventArgs? received = null;
        sut.SelectionChanged += (_, args) => received = args;

        sut.Publish(documentId, context, "SceneExplorer");

        _ = received.Should().NotBeNull();
        _ = received!.DocumentId.Should().Be(documentId);
        _ = received.Context.Should().Be(context);
        _ = received.Source.Should().Be("SceneExplorer");
    }

    [TestMethod]
    public void SetSelection_ClassifiesAsNodeWithLastNodePrimaryAndFires()
    {
        var scene = CreateScene();
        var first = new SceneNode(scene) { Name = "First" };
        var second = new SceneNode(scene) { Name = "Second" };
        scene.RootNodes.Add(first);
        scene.RootNodes.Add(second);
        var sut = new SceneSelectionService();
        SceneSelectionChangedEventArgs? received = null;
        sut.SelectionChanged += (_, args) => received = args;

        sut.SetSelection(scene.Id, [first, second], "Command");

        _ = received!.Context.Kind.Should().Be(SceneSelectionKind.Node);
        _ = received.Context.PrimaryNodeId.Should().Be(second.Id);
        _ = sut.GetContext(scene.Id).Should().Be(received.Context);
    }

    [TestMethod]
    public void Clear_FiresEmptyContextOnlyWhenAnEntryExisted()
    {
        var scene = CreateScene();
        var selected = new SceneNode(scene) { Name = "Selected" };
        scene.RootNodes.Add(selected);
        var sut = new SceneSelectionService();
        var events = new List<SceneSelectionChangedEventArgs>();
        sut.SelectionChanged += (_, args) => events.Add(args);

        sut.Clear(scene.Id);
        _ = events.Should().BeEmpty("an absent document has no selection to announce");

        sut.SetSelection(scene.Id, [selected], "test");
        events.Clear();

        sut.Clear(scene.Id);

        _ = events.Should().ContainSingle();
        _ = events[0].Context.Should().Be(SceneSelectionContext.Empty);
        _ = events[0].Source.Should().Be("Clear");
    }

    [TestMethod]
    public void Reconcile_PrunesMissingNodesKeepsFoldersAndReclassifies()
    {
        var scene = CreateScene();
        var staleScene = CreateScene();
        var selected = new SceneNode(scene) { Name = "Selected" };
        var stale = new SceneNode(staleScene) { Name = "Stale" };
        scene.RootNodes.Add(selected);
        var folderId = Guid.NewGuid();
        scene.SetExplorerLayout([new ExplorerEntryData { Type = "Folder", FolderId = folderId }]);
        var sut = new SceneSelectionService();
        sut.Publish(
            scene.Id,
            new SceneSelectionContext(SceneSelectionKind.Mixed, [selected.Id, stale.Id], [folderId], stale.Id, null),
            "SceneExplorer");

        var reconciled = sut.Reconcile(scene.Id, scene);

        _ = reconciled.Should().Equal(selected);
        var context = sut.GetContext(scene.Id);
        _ = context.Kind.Should().Be(SceneSelectionKind.Mixed, "the surviving node and the folder still make a mixed batch");
        _ = context.SelectedNodeIds.Should().Equal(selected.Id);
        _ = context.PrimaryNodeId.Should().Be(selected.Id, "a pruned primary falls back to the first surviving node");
    }

    [TestMethod]
    public void Reconcile_WhenNothingSurvives_ClearsAndAnnounces()
    {
        var scene = CreateScene();
        var staleScene = CreateScene();
        var stale = new SceneNode(staleScene) { Name = "Stale" };
        var sut = new SceneSelectionService();
        sut.SetSelection(scene.Id, [stale], "test");
        var events = new List<SceneSelectionChangedEventArgs>();
        sut.SelectionChanged += (_, args) => events.Add(args);

        var reconciled = sut.Reconcile(scene.Id, scene);

        _ = reconciled.Should().BeEmpty();
        _ = sut.GetContext(scene.Id).Should().Be(SceneSelectionContext.Empty);
        _ = events.Should().ContainSingle();
        _ = events[0].Source.Should().Be("Clear");
    }

    [TestMethod]
    public void Context_CapturesIdentityCollectionsWithoutAliasingCallerLists()
    {
        var nodeId = Guid.NewGuid();
        var folderId = Guid.NewGuid();
        var nodes = new List<Guid> { nodeId };
        var folders = new List<Guid> { folderId };
        var context = new SceneSelectionContext(SceneSelectionKind.Mixed, nodes, folders, nodeId, null);

        nodes.Clear();
        folders.Clear();

        _ = context.SelectedNodeIds.Should().Equal(nodeId);
        _ = context.SelectedFolderIds.Should().Equal(folderId);
    }

    [TestMethod]
    public void Reconcile_SurvivingFolderPrimary_DoesNotInventNodePrimary()
    {
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Selected" };
        scene.RootNodes.Add(node);
        var folderId = Guid.NewGuid();
        scene.SetExplorerLayout([new ExplorerEntryData { Type = "Folder", FolderId = folderId }]);
        var sut = new SceneSelectionService();
        var context = new SceneSelectionContext(SceneSelectionKind.Mixed, [node.Id], [folderId], null, folderId);
        sut.Publish(scene.Id, context, "test");

        _ = sut.Reconcile(scene.Id, scene);

        _ = sut.GetContext(scene.Id).Should().BeSameAs(context, "an unchanged reconciliation preserves the snapshot");
        _ = sut.GetContext(scene.Id).PrimaryNodeId.Should().BeNull();
        _ = sut.GetContext(scene.Id).PrimaryFolderId.Should().Be(folderId);
    }

    [TestMethod]
    public void Reconcile_DeletedFolderPrimary_FallsBackToFirstSurvivingFolder()
    {
        var scene = CreateScene();
        var first = Guid.NewGuid();
        var second = Guid.NewGuid();
        var deleted = Guid.NewGuid();
        scene.SetExplorerLayout(
            [
                new ExplorerEntryData
                {
                    Type = "Folder",
                    FolderId = first,
                    Children = [new ExplorerEntryData { Type = "Folder", FolderId = second }],
                },
            ]);
        var sut = new SceneSelectionService();
        sut.Publish(scene.Id, new SceneSelectionContext(SceneSelectionKind.Folder, [], [first, second, deleted], null, deleted), "test");

        _ = sut.Reconcile(scene.Id, scene);

        var context = sut.GetContext(scene.Id);
        _ = context.SelectedFolderIds.Should().Equal(first, second);
        _ = context.PrimaryFolderId.Should().Be(first);
        _ = context.PrimaryNodeId.Should().BeNull();
    }

    [TestMethod]
    public void Reconcile_AllSelectedFoldersDeleted_ClearsSelection()
    {
        var scene = CreateScene();
        var folderId = Guid.NewGuid();
        var sut = new SceneSelectionService();
        sut.Publish(scene.Id, new SceneSelectionContext(SceneSelectionKind.Folder, [], [folderId], null, folderId), "test");

        _ = sut.Reconcile(scene.Id, scene);

        _ = sut.GetContext(scene.Id).Should().BeSameAs(SceneSelectionContext.Empty);
    }

    [TestMethod]
    public void Reconcile_DeletedNodePrimary_FallsBackToFirstNotLastSurvivor()
    {
        var scene = CreateScene();
        var first = new SceneNode(scene) { Name = "First" };
        var second = new SceneNode(scene) { Name = "Second" };
        scene.RootNodes.Add(first);
        scene.RootNodes.Add(second);
        var deletedId = Guid.NewGuid();
        var sut = new SceneSelectionService();
        sut.Publish(scene.Id, new SceneSelectionContext(SceneSelectionKind.Node, [first.Id, second.Id, deletedId], [], deletedId, null), "test");

        _ = sut.Reconcile(scene.Id, scene);

        _ = sut.GetContext(scene.Id).PrimaryNodeId.Should().Be(first.Id);
    }

    private static Scene CreateScene()
    {
        var project = new Mock<IProject>().Object;
        return new Scene(project) { Name = "Test Scene" };
    }
}
