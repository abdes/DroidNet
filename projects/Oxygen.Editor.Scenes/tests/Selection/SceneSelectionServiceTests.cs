// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Documents;
using Oxygen.Editor.Scenes.Layout;
using Oxygen.Editor.Scenes.Selection;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.Selection;

[TestClass]
[TestCategory("Scene Selection")]
public sealed class SceneSelectionServiceTests
{
    private static readonly DocumentId SceneDocument = DocumentId.New();

    [TestMethod]
    public void SetSelection_DeduplicatesNodesAndPreservesOrder()
    {
        var scene = CreateScene();
        var first = new SceneNode(scene, "First");
        var second = new SceneNode(scene, "Second");
        scene.RootNodes.Add(first);
        scene.RootNodes.Add(second);
        var sut = new SceneSelectionService();

        sut.SetSelection(SceneDocument, [NodeReference.To(first.Id), NodeReference.To(second.Id), NodeReference.To(first.Id)], "test");

        _ = sut.GetSelectedNodes(SceneDocument, scene).Should().Equal(first, second);
    }

    [TestMethod]
    public void Reconcile_RemovesNodesThatNoLongerBelongToScene()
    {
        var scene = CreateScene();
        var staleScene = CreateScene();
        var selected = new SceneNode(scene, "Selected");
        var stale = new SceneNode(staleScene, "Stale");
        scene.RootNodes.Add(selected);
        staleScene.RootNodes.Add(stale);
        var sut = new SceneSelectionService();
        sut.SetSelection(SceneDocument, [NodeReference.To(selected.Id), NodeReference.To(stale.Id)], "test");

        var reconciled = sut.Reconcile(SceneDocument, scene, []);

        _ = reconciled.Should().Equal(selected);
        _ = sut.GetSelectedNodes(SceneDocument, scene).Should().Equal(selected);
    }

    [TestMethod]
    public void Clear_RemovesDocumentSelection()
    {
        var scene = CreateScene();
        var selected = new SceneNode(scene, "Selected");
        scene.RootNodes.Add(selected);
        var sut = new SceneSelectionService();
        sut.SetSelection(SceneDocument, [NodeReference.To(selected.Id)], "test");

        sut.Clear(SceneDocument);

        _ = sut.GetSelectedNodes(SceneDocument, scene).Should().BeEmpty();
    }

    [TestMethod]
    public void Publish_StoresContextAndClearRemovesIt()
    {
        var documentId = DocumentId.New();
        var folderId = FolderId.New();
        var context = new SceneSelectionContext(SceneSelectionKind.Folder, [], [folderId], PrimaryNode: null, folderId);
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

        _ = sut.GetContext(DocumentId.New()).Should().Be(SceneSelectionContext.Empty);
    }

    [TestMethod]
    public void Publish_FiresSelectionChangedWithTheStoredContextAndSource()
    {
        var documentId = DocumentId.New();
        var folderId = FolderId.New();
        var context = new SceneSelectionContext(SceneSelectionKind.Folder, [], [folderId], PrimaryNode: null, folderId);
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
        var first = new SceneNode(scene, "First");
        var second = new SceneNode(scene, "Second");
        scene.RootNodes.Add(first);
        scene.RootNodes.Add(second);
        var sut = new SceneSelectionService();
        SceneSelectionChangedEventArgs? received = null;
        sut.SelectionChanged += (_, args) => received = args;

        sut.SetSelection(SceneDocument, [NodeReference.To(first.Id), NodeReference.To(second.Id)], "Command");

        _ = received!.Context.Kind.Should().Be(SceneSelectionKind.Node);
        _ = received.Context.OwnedPrimaryNodeId.Should().Be(second.Id);
        _ = sut.GetContext(SceneDocument).Should().Be(received.Context);
    }

    [TestMethod]
    public void Clear_FiresEmptyContextOnlyWhenAnEntryExisted()
    {
        var scene = CreateScene();
        var selected = new SceneNode(scene, "Selected");
        scene.RootNodes.Add(selected);
        var sut = new SceneSelectionService();
        var events = new List<SceneSelectionChangedEventArgs>();
        sut.SelectionChanged += (_, args) => events.Add(args);

        sut.Clear(SceneDocument);
        _ = events.Should().BeEmpty("an absent document has no selection to announce");

        sut.SetSelection(SceneDocument, [NodeReference.To(selected.Id)], "test");
        events.Clear();

        sut.Clear(SceneDocument);

        _ = events.Should().ContainSingle();
        _ = events[0].Context.Should().Be(SceneSelectionContext.Empty);
        _ = events[0].Source.Should().Be("Clear");
    }

