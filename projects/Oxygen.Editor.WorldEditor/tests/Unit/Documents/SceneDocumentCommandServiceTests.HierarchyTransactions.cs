// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using System.Text.Json;
using AwesomeAssertions;
using Moq;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Documents;

public sealed partial class SceneDocumentCommandServiceTests
{
    private static readonly JsonSerializerOptions HierarchyJsonOptions = new() { IncludeFields = true };

    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task DeleteItemsAsync_MixedBatchWithStaleItem_RejectsWithoutChangingGraphLayoutOrHistory(bool staleNode)
    {
        var fixture = CreateFixture();
        ConfigureHierarchySync(fixture);
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Node" };
        var child = new SceneNode(scene) { Name = "Child" };
        node.AddChild(child);
        scene.RootNodes.Add(node);
        var folderId = Guid.NewGuid();
        scene.SetExplorerLayout(
            [
                new ExplorerEntryData
                {
                    Type = "Folder",
                    FolderId = folderId,
                    Name = "Group",
                    IsExpanded = false,
                    Children = [new ExplorerEntryData { NodeId = node.Id }],
                },
            ]);
        var context = CreateContext(scene);
        var before = SerializeHierarchy(scene);

        var result = await fixture.Sut.DeleteItemsAsync(
            context,
            staleNode ? [node.Id, Guid.NewGuid()] : [node.Id],
            staleNode ? [folderId] : [folderId, Guid.NewGuid()]).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = SerializeHierarchy(scene).Should().Be(before);
        _ = node.Children.Should().ContainSingle().Which.Should().BeSameAs(child);
        _ = context.Metadata.IsDirty.Should().BeFalse();
        _ = context.History.UndoStack.Should().BeEmpty();
        _ = context.History.RedoStack.Should().BeEmpty();
        fixture.Sync.Verify(
            sync => sync.RemoveNodeHierarchiesAsync(It.IsAny<Scene>(), It.IsAny<IReadOnlyList<Guid>>()),
            Times.Never);
        fixture.DocumentService.Verify(
            service => service.UpdateMetadataAsync(It.IsAny<Microsoft.UI.WindowId>(), context.DocumentId, context.Metadata),
            Times.Never);
    }