    [TestMethod]
    public void Reconcile_PrunesMissingNodesKeepsFoldersAndReclassifies()
    {
        var scene = CreateScene();
        var staleScene = CreateScene();
        var selected = new SceneNode(scene, "Selected");
        var stale = new SceneNode(staleScene, "Stale");
        scene.RootNodes.Add(selected);
        var folderId = FolderId.New();
        IList<ExplorerEntryData> layout = [new ExplorerEntryData { Type = "Folder", FolderId = folderId }];
        var sut = new SceneSelectionService();
        sut.Publish(
            SceneDocument,
            new SceneSelectionContext(SceneSelectionKind.Mixed, [NodeReference.To(selected.Id), NodeReference.To(stale.Id)], [folderId], NodeReference.To(stale.Id), PrimaryFolderId: null),
            "SceneExplorer");

        var reconciled = sut.Reconcile(SceneDocument, scene, layout);

        _ = reconciled.Should().Equal(selected);
        var context = sut.GetContext(SceneDocument);
        _ = context.Kind.Should().Be(SceneSelectionKind.Mixed, "the surviving node and the folder still make a mixed batch");
        _ = context.OwnedNodeIds.Should().Equal(selected.Id);
        _ = context.OwnedPrimaryNodeId.Should().Be(selected.Id, "a pruned primary falls back to the first surviving node");
    }

    [TestMethod]
    public void Reconcile_WhenNothingSurvives_ClearsAndAnnounces()
    {
        var scene = CreateScene();
        var staleScene = CreateScene();
        var stale = new SceneNode(staleScene, "Stale");
        var sut = new SceneSelectionService();
        sut.SetSelection(SceneDocument, [NodeReference.To(stale.Id)], "test");
        var events = new List<SceneSelectionChangedEventArgs>();
        sut.SelectionChanged += (_, args) => events.Add(args);

        var reconciled = sut.Reconcile(SceneDocument, scene, []);

        _ = reconciled.Should().BeEmpty();
        _ = sut.GetContext(SceneDocument).Should().Be(SceneSelectionContext.Empty);
        _ = events.Should().ContainSingle();
        _ = events[0].Source.Should().Be("Clear");
    }

    [TestMethod]
    public void Context_CapturesIdentityCollectionsWithoutAliasingCallerLists()
    {
        var nodeId = NodeId.New();
        var folderId = FolderId.New();
        var nodes = new List<NodeReference> { NodeReference.To(nodeId) };
        var folders = new List<FolderId> { folderId };
        var context = new SceneSelectionContext(SceneSelectionKind.Mixed, nodes, folders, NodeReference.To(nodeId), PrimaryFolderId: null);

        nodes.Clear();
        folders.Clear();

        _ = context.OwnedNodeIds.Should().Equal(nodeId);
        _ = context.SelectedFolderIds.Should().Equal(folderId);
    }

    [TestMethod]
    public void Reconcile_SurvivingFolderPrimary_DoesNotInventNodePrimary()
    {
        var scene = CreateScene();
        var node = new SceneNode(scene, "Selected");
        scene.RootNodes.Add(node);
        var folderId = FolderId.New();
        IList<ExplorerEntryData> layout = [new ExplorerEntryData { Type = "Folder", FolderId = folderId }];
        var sut = new SceneSelectionService();
        var context = new SceneSelectionContext(SceneSelectionKind.Mixed, [NodeReference.To(node.Id)], [folderId], PrimaryNode: null, folderId);
        sut.Publish(SceneDocument, context, "test");

        _ = sut.Reconcile(SceneDocument, scene, layout);

        _ = sut.GetContext(SceneDocument).Should().BeSameAs(context, "an unchanged reconciliation preserves the snapshot");
        _ = sut.GetContext(SceneDocument).PrimaryNode.Should().BeNull();
        _ = sut.GetContext(SceneDocument).PrimaryFolderId.Should().Be(folderId);
    }

    [TestMethod]
    public void Reconcile_DeletedFolderPrimary_FallsBackToFirstSurvivingFolder()
    {
        var scene = CreateScene();
        var first = FolderId.New();
        var second = FolderId.New();
        var deleted = FolderId.New();
        IList<ExplorerEntryData> layout =
        [
            new ExplorerEntryData
            {
                Type = "Folder",
                FolderId = first,
                Children = [new ExplorerEntryData { Type = "Folder", FolderId = second }],
            },
        ];
        var sut = new SceneSelectionService();
        sut.Publish(SceneDocument, new SceneSelectionContext(SceneSelectionKind.Folder, [], [first, second, deleted], PrimaryNode: null, deleted), "test");

        _ = sut.Reconcile(SceneDocument, scene, layout);

        var context = sut.GetContext(SceneDocument);
        _ = context.SelectedFolderIds.Should().Equal(first, second);
        _ = context.PrimaryFolderId.Should().Be(first);
        _ = context.PrimaryNode.Should().BeNull();
    }