    [TestMethod]
    public async Task DuplicateNodesAsync_MultipleRoots_UndoRedoRestoresWholeBatchWithOneHistoryEntry()
    {
        var fixture = CreateFixture();
        ConfigureHierarchySync(fixture);
        var scene = CreateScene();
        var first = new SceneNode(scene) { Name = "First" };
        var child = new SceneNode(scene) { Name = "Child" };
        first.AddChild(child);
        var second = new SceneNode(scene) { Name = "Second" };
        scene.RootNodes.Add(first);
        scene.RootNodes.Add(second);
        scene.SetExplorerLayout([new ExplorerEntryData { NodeId = second.Id }, new ExplorerEntryData { NodeId = first.Id }]);
        var context = CreateContext(scene);
        var before = SerializeHierarchy(scene);

        var result = await fixture.Sut.DuplicateNodesAsync(context, [first.Id, second.Id], null, null).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = result.Value.Should().HaveCount(2);
        _ = result.Value!.Select(node => node.Name).Should().Equal("First", "Second");
        _ = scene.RootNodes.Should().HaveCount(4);
        _ = context.History.UndoStack.Should().ContainSingle();
        var after = SerializeHierarchy(scene);
        var cloneIds = result.Value.SelectMany(node => node.Descendants().Prepend(node)).Select(node => node.Id).ToArray();
        _ = cloneIds.Should().NotIntersectWith(new[] { first.Id, child.Id, second.Id });

        await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = SerializeHierarchy(scene).Should().Be(before);
        _ = context.History.UndoStack.Should().BeEmpty();
        _ = context.History.RedoStack.Should().ContainSingle();

        await context.History.RedoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = SerializeHierarchy(scene).Should().Be(after, "redo restores the same two clone hierarchies, not another duplication");
        _ = scene.AllNodes.Select(node => node.Id).Should().Contain(cloneIds);
        _ = context.History.UndoStack.Should().ContainSingle();
        _ = context.History.RedoStack.Should().BeEmpty();
    }

    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task ReparentNodesAsync_History_RestoresExactLayoutOrderAndTrsAtSyncBoundary(bool ignoreParentTransform)
    {
        var fixture = CreateFixture();
        ConfigureHierarchySync(fixture);
        var (scene, source, destination, moved) = CreateReparentTransactionScene(ignoreParentTransform);
        var beforeSibling = source.Children[0];
        var afterSibling = source.Children[2];
        var anchor = destination.Children[0];
        var tail = destination.Children[1];
        var context = CreateContext(scene);
        var before = SerializeHierarchy(scene);
        var oldTransform = ReadTransactionTransform(moved);
        var world = SceneTransformMath.WorldMatrix(moved);
        var synchronized = CaptureTransactionSync(fixture, scene, moved);

        var result = await fixture.Sut.ReparentNodesAsync(context, [moved.Id], null, true, anchor.Id).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = destination.Children.Should().Equal(anchor, moved, tail);
        _ = source.Children.Should().Equal(beforeSibling, afterSibling);
        _ = MatricesClose(SceneTransformMath.WorldMatrix(moved), world).Should().BeTrue();
        _ = context.History.UndoStack.Should().ContainSingle();
        var after = SerializeHierarchy(scene);
        var newTransform = ReadTransactionTransform(moved);
        if (ignoreParentTransform)
        {
            _ = newTransform.Should().Be(oldTransform, "ignored-parent reparenting must retain every local TRS value exactly");
        }

        await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = SerializeHierarchy(scene).Should().Be(before);
        _ = context.History.RedoStack.Should().ContainSingle();

        await context.History.RedoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = SerializeHierarchy(scene).Should().Be(after);
        _ = context.History.UndoStack.Should().ContainSingle();
        _ = synchronized.Should().Equal(
            (destination.Id, newTransform),
            (source.Id, oldTransform),
            (destination.Id, newTransform));
        fixture.Sync.Verify(sync => sync.UpdateNodeTransformAsync(scene, moved, It.IsAny<CancellationToken>()), Times.Exactly(3));
    }

    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task MoveNodesToFolderAsync_History_RestoresExactLayoutOrderAndTrsAtSyncBoundary(bool crossScope)
    {
        var fixture = CreateFixture();
        ConfigureHierarchySync(fixture);
        var (scene, scope, moved, destinationFolderId) = CreateGroupingTransactionScene(crossScope);
        var context = CreateContext(scene);
        var before = SerializeHierarchy(scene);
        var transform = ReadTransactionTransform(moved);
        var synchronized = CaptureTransactionSync(fixture, scene, moved);

        var result = await fixture.Sut.MoveNodesToFolderAsync(context, [moved.Id], destinationFolderId).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = moved.Parent.Should().BeSameAs(crossScope ? scope : null);
        _ = ReadTransactionTransform(moved).Should().Be(transform, "grouping preserves local TRS even across node scopes");
        _ = FindFolderEntry(scene.ExplorerLayout, destinationFolderId)!.Children
            .Should().ContainSingle().Which.NodeId.Should().Be(moved.Id);
        _ = context.History.UndoStack.Should().ContainSingle();
        var after = SerializeHierarchy(scene);

        await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = SerializeHierarchy(scene).Should().Be(before);
        _ = context.History.RedoStack.Should().ContainSingle();

        await context.History.RedoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = SerializeHierarchy(scene).Should().Be(after);
        _ = context.History.UndoStack.Should().ContainSingle();
        if (crossScope)
        {
            _ = synchronized.Should().Equal((scope.Id, transform), (null, transform), (scope.Id, transform));
        }
        else
        {
            _ = synchronized.Should().BeEmpty("same-scope grouping is editor layout only");
        }

        fixture.Sync.Verify(
            sync => sync.UpdateNodeTransformAsync(scene, moved, It.IsAny<CancellationToken>()),
            crossScope ? Times.Exactly(3) : Times.Never());
    }

    [TestMethod]
    public async Task DuplicateNodesFromDataAsync_RepeatedPaste_UsesIndependentSnapshotAndDestinationHistory()
    {
        var fixture = CreateFixture();
        ConfigureHierarchySync(fixture);
        var sourceScene = CreateScene();
        var source = new SceneNode(sourceScene) { Name = "Snapshot" };
        var child = new SceneNode(sourceScene) { Name = "Hidden child" };
        SetTransactionTransform(source);
        source.AddChild(child);
        sourceScene.RootNodes.Add(source);
        var sourceContext = CreateContext(sourceScene);
        var snapshot = source.Dehydrate();
        var snapshotBefore = JsonSerializer.Serialize(snapshot, HierarchyJsonOptions);
        source.Name = "Changed after Copy";
        child.Name = "Changed child";
        source.Components.OfType<TransformComponent>().Single().LocalPosition = Vector3.Zero;
        var sourceBefore = SerializeHierarchy(sourceScene);
        var destination = CreateScene();
        var context = CreateContext(destination);

        var first = await fixture.Sut.DuplicateNodesFromDataAsync(context, [snapshot], null, null).ConfigureAwait(false);
        var second = await fixture.Sut.DuplicateNodesFromDataAsync(context, [snapshot], null, null).ConfigureAwait(false);

        _ = first.Succeeded.Should().BeTrue();
        _ = second.Succeeded.Should().BeTrue();
        _ = destination.RootNodes.Select(node => node.Name).Should().Equal("Snapshot", "Snapshot");
        _ = destination.RootNodes.Select(node => node.Children.Single().Name).Should().Equal("Hidden child", "Hidden child");
        var firstClone = first.Value!.Single();
        var secondClone = second.Value!.Single();
        AssertPasteIsolationAfterEditingFirstClone(source, child, firstClone, secondClone);
        _ = context.History.UndoStack.Should().HaveCount(2);
        _ = sourceContext.History.UndoStack.Should().BeEmpty();
        _ = sourceContext.Metadata.IsDirty.Should().BeFalse();
        _ = SerializeHierarchy(sourceScene).Should().Be(sourceBefore);
        _ = JsonSerializer.Serialize(snapshot, HierarchyJsonOptions).Should().Be(snapshotBefore);

        await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = destination.RootNodes.Should().ContainSingle().Which.Should().BeSameAs(firstClone);
        await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = destination.RootNodes.Should().BeEmpty();
        await context.History.RedoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        await context.History.RedoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = destination.RootNodes.Should().Equal(firstClone, secondClone);
        _ = JsonSerializer.Serialize(snapshot, HierarchyJsonOptions).Should().Be(snapshotBefore);
    }

    private static (Scene scene, SceneNode source, SceneNode destination, SceneNode moved) CreateReparentTransactionScene(bool ignoreParentTransform)
    {
        var scene = CreateScene();
        var source = new SceneNode(scene) { Name = "Source" };
        var destination = new SceneNode(scene) { Name = "Destination" };
        source.Components.OfType<TransformComponent>().Single().LocalPosition = new Vector3(10f, 2f, 3f);
        var destinationTransform = destination.Components.OfType<TransformComponent>().Single();
        destinationTransform.LocalPosition = new Vector3(-3f, 5f, 7f);
        destinationTransform.LocalRotation = Quaternion.CreateFromYawPitchRoll(0.2f, 0.3f, 0.4f);
        destinationTransform.LocalScale = new Vector3(2f);
        scene.RootNodes.Add(source);
        scene.RootNodes.Add(destination);
        var beforeSibling = new SceneNode(scene) { Name = "Before" };
        var moved = new SceneNode(scene) { Name = "Moved", IgnoreParentTransform = ignoreParentTransform };
        var afterSibling = new SceneNode(scene) { Name = "After" };
        source.AddChild(beforeSibling);
        source.AddChild(moved);
        source.AddChild(afterSibling);
        var anchor = new SceneNode(scene) { Name = "Anchor" };
        var tail = new SceneNode(scene) { Name = "Tail" };
        destination.AddChild(anchor);
        destination.AddChild(tail);
        SetTransactionTransform(moved);
        scene.SetExplorerLayout(
            [
                new ExplorerEntryData
                {
                    NodeId = source.Id,
                    Children =
                    [
                        new ExplorerEntryData { NodeId = beforeSibling.Id },
                        new ExplorerEntryData
                        {
                            Type = "Folder",
                            FolderId = Guid.NewGuid(),
                            Name = "Group",
                            IsExpanded = false,
                            Children = [new ExplorerEntryData { NodeId = moved.Id }],
                        },
                        new ExplorerEntryData { NodeId = afterSibling.Id },
                    ],
                },
                new ExplorerEntryData
                {
                    NodeId = destination.Id,
                    Children = [new ExplorerEntryData { NodeId = anchor.Id }, new ExplorerEntryData { NodeId = tail.Id }],
                },
            ]);
        return (scene, source, destination, moved);
    }