    [TestMethod]
    public void Reconcile_AllSelectedFoldersDeleted_ClearsSelection()
    {
        var scene = CreateScene();
        var folderId = FolderId.New();
        var sut = new SceneSelectionService();
        sut.Publish(SceneDocument, new SceneSelectionContext(SceneSelectionKind.Folder, [], [folderId], PrimaryNode: null, folderId), "test");

        _ = sut.Reconcile(SceneDocument, scene, []);

        _ = sut.GetContext(SceneDocument).Should().BeSameAs(SceneSelectionContext.Empty);
    }

    [TestMethod]
    public void Reconcile_DeletedNodePrimary_FallsBackToFirstNotLastSurvivor()
    {
        var scene = CreateScene();
        var first = new SceneNode(scene, "First");
        var second = new SceneNode(scene, "Second");
        scene.RootNodes.Add(first);
        scene.RootNodes.Add(second);
        var deletedId = NodeId.New();
        var sut = new SceneSelectionService();
        sut.Publish(SceneDocument, SceneSelectionContext.ForNodes([NodeReference.To(first.Id), NodeReference.To(second.Id), NodeReference.To(deletedId)]), "test");

        _ = sut.Reconcile(SceneDocument, scene, []);

        _ = sut.GetContext(SceneDocument).OwnedPrimaryNodeId.Should().Be(first.Id);
    }

    [TestMethod]
    public void SetSelection_HoldsInstancedNodesBesideOwnedNodes()
    {
        var scene = CreateScene();
        var (root, door) = AddInstance(scene);
        var owned = new SceneNode(scene, "Owned");
        scene.RootNodes.Add(owned);
        var sut = new SceneSelectionService();

        sut.SetSelection(SceneDocument, [NodeReference.To(owned.Id), door], "test");

        var context = sut.GetContext(SceneDocument);
        _ = context.SelectedNodes.Should().Equal(NodeReference.To(owned.Id), door);
        _ = context.PrimaryNode.Should().Be(door);
        _ = context.OwnedNodeIds.Should().Equal([owned.Id], "the instanced node does not select its instance root");
        _ = context.OwnedPrimaryNodeId.Should().BeNull();
        _ = sut.GetSelectedNodes(SceneDocument, scene).Should().Equal([owned], "an instanced node has no scene node");
        _ = sut.GetSelectedNodes(SceneDocument, scene).Should().NotContain(root);
    }

    [TestMethod]
    public void Reconcile_KeepsAnInstancedNodeWhileItsInstanceRootIsInTheScene()
    {
        var scene = CreateScene();
        var (root, door) = AddInstance(scene);
        var sut = new SceneSelectionService();
        sut.SetSelection(SceneDocument, [door], "test");

        _ = sut.Reconcile(SceneDocument, scene, []);

        _ = sut.GetContext(SceneDocument).SelectedNodes.Should().Equal(door);

        _ = scene.RootNodes.Remove(root);
        _ = sut.Reconcile(SceneDocument, scene, []);

        _ = sut.GetContext(SceneDocument).Should().BeSameAs(SceneSelectionContext.Empty, "a removed instance takes its nodes' selection with it");
    }

    [TestMethod]
    public void Reconcile_DropsAPathBelowANodeThatIsNoInstanceRoot()
    {
        var scene = CreateScene();
        var plain = new SceneNode(scene, "Plain");
        scene.RootNodes.Add(plain);
        var sut = new SceneSelectionService();
        sut.SetSelection(SceneDocument, [new NodeReference(plain.Id, NodePath.Of(NodeId.New()))], "test");

        _ = sut.Reconcile(SceneDocument, scene, []);

        _ = sut.GetContext(SceneDocument).Should().BeSameAs(SceneSelectionContext.Empty);
    }

    [TestMethod]
    public void ForNodes_DeduplicatesAndMakesTheLastNodePrimary()
    {
        var first = NodeReference.To(NodeId.New());
        var second = new NodeReference(NodeId.New(), NodePath.Of(NodeId.New()));

        var context = SceneSelectionContext.ForNodes([first, second, first]);

        _ = context.Kind.Should().Be(SceneSelectionKind.Node);
        _ = context.SelectedNodes.Should().Equal(first, second);
        _ = context.PrimaryNode.Should().Be(second);
        _ = SceneSelectionContext.ForNodes([]).Should().BeSameAs(SceneSelectionContext.Empty);
    }

    /// <summary>Adds an instance root, <c>Car</c>, and a reference to its instanced node <c>Door_FL</c>.</summary>
    private static (SceneNode Root, NodeReference Door) AddInstance(Scene scene)
    {
        var root = new SceneNode(scene, "Car");
        _ = root.AddComponent(new SceneInstanceComponent { Name = "Scene Instance", Source = AssetPath.Parse("/Content/Vehicles/Car.glb") });
        scene.RootNodes.Add(root);
        return (root, new NodeReference(root.Id, NodePath.Of(NodeId.New())));
    }

    private static Scene CreateScene() => new() { Name = "Test Scene" };
}