    private static (Scene scene, SceneNode scope, SceneNode moved, Guid destinationFolderId) CreateGroupingTransactionScene(bool crossScope)
    {
        var scene = CreateScene();
        var scope = new SceneNode(scene) { Name = "Scope" };
        scope.Components.OfType<TransformComponent>().Single().LocalPosition = new Vector3(10f, 20f, 30f);
        var beforeSibling = new SceneNode(scene) { Name = "Before" };
        var moved = new SceneNode(scene) { Name = "Moved" };
        var afterSibling = new SceneNode(scene) { Name = "After" };
        scene.RootNodes.Add(beforeSibling);
        scene.RootNodes.Add(moved);
        scene.RootNodes.Add(afterSibling);
        scene.RootNodes.Add(scope);
        SetTransactionTransform(moved);
        var sourceFolderId = Guid.NewGuid();
        var destinationFolderId = Guid.NewGuid();
        var destinationFolder = new ExplorerEntryData
        {
            Type = "Folder",
            FolderId = destinationFolderId,
            Name = "Destination",
            Children = [],
        };
        scene.SetExplorerLayout(
            [
                new ExplorerEntryData { NodeId = beforeSibling.Id },
                new ExplorerEntryData
                {
                    Type = "Folder",
                    FolderId = sourceFolderId,
                    Name = "Source",
                    IsExpanded = false,
                    Children = [new ExplorerEntryData { NodeId = moved.Id }],
                },
                new ExplorerEntryData { NodeId = afterSibling.Id },
                new ExplorerEntryData { NodeId = scope.Id, Children = crossScope ? [destinationFolder] : [] },
            ]);
        if (!crossScope)
        {
            scene.ExplorerLayout!.Add(destinationFolder);
        }

        return (scene, scope, moved, destinationFolderId);
    }

    private static void AssertPasteIsolationAfterEditingFirstClone(SceneNode source, SceneNode child, SceneNode firstClone, SceneNode secondClone)
    {
        _ = firstClone.Id.Should().NotBe(secondClone.Id).And.NotBe(source.Id);
        _ = firstClone.Children[0].Id.Should().NotBe(secondClone.Children[0].Id).And.NotBe(child.Id);
        _ = firstClone.Components.Select(component => component.Id).Should()
            .NotIntersectWith(secondClone.Components.Concat(source.Components).Select(component => component.Id));
        _ = firstClone.Components.OfType<TransformComponent>().Single().LocalPosition.Should().Be(new Vector3(1f, 2f, 3f));
        firstClone.Components.OfType<TransformComponent>().Single().LocalPosition = Vector3.Zero;
        firstClone.Children[0].Name = "Edited first paste";
        _ = secondClone.Components.OfType<TransformComponent>().Single().LocalPosition.Should().Be(new Vector3(1f, 2f, 3f));
        _ = secondClone.Children[0].Name.Should().Be("Hidden child");
    }

    private static string SerializeHierarchy(Scene scene)
        => JsonSerializer.Serialize(scene.Dehydrate(), HierarchyJsonOptions);

    private static TransformData ReadTransactionTransform(SceneNode node)
        => (TransformData)node.Components.OfType<TransformComponent>().Single().Dehydrate();

    private static void SetTransactionTransform(SceneNode node)
    {
        var transform = node.Components.OfType<TransformComponent>().Single();
        transform.LocalPosition = new Vector3(1f, 2f, 3f);
        transform.LocalRotation = Quaternion.CreateFromYawPitchRoll(0.4f, 0.2f, 0.6f);
        transform.LocalScale = new Vector3(1f, 2f, 3f);
    }

    private static List<(Guid? parentId, TransformData transform)> CaptureTransactionSync(Fixture fixture, Scene scene, SceneNode node)
    {
        var synchronized = new List<(Guid? parentId, TransformData transform)>();
        TransformData? published = null;
        _ = fixture.Sync.Setup(sync => sync.UpdateNodeTransformAsync(scene, node, It.IsAny<CancellationToken>()))
            .Callback<Scene, SceneNode, CancellationToken>((_, value, _) => published = ReadTransactionTransform(value))
            .ReturnsAsync(new SyncOutcome(SyncStatus.Accepted, SceneOperationKinds.EditTransform, AffectedScope.Empty));
        _ = fixture.Sync.Setup(sync => sync.ReparentHierarchiesAsync(scene, It.IsAny<IReadOnlyList<Guid>>(), It.IsAny<Guid?>(), false))
            .Callback<Scene, IReadOnlyList<Guid>, Guid?, bool>((_, ids, parentId, _) =>
            {
                _ = ids.Should().Equal(node.Id);
                _ = published.Should().NotBeNull();
                synchronized.Add((parentId, published!));
            })
            .Returns(Task.CompletedTask);
        _ = fixture.Sync.Setup(sync => sync.ReparentNodeAsync(scene, node.Id, It.IsAny<Guid?>(), false))
            .Callback<Scene, Guid, Guid?, bool>((_, _, parentId, _) =>
            {
                _ = published.Should().NotBeNull();
                synchronized.Add((parentId, published!));
            })
            .Returns(Task.CompletedTask);
        return synchronized;
    }
}
