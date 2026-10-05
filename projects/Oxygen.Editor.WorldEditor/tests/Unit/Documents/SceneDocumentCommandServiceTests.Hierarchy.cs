// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using Moq;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Editor.World.SceneExplorer.Operations;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Documents;

/// <summary>Identity-based hierarchy authoring commands on the scene document command owner.</summary>
public sealed partial class SceneDocumentCommandServiceTests
{
    private const float MatrixTolerance = 1e-3f;

    [TestMethod]
    public async Task RenameNodeAsync_WhenStaleNodeId_RejectsWithoutDirtying()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Cube" };
        scene.RootNodes.Add(node);
        var context = CreateContext(scene);

        var result = await fixture.Sut.RenameNodeAsync(context, Guid.NewGuid(), "Renamed").ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = node.Name.Should().Be("Cube");
        _ = context.Metadata.IsDirty.Should().BeFalse();
        _ = context.History.UndoStack.Should().BeEmpty();
    }

    [TestMethod]
    public async Task RenameNodeAsync_WhenCommitted_RecordsSingleUndoStep()
    {
        var (interaction, _) = CreateInteraction();
        var scene = CreateScene();
        await interaction.RestoreAsync(ProjectContext.FromProjectInfo(SlotTestProjectInfo), scene.Id).ConfigureAwait(false);
        var fixture = CreateFixture(interaction: interaction);
        ConfigureHierarchySync(fixture);
        var node = new SceneNode(scene) { Name = "Cube" };
        scene.RootNodes.Add(node);
        var context = CreateContext(scene);

        var result = await fixture.Sut.RenameNodeAsync(context, node.Id, "Sphere").ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = node.Name.Should().Be("Sphere");
        _ = context.Metadata.IsDirty.Should().BeTrue();
        _ = context.History.UndoStack.Should().ContainSingle();
        interaction.SetLocked(node.Id, isLocked: true);

        var rejected = await fixture.Sut.RenameNodeAsync(context, node.Id, "Blocked").ConfigureAwait(false);
        _ = rejected.Succeeded.Should().BeFalse();
        _ = node.Name.Should().Be("Sphere");
        fixture.Sync.Verify(sync => sync.RenameNodeAsync(scene, node.Id, "Sphere"), Times.Once);

        await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = node.Name.Should().Be("Cube");
        _ = context.History.RedoStack.Should().ContainSingle();

        await context.History.RedoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = node.Name.Should().Be("Sphere");
    }

    [TestMethod]
    public async Task RenameNodeAsync_WhenLiveSyncRejects_PublishesOperationResultAndSucceeds()
    {
        var fixture = CreateFixture();
        ConfigureHierarchySync(fixture);
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Cube" };
        scene.RootNodes.Add(node);
        var context = CreateContext(scene);
        _ = fixture.Sync
            .Setup(sync => sync.RenameNodeAsync(scene, node.Id, "Sphere"))
            .ReturnsAsync(new SyncOutcome(
                SyncStatus.Rejected,
                SceneOperationKinds.NodeRename,
                AffectedScope.Empty,
                LiveSyncDiagnosticCodes.RenameRejected,
                "The runtime rejected the rename."));

        var result = await fixture.Sut.RenameNodeAsync(context, node.Id, "Sphere").ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue("the authoring rename commits even when the live preview rejects it");
        _ = node.Name.Should().Be("Sphere");
        _ = result.OperationResultId.Should().NotBeNull("a rejected live-sync rename must publish an operation result");
        var published = fixture.Results.Published.Should().ContainSingle().Which;
        _ = published.OperationId.Should().Be(result.OperationResultId!.Value);
        _ = published.OperationKind.Should().Be(SceneOperationKinds.NodeRename);
        _ = published.Diagnostics.Should().ContainSingle()
            .Which.Code.Should().Be(LiveSyncDiagnosticCodes.RenameRejected);
    }

    [TestMethod]
    public async Task CreateNodeAsync_AtRoot_CreatesNodeAndUndoRemovesIt()
    {
        var fixture = CreateFixture();
        ConfigureHierarchySync(fixture);
        var scene = CreateScene();
        var context = CreateContext(scene);

        var result = await fixture.Sut.CreateNodeAsync(context, parentNodeId: null, parentFolderId: null, "New Node").ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = scene.RootNodes.Should().ContainSingle().Which.Name.Should().Be("New Node");
        _ = context.Metadata.IsDirty.Should().BeTrue();
        _ = context.History.UndoStack.Should().ContainSingle();

        await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = scene.RootNodes.Should().BeEmpty();
    }

    [TestMethod]
    public async Task CreateFolderAsync_WhenParentFolderIsStale_RejectsWithStaleTargetWithoutSeedingLayout()
    {
        var fixture = CreateFixture();
        ConfigureHierarchySync(fixture);
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Node" };
        scene.RootNodes.Add(node);
        var context = CreateContext(scene);
        _ = scene.ExplorerLayout.Should().BeNull();

        var result = await fixture.Sut.CreateFolderAsync(context, parentFolderId: Guid.NewGuid(), parentNodeId: null, "Folder").ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = result.OperationResultId.Should().NotBeNull("a user-triggered folder creation failure must publish an operation result");
        var published = fixture.Results.Published.Should().ContainSingle().Which;
        _ = published.OperationId.Should().Be(result.OperationResultId!.Value);
        _ = published.OperationKind.Should().Be(SceneOperationKinds.ExplorerFolderCreate);
        _ = published.Diagnostics.Should().ContainSingle()
            .Which.Code.Should().Be(DiagnosticCodes.ScenePrefix + "STALE_TARGET");
        _ = scene.ExplorerLayout.Should().BeNull("a rejected folder creation must not leave an uncommitted seeded layout behind");
        _ = context.Metadata.IsDirty.Should().BeFalse();
        _ = context.History.UndoStack.Should().BeEmpty();
    }

    [TestMethod]
    public async Task CreateFolderAsync_WhenParentNodeIsStale_RejectsWithStaleTargetWithoutSeedingLayout()
    {
        var fixture = CreateFixture();
        ConfigureHierarchySync(fixture);
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Node" };
        scene.RootNodes.Add(node);
        var context = CreateContext(scene);
        _ = scene.ExplorerLayout.Should().BeNull();

        var result = await fixture.Sut.CreateFolderAsync(context, parentFolderId: null, parentNodeId: Guid.NewGuid(), "Folder").ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = result.OperationResultId.Should().NotBeNull("a user-triggered folder creation failure must publish an operation result");
        var published = fixture.Results.Published.Should().ContainSingle().Which;
        _ = published.OperationId.Should().Be(result.OperationResultId!.Value);
        _ = published.OperationKind.Should().Be(SceneOperationKinds.ExplorerFolderCreate);
        _ = published.Diagnostics.Should().ContainSingle()
            .Which.Code.Should().Be(DiagnosticCodes.ScenePrefix + "STALE_TARGET");
        _ = scene.ExplorerLayout.Should().BeNull("a rejected folder creation must not leave an uncommitted seeded layout behind");
        _ = context.Metadata.IsDirty.Should().BeFalse();
        _ = context.History.UndoStack.Should().BeEmpty();
    }

    [TestMethod]
    public async Task CreateFolderAsync_WhenParentNodeIsLocked_RejectsWithoutSeedingLayout()
    {
        var scene = CreateScene();
        var parent = new SceneNode(scene) { Name = "Locked Parent" };
        scene.RootNodes.Add(parent);
        var context = CreateContext(scene);
        var (interaction, _) = CreateInteraction();
        await interaction.RestoreAsync(ProjectContext.FromProjectInfo(SlotTestProjectInfo), scene.Id).ConfigureAwait(false);
        interaction.SetLocked(parent.Id, isLocked: true);
        var fixture = CreateFixture(interaction: interaction);

        var result = await fixture.Sut.CreateFolderAsync(context, parentFolderId: null, parentNodeId: parent.Id, "Folder").ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = scene.ExplorerLayout.Should().BeNull();
        _ = context.Metadata.IsDirty.Should().BeFalse();
        _ = context.History.UndoStack.Should().BeEmpty();
        _ = fixture.Results.Published.Should().ContainSingle()
            .Which.Diagnostics.Should().ContainSingle()
            .Which.Code.Should().Be(DiagnosticCodes.ScenePrefix + "NODE_LOCKED");
    }

    [TestMethod]
    public async Task DeleteNodesAsync_DeletesSubtreeAndUndoRestoresExactHierarchy()
    {
        var fixture = CreateFixture();
        ConfigureHierarchySync(fixture);
        var scene = CreateScene();
        var root = new SceneNode(scene) { Name = "Root" };
        var child = new SceneNode(scene) { Name = "Child" };
        root.AddChild(child);
        scene.RootNodes.Add(root);
        var sibling = new SceneNode(scene) { Name = "Sibling" };
        scene.RootNodes.Add(sibling);
        var context = CreateContext(scene);

        var result = await fixture.Sut.DeleteNodesAsync(context, [root.Id]).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = scene.RootNodes.Should().ContainSingle().Which.Should().BeSameAs(sibling);
        _ = scene.AllNodes.Should().NotContain(root);
        _ = context.Metadata.IsDirty.Should().BeTrue();

        await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = scene.RootNodes.Should().Contain(root);
        _ = root.Children.Should().ContainSingle().Which.Name.Should().Be("Child");
        _ = scene.RootNodes[0].Should().BeSameAs(root, "restored order places the deleted root at its former index");
    }

    [TestMethod]
    public async Task DeleteNodesAsync_WhenDescendantIsLocked_RejectsTheWholeSubtree()
    {
        var scene = CreateScene();
        var root = new SceneNode(scene) { Name = "Root" };
        var child = new SceneNode(scene) { Name = "Locked Child" };
        root.AddChild(child);
        scene.RootNodes.Add(root);
        var context = CreateContext(scene);
        var (interaction, _) = CreateInteraction();
        await interaction.RestoreAsync(ProjectContext.FromProjectInfo(SlotTestProjectInfo), scene.Id).ConfigureAwait(false);
        interaction.SetLocked(child.Id, isLocked: true);
        var fixture = CreateFixture(interaction: interaction);

        var result = await fixture.Sut.DeleteNodesAsync(context, [root.Id]).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = scene.RootNodes.Should().ContainSingle().Which.Should().BeSameAs(root);
        _ = root.Children.Should().ContainSingle().Which.Should().BeSameAs(child);
        _ = context.Metadata.IsDirty.Should().BeFalse();
        _ = context.History.UndoStack.Should().BeEmpty();
        _ = fixture.Results.Published.Should().ContainSingle()
            .Which.Diagnostics.Should().ContainSingle()
            .Which.Code.Should().Be(DiagnosticCodes.ScenePrefix + "NODE_LOCKED");
    }

    [TestMethod]
    public async Task ReparentNodesAsync_WhenDestinationIsLocked_RejectsWithoutMovingNode()
    {
        var scene = CreateScene();
        var source = new SceneNode(scene) { Name = "Source" };
        var destination = new SceneNode(scene) { Name = "Locked Destination" };
        scene.RootNodes.Add(source);
        scene.RootNodes.Add(destination);
        var context = CreateContext(scene);
        var (interaction, _) = CreateInteraction();
        await interaction.RestoreAsync(ProjectContext.FromProjectInfo(SlotTestProjectInfo), scene.Id).ConfigureAwait(false);
        interaction.SetLocked(destination.Id, isLocked: true);
        var fixture = CreateFixture(interaction: interaction);

        var result = await fixture.Sut.ReparentNodesAsync(
            context,
            [source.Id],
            destination.Id,
            preserveWorldTransform: false).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = source.Parent.Should().BeNull();
        _ = destination.Children.Should().BeEmpty();
        _ = context.Metadata.IsDirty.Should().BeFalse();
        _ = context.History.UndoStack.Should().BeEmpty();
        _ = fixture.Results.Published.Should().ContainSingle()
            .Which.Diagnostics.Should().ContainSingle()
            .Which.Code.Should().Be(DiagnosticCodes.ScenePrefix + "NODE_LOCKED");
    }

    [TestMethod]
    public async Task DeleteFolderAsync_PromotesContainedEntriesWithoutRemovingNodes()
    {
        var fixture = CreateFixture();
        ConfigureHierarchySync(fixture);
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Grouped" };
        scene.RootNodes.Add(node);
        var context = CreateContext(scene);

        var folder = await fixture.Sut.CreateFolderAsync(context, parentFolderId: null, parentNodeId: null, "Folder").ConfigureAwait(false);
        var group = await fixture.Sut.MoveNodesToFolderAsync(context, [node.Id], folder.Value!).ConfigureAwait(false);
        _ = group.Succeeded.Should().BeTrue();

        var result = await fixture.Sut.DeleteFolderAsync(context, folder.Value!).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = scene.RootNodes.Should().ContainSingle().Which.Should().BeSameAs(node);
        _ = scene.AllNodes.Should().ContainSingle();

        await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = scene.ExplorerLayout.Should().NotBeNull();
        _ = scene.AllNodes.Should().ContainSingle();
    }

    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task DeleteFolderAsync_WhenContainedNodeIsLocked_AllowsPromotionAtAnyFolderDepth(bool nested)
    {
        var (interaction, _) = CreateInteraction();
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Locked grouped node" };
        scene.RootNodes.Add(node);
        await interaction.RestoreAsync(ProjectContext.FromProjectInfo(SlotTestProjectInfo), scene.Id).ConfigureAwait(false);
        var fixture = CreateFixture(interaction: interaction);
        ConfigureHierarchySync(fixture);
        var context = CreateContext(scene);
        var outer = await fixture.Sut.CreateFolderAsync(context, parentFolderId: null, parentNodeId: null, "Outer").ConfigureAwait(false);
        var targetFolderId = outer.Value!;
        if (nested)
        {
            var inner = await fixture.Sut.CreateFolderAsync(context, parentFolderId: outer.Value, parentNodeId: null, "Inner").ConfigureAwait(false);
            targetFolderId = inner.Value!;
        }

        _ = (await fixture.Sut.MoveNodesToFolderAsync(context, [node.Id], targetFolderId).ConfigureAwait(false)).Succeeded.Should().BeTrue();
        interaction.SetLocked(node.Id, isLocked: true);
        var result = await fixture.Sut.DeleteFolderAsync(context, outer.Value!).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue("folder removal does not edit the locked scene node");
        _ = scene.AllNodes.Should().ContainSingle().Which.Should().BeSameAs(node);
        _ = FindFolderEntry(scene.ExplorerLayout, outer.Value).Should().BeNull();
        if (nested)
        {
            _ = FindFolderEntry(scene.ExplorerLayout, targetFolderId)!.Children
                .Should().Contain(entry => entry.NodeId == node.Id);
        }
        else
        {
            _ = scene.ExplorerLayout.Should().Contain(entry => entry.NodeId == node.Id);
        }
    }

    [TestMethod]
    public async Task DeleteItemsAsync_MixedBatchWithLockedNodeInsideFolder_DeletesItemsAndPromotesLockedNode()
    {
        var (interaction, _) = CreateInteraction();
        var scene = CreateScene();
        var lockedNode = new SceneNode(scene) { Name = "Locked grouped node" };
        var selectedNode = new SceneNode(scene) { Name = "Selected node" };
        scene.RootNodes.Add(lockedNode);
        scene.RootNodes.Add(selectedNode);
        await interaction.RestoreAsync(ProjectContext.FromProjectInfo(SlotTestProjectInfo), scene.Id).ConfigureAwait(false);
        var fixture = CreateFixture(interaction: interaction);
        ConfigureHierarchySync(fixture);
        var context = CreateContext(scene);
        var folder = await fixture.Sut.CreateFolderAsync(context, parentFolderId: null, parentNodeId: null, "Folder").ConfigureAwait(false);
        _ = (await fixture.Sut.MoveNodesToFolderAsync(context, [lockedNode.Id], folder.Value!).ConfigureAwait(false)).Succeeded.Should().BeTrue();
        interaction.SetLocked(lockedNode.Id, isLocked: true);
        var result = await fixture.Sut.DeleteItemsAsync(context, [selectedNode.Id], [folder.Value!]).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = scene.AllNodes.Should().ContainSingle().Which.Should().BeSameAs(lockedNode);
        _ = FindFolderEntry(scene.ExplorerLayout, folder.Value).Should().BeNull();
        _ = scene.ExplorerLayout.Should().Contain(entry => entry.NodeId == lockedNode.Id);
    }

    [TestMethod]
    public async Task DeleteFolderAsync_NestedFolders_PromotesEntriesWithoutRemovingNodes()
    {
        var fixture = CreateFixture();
        ConfigureHierarchySync(fixture);
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Grouped" };
        scene.RootNodes.Add(node);
        var context = CreateContext(scene);
        var outer = await fixture.Sut.CreateFolderAsync(context, parentFolderId: null, parentNodeId: null, "Outer").ConfigureAwait(false);
        var inner = await fixture.Sut.CreateFolderAsync(context, parentFolderId: outer.Value, parentNodeId: null, "Inner").ConfigureAwait(false);
        _ = (await fixture.Sut.MoveNodesToFolderAsync(context, [node.Id], inner.Value!).ConfigureAwait(false)).Succeeded.Should().BeTrue();

        var deleteInner = await fixture.Sut.DeleteFolderAsync(context, inner.Value!).ConfigureAwait(false);

        _ = deleteInner.Succeeded.Should().BeTrue();
        _ = scene.RootNodes.Should().ContainSingle().Which.Should().BeSameAs(node);
        _ = FindFolderEntry(scene.ExplorerLayout, inner.Value).Should().BeNull("the inner folder is removed");
        _ = scene.ExplorerLayout!.Single(entry => entry.FolderId == outer.Value).Children
            .Should().Contain(entry => entry.NodeId == node.Id, "deleting the inner folder promotes its entries into the outer folder's position");
        var undoSteps = context.History.UndoStack.Count;

        await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var restoredInner = FindFolderEntry(scene.ExplorerLayout, inner.Value);
        _ = restoredInner.Should().NotBeNull("one undo step restores the deleted folder");
        _ = restoredInner!.Children.Should().Contain(entry => entry.NodeId == node.Id);

        var deleteOuter = await fixture.Sut.DeleteFolderAsync(context, outer.Value!).ConfigureAwait(false);

        _ = deleteOuter.Succeeded.Should().BeTrue();
        _ = scene.RootNodes.Should().ContainSingle().Which.Should().BeSameAs(node);
        _ = FindFolderEntry(scene.ExplorerLayout, outer.Value).Should().BeNull("the outer folder is removed");
        var promotedInner = scene.ExplorerLayout!.Single(entry => entry.FolderId == inner.Value);
        _ = promotedInner.Children.Should().Contain(entry => entry.NodeId == node.Id, "deleting the outer folder promotes the inner folder with its children");
        _ = context.History.UndoStack.Should().HaveCount(undoSteps, "each folder delete is exactly one undo step");

        await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = FindFolderEntry(scene.ExplorerLayout, outer.Value).Should().NotBeNull();
        _ = scene.AllNodes.Should().ContainSingle("folder deletes never remove nodes");
    }

    [TestMethod]
    public async Task DeleteItemsAsync_MixedBatch_PromotesNestedFolderEntriesAndDeletesNodesAsOneUndoStep()
    {
        var fixture = CreateFixture();
        ConfigureHierarchySync(fixture);
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Grouped" };
        var doomed = new SceneNode(scene) { Name = "Doomed" };
        scene.RootNodes.Add(node);
        scene.RootNodes.Add(doomed);
        var context = CreateContext(scene);
        var outer = await fixture.Sut.CreateFolderAsync(context, parentFolderId: null, parentNodeId: null, "Outer").ConfigureAwait(false);
        var inner = await fixture.Sut.CreateFolderAsync(context, parentFolderId: outer.Value, parentNodeId: null, "Inner").ConfigureAwait(false);
        _ = (await fixture.Sut.MoveNodesToFolderAsync(context, [node.Id], inner.Value!).ConfigureAwait(false)).Succeeded.Should().BeTrue();
        var undoSteps = context.History.UndoStack.Count;

        var first = await fixture.Sut.DeleteItemsAsync(context, [doomed.Id], [inner.Value!]).ConfigureAwait(false);

        _ = first.Succeeded.Should().BeTrue();
        _ = scene.RootNodes.Should().ContainSingle().Which.Should().BeSameAs(node);
        _ = FindFolderEntry(scene.ExplorerLayout, inner.Value).Should().BeNull();
        _ = scene.ExplorerLayout!.Single(entry => entry.FolderId == outer.Value).Children
            .Should().Contain(entry => entry.NodeId == node.Id, "the mixed batch promotes the inner folder's entries into the outer folder");
        _ = context.History.UndoStack.Should().HaveCount(undoSteps + 1, "the mixed batch is exactly one undo step");

        await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = scene.RootNodes.Should().HaveCount(2, "one undo step restores the deleted node");
        _ = FindFolderEntry(scene.ExplorerLayout, inner.Value).Should().NotBeNull("one undo step restores the deleted folder");

        var second = await fixture.Sut.DeleteItemsAsync(context, [doomed.Id], [outer.Value!]).ConfigureAwait(false);

        _ = second.Succeeded.Should().BeTrue();
        _ = scene.RootNodes.Should().ContainSingle().Which.Should().BeSameAs(node);
        _ = FindFolderEntry(scene.ExplorerLayout, outer.Value).Should().BeNull();
        var promotedInner = scene.ExplorerLayout!.Single(entry => entry.FolderId == inner.Value);
        _ = promotedInner.Children.Should().Contain(entry => entry.NodeId == node.Id, "the mixed batch promotes the inner folder with its children");
        _ = context.History.UndoStack.Should().HaveCount(undoSteps + 1, "the mixed batch is exactly one undo step");

        await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = scene.AllNodes.Should().HaveCount(2);
        _ = FindFolderEntry(scene.ExplorerLayout, outer.Value).Should().NotBeNull();
        _ = FindFolderEntry(scene.ExplorerLayout, inner.Value).Should().NotBeNull();
    }

    [TestMethod]
    public async Task DeleteFolderAsync_AfterCrossScopeGrouping_KeepsNodeParentAndWorldPose()
    {
        var fixture = CreateFixture();
        ConfigureHierarchySync(fixture);
        var scene = CreateScene();
        var scopeParent = new SceneNode(scene) { Name = "Scope" };
        scopeParent.Components.OfType<TransformComponent>().Single().LocalPosition = new Vector3(10f, 0f, 0f);
        scene.RootNodes.Add(scopeParent);
        var node = new SceneNode(scene) { Name = "Grouped" };
        var transform = node.Components.OfType<TransformComponent>().Single();
        transform.LocalPosition = new Vector3(1f, 2f, 3f);
        scene.RootNodes.Add(node);
        var context = CreateContext(scene);
        var folder = await fixture.Sut.CreateFolderAsync(context, parentFolderId: null, parentNodeId: scopeParent.Id, "Scoped").ConfigureAwait(false);

        // Grouping into a folder scoped under another node performs the D2 cross-scope reparent.
        _ = (await fixture.Sut.MoveNodesToFolderAsync(context, [node.Id], folder.Value!).ConfigureAwait(false)).Succeeded.Should().BeTrue();
        _ = node.Parent.Should().BeSameAs(scopeParent);
        var worldBefore = SceneTransformMath.WorldMatrix(node);
        var undoSteps = context.History.UndoStack.Count;

        var result = await fixture.Sut.DeleteFolderAsync(context, folder.Value!).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = scene.AllNodes.Should().HaveCount(2, "folder deletion is grouping-only and removes no nodes");
        _ = node.Parent.Should().BeSameAs(scopeParent, "promotion must not reparent the grouped node");
        _ = transform.LocalPosition.Should().Be(new Vector3(1f, 2f, 3f), "promotion must not touch local transforms");
        _ = MatricesClose(SceneTransformMath.WorldMatrix(node), worldBefore).Should().BeTrue("promotion must preserve the world pose");
        _ = scene.ExplorerLayout!.Single(entry => entry.NodeId == scopeParent.Id).Children
            .Should().Contain(entry => entry.NodeId == node.Id, "the node entry is promoted into the folder's former position");
        _ = FindFolderEntry(scene.ExplorerLayout, folder.Value).Should().BeNull();
        _ = context.History.UndoStack.Should().HaveCount(undoSteps + 1, "the folder delete is exactly one undo step");

        await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = FindFolderEntry(scene.ExplorerLayout, folder.Value).Should().NotBeNull();
        _ = node.Parent.Should().BeSameAs(scopeParent);
        _ = MatricesClose(SceneTransformMath.WorldMatrix(node), worldBefore).Should().BeTrue();
    }

    [TestMethod]
    public async Task DeleteItemsAsync_MixedBatch_AfterCrossScopeGrouping_KeepsNodeParentAndWorldPose()
    {
        var fixture = CreateFixture();
        ConfigureHierarchySync(fixture);
        var scene = CreateScene();
        var scopeParent = new SceneNode(scene) { Name = "Scope" };
        scopeParent.Components.OfType<TransformComponent>().Single().LocalPosition = new Vector3(10f, 0f, 0f);
        scene.RootNodes.Add(scopeParent);
        var node = new SceneNode(scene) { Name = "Grouped" };
        var transform = node.Components.OfType<TransformComponent>().Single();
        transform.LocalPosition = new Vector3(1f, 2f, 3f);
        scene.RootNodes.Add(node);
        var doomed = new SceneNode(scene) { Name = "Doomed" };
        scene.RootNodes.Add(doomed);
        var context = CreateContext(scene);
        var folder = await fixture.Sut.CreateFolderAsync(context, parentFolderId: null, parentNodeId: scopeParent.Id, "Scoped").ConfigureAwait(false);

        // Grouping into a folder scoped under another node performs the D2 cross-scope reparent.
        _ = (await fixture.Sut.MoveNodesToFolderAsync(context, [node.Id], folder.Value!).ConfigureAwait(false)).Succeeded.Should().BeTrue();
        _ = node.Parent.Should().BeSameAs(scopeParent);
        var worldBefore = SceneTransformMath.WorldMatrix(node);
        var undoSteps = context.History.UndoStack.Count;

        var result = await fixture.Sut.DeleteItemsAsync(context, [doomed.Id], [folder.Value!]).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = scene.AllNodes.Should().HaveCount(2, "only the selected node is deleted");
        _ = scene.AllNodes.Should().NotContain(doomed);
        _ = node.Parent.Should().BeSameAs(scopeParent, "promotion must not reparent the grouped node");
        _ = transform.LocalPosition.Should().Be(new Vector3(1f, 2f, 3f), "promotion must not touch local transforms");
        _ = MatricesClose(SceneTransformMath.WorldMatrix(node), worldBefore).Should().BeTrue("promotion must preserve the world pose");
        _ = scene.ExplorerLayout!.Single(entry => entry.NodeId == scopeParent.Id).Children
            .Should().Contain(entry => entry.NodeId == node.Id, "the node entry is promoted into the folder's former position");
        _ = FindFolderEntry(scene.ExplorerLayout, folder.Value).Should().BeNull();
        _ = context.History.UndoStack.Should().HaveCount(undoSteps + 1, "the mixed batch is exactly one undo step");

        await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = scene.AllNodes.Should().HaveCount(3, "one undo step restores the deleted node");
        _ = FindFolderEntry(scene.ExplorerLayout, folder.Value).Should().NotBeNull();
        _ = node.Parent.Should().BeSameAs(scopeParent);
        _ = MatricesClose(SceneTransformMath.WorldMatrix(node), worldBefore).Should().BeTrue();
    }

    [TestMethod]
    public async Task DeleteFolderAsync_WhenFolderIdIsStale_RejectsWithStaleTargetValidation()
    {
        var fixture = CreateFixture();
        ConfigureHierarchySync(fixture);
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Grouped" };
        scene.RootNodes.Add(node);
        var context = CreateContext(scene);
        var folder = await fixture.Sut.CreateFolderAsync(context, parentFolderId: null, parentNodeId: null, "Folder").ConfigureAwait(false);
        _ = (await fixture.Sut.DeleteFolderAsync(context, folder.Value!).ConfigureAwait(false)).Succeeded.Should().BeTrue();
        var undoSteps = context.History.UndoStack.Count;

        var stale = await fixture.Sut.DeleteFolderAsync(context, folder.Value!).ConfigureAwait(false);

        _ = stale.Succeeded.Should().BeFalse();
        _ = stale.ValidationCode.Should().Be(DiagnosticCodes.ScenePrefix + "STALE_TARGET");
        _ = scene.AllNodes.Should().ContainSingle();
        _ = context.History.UndoStack.Should().HaveCount(undoSteps, "a rejected delete records no undo step");
    }

    [TestMethod]
    public async Task DeleteItemsAsync_WhenFolderIsStale_LeavesExplorerLayoutUnseeded()
    {
        var fixture = CreateFixture();
        ConfigureHierarchySync(fixture);
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Node" };
        scene.RootNodes.Add(node);
        var context = CreateContext(scene);
        _ = scene.ExplorerLayout.Should().BeNull();

        var result = await fixture.Sut.DeleteItemsAsync(context, [node.Id], [Guid.NewGuid()]).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = result.ValidationCode.Should().Be(DiagnosticCodes.ScenePrefix + "STALE_TARGET");
        _ = scene.ExplorerLayout.Should().BeNull("a rejected delete must leave the layout exactly as it was");
        _ = scene.RootNodes.Should().ContainSingle().Which.Should().BeSameAs(node);
        _ = context.Metadata.IsDirty.Should().BeFalse();
        _ = context.History.UndoStack.Should().BeEmpty();
    }

    [TestMethod]
    public async Task DeleteNodesAsync_Undo_RestoresGraphAndExplorerLayout()
    {
        var fixture = CreateFixture();
        ConfigureHierarchySync(fixture);
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Node" };
        scene.RootNodes.Add(node);
        var context = CreateContext(scene);

        _ = (await fixture.Sut.DeleteNodesAsync(context, [node.Id]).ConfigureAwait(false)).Succeeded.Should().BeTrue();
        _ = context.History.UndoStack.Should().ContainSingle();

        await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = scene.RootNodes.Should().Contain(node);
        _ = scene.ExplorerLayout.Should().Contain(entry => entry.NodeId == node.Id, "undo of the delete transaction restores graph and layout together");
    }

    [TestMethod]
    public async Task ReparentNodesAsync_PreserveWorld_KeepsWorldPoseUnderRotatedScaledParent()
    {
        var fixture = CreateFixture();
        ConfigureHierarchySync(fixture);
        var scene = CreateScene();
        var parent = new SceneNode(scene) { Name = "Parent" };
        var parentTransform = parent.Components.OfType<TransformComponent>().Single();
        parentTransform.LocalPosition = new Vector3(1f, 2f, 3f);
        parentTransform.LocalRotation = Quaternion.CreateFromYawPitchRoll(0.3f, 0.5f, 0.2f);
        parentTransform.LocalScale = new Vector3(2f, 2f, 2f);
        scene.RootNodes.Add(parent);

        var child = new SceneNode(scene) { Name = "Child" };
        var childTransform = child.Components.OfType<TransformComponent>().Single();
        childTransform.LocalPosition = new Vector3(4f, 5f, 6f);
        childTransform.LocalRotation = Quaternion.CreateFromYawPitchRoll(0.1f, 0.4f, 0.9f);
        childTransform.LocalScale = new Vector3(1f, 2f, 3f);
        parent.AddChild(child);
        var context = CreateContext(scene);

        var before = SceneTransformMath.WorldMatrix(child);

        var result = await fixture.Sut.ReparentNodesAsync(context, [child.Id], newParentNodeId: null, preserveWorldTransform: true).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = child.Parent.Should().BeNull();
        var after = SceneTransformMath.WorldMatrix(child);
        _ = MatricesClose(after, before).Should().BeTrue("preserve-world reparent must retain the world pose");

        await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = child.Parent.Should().BeSameAs(parent);
        _ = childTransform.LocalPosition.Should().Be(new Vector3(4f, 5f, 6f));

        await context.History.RedoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = child.Parent.Should().BeNull();
        var afterRedo = SceneTransformMath.WorldMatrix(child);
        _ = MatricesClose(afterRedo, before).Should().BeTrue("redo must re-apply the preserve-world move");
    }

    [TestMethod]
    public async Task ReparentNodesAsync_IntoOwnDescendant_RejectsAtomically()
    {
        var fixture = CreateFixture();
        ConfigureHierarchySync(fixture);
        var scene = CreateScene();
        var parent = new SceneNode(scene) { Name = "Parent" };
        var child = new SceneNode(scene) { Name = "Child" };
        parent.AddChild(child);
        scene.RootNodes.Add(parent);
        var context = CreateContext(scene);

        var result = await fixture.Sut.ReparentNodesAsync(context, [parent.Id], child.Id, preserveWorldTransform: false).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = parent.Parent.Should().BeNull();
        _ = child.Parent.Should().BeSameAs(parent);
        _ = context.Metadata.IsDirty.Should().BeFalse();
        _ = context.History.UndoStack.Should().BeEmpty();
    }

    [TestMethod]
    public async Task ReparentNodesAsync_WhenTransformSyncRejects_PublishesOutcomeAndThreadsOperationId()
    {
        var fixture = CreateFixture();
        ConfigureHierarchySync(fixture);
        var scene = CreateScene();
        var parent = new SceneNode(scene) { Name = "Parent" };
        var child = new SceneNode(scene) { Name = "Child" };
        parent.AddChild(child);
        scene.RootNodes.Add(parent);
        var context = CreateContext(scene);
        _ = fixture.Sync
            .Setup(sync => sync.UpdateNodeTransformAsync(scene, child, It.IsAny<CancellationToken>()))
            .ReturnsAsync(new SyncOutcome(
                SyncStatus.Rejected,
                SceneOperationKinds.EditTransform,
                AffectedScope.Empty,
                LiveSyncDiagnosticCodes.TransformRejected,
                "The runtime rejected the transform."));

        var result = await fixture.Sut.ReparentNodesAsync(context, [child.Id], newParentNodeId: null, preserveWorldTransform: false).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue("the authoring reparent commits even when the live preview rejects the transform push");
        _ = child.Parent.Should().BeNull();
        _ = result.OperationResultId.Should().NotBeNull("a rejected transform push must publish an operation result");
        var published = fixture.Results.Published.Should().ContainSingle().Which;
        _ = published.OperationId.Should().Be(result.OperationResultId!.Value);
        _ = published.OperationKind.Should().Be(SceneOperationKinds.NodeReparent);
        _ = published.Diagnostics.Should().ContainSingle()
            .Which.Code.Should().Be(LiveSyncDiagnosticCodes.TransformRejected);
    }

    [TestMethod]
    public async Task ReparentUndoRedo_WhenTransformSyncRejects_PublishesOutcomesFromHistoryDelegates()
    {
        var fixture = CreateFixture();
        ConfigureHierarchySync(fixture);
        var scene = CreateScene();
        var parent = new SceneNode(scene) { Name = "Parent" };
        var child = new SceneNode(scene) { Name = "Child" };
        parent.AddChild(child);
        scene.RootNodes.Add(parent);
        var context = CreateContext(scene);

        _ = (await fixture.Sut.ReparentNodesAsync(context, [child.Id], newParentNodeId: null, preserveWorldTransform: false).ConfigureAwait(false)).Succeeded.Should().BeTrue();
        _ = fixture.Results.Published.Should().BeEmpty("the committed reparent synced cleanly");

        // The runtime starts rejecting transform pushes only after the commit; undo and redo must surface that.
        _ = fixture.Sync
            .Setup(sync => sync.UpdateNodeTransformAsync(scene, child, It.IsAny<CancellationToken>()))
            .ReturnsAsync(new SyncOutcome(
                SyncStatus.Rejected,
                SceneOperationKinds.EditTransform,
                AffectedScope.Empty,
                LiveSyncDiagnosticCodes.TransformRejected,
                "The runtime rejected the transform."));

        await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = child.Parent.Should().BeSameAs(parent);
        _ = fixture.Results.Published.Should().ContainSingle("undo publishes through the operations channel without a return path");
        _ = fixture.Results.Published[0].OperationKind.Should().Be(SceneOperationKinds.NodeReparent);
        _ = fixture.Results.Published[0].Diagnostics.Should().ContainSingle()
            .Which.Code.Should().Be(LiveSyncDiagnosticCodes.TransformRejected);

        await context.History.RedoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = child.Parent.Should().BeNull();
        _ = fixture.Results.Published.Should().HaveCount(2, "redo publishes its own rejected transform outcome");
        _ = fixture.Results.Published[1].OperationKind.Should().Be(SceneOperationKinds.NodeReparent);
        _ = fixture.Results.Published[1].Diagnostics.Should().ContainSingle()
            .Which.Code.Should().Be(LiveSyncDiagnosticCodes.TransformRejected);
    }

    [TestMethod]
    public async Task MoveNodesToFolderAsync_IsGroupingOnlyAndDoesNotChangeParentOrTransform()
    {
        var fixture = CreateFixture();
        ConfigureHierarchySync(fixture);
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Node" };
        var transform = node.Components.OfType<TransformComponent>().Single();
        transform.LocalPosition = new Vector3(9f, 8f, 7f);
        scene.RootNodes.Add(node);
        var context = CreateContext(scene);

        var folder = await fixture.Sut.CreateFolderAsync(context, parentFolderId: null, parentNodeId: null, "Folder").ConfigureAwait(false);
        var beforeParent = node.Parent;
        var beforePosition = transform.LocalPosition;

        var result = await fixture.Sut.MoveNodesToFolderAsync(context, [node.Id], folder.Value!).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = node.Parent.Should().BeSameAs(beforeParent, "grouping must not reparent");
        _ = transform.LocalPosition.Should().Be(beforePosition, "grouping must not change transforms");
        _ = scene.ExplorerLayout.Should().NotBeNull();
    }

    [TestMethod]
    public async Task MoveNodesToFolderAsync_WhenCrossScopeTransformSyncRejects_PublishesOutcomeAndThreadsOperationId()
    {
        var fixture = CreateFixture();
        ConfigureHierarchySync(fixture);
        var scene = CreateScene();
        var scopeParent = new SceneNode(scene) { Name = "Scope" };
        scene.RootNodes.Add(scopeParent);
        var node = new SceneNode(scene) { Name = "Grouped" };
        scene.RootNodes.Add(node);
        var context = CreateContext(scene);
        var folder = await fixture.Sut.CreateFolderAsync(context, parentFolderId: null, parentNodeId: scopeParent.Id, "Scoped").ConfigureAwait(false);
        _ = folder.Succeeded.Should().BeTrue();
        _ = fixture.Sync
            .Setup(sync => sync.UpdateNodeTransformAsync(scene, node, It.IsAny<CancellationToken>()))
            .ReturnsAsync(new SyncOutcome(
                SyncStatus.Rejected,
                SceneOperationKinds.EditTransform,
                AffectedScope.Empty,
                LiveSyncDiagnosticCodes.TransformRejected,
                "The runtime rejected the transform."));

        var result = await fixture.Sut.MoveNodesToFolderAsync(context, [node.Id], folder.Value!).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue("the authoring grouping commits even when the live preview rejects the transform push");
        _ = node.Parent.Should().BeSameAs(scopeParent, "cross-scope grouping reparents in the authoring model");
        _ = result.OperationResultId.Should().NotBeNull("a rejected transform push must publish an operation result");
        var published = fixture.Results.Published.Should().ContainSingle().Which;
        _ = published.OperationId.Should().Be(result.OperationResultId!.Value);
        _ = published.OperationKind.Should().Be(SceneOperationKinds.ExplorerLayoutMoveNode);
        _ = published.Diagnostics.Should().ContainSingle()
            .Which.Code.Should().Be(LiveSyncDiagnosticCodes.TransformRejected);
    }

    [TestMethod]
    public async Task RemoveNodesFromFolderAsync_PromotesNodeOutOfFolder()
    {
        var fixture = CreateFixture();
        ConfigureHierarchySync(fixture);
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Node" };
        scene.RootNodes.Add(node);
        var context = CreateContext(scene);

        var folder = await fixture.Sut.CreateFolderAsync(context, parentFolderId: null, parentNodeId: null, "Folder").ConfigureAwait(false);
        _ = await fixture.Sut.MoveNodesToFolderAsync(context, [node.Id], folder.Value!).ConfigureAwait(false);

        var result = await fixture.Sut.RemoveNodesFromFolderAsync(context, [node.Id], folder.Value!).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = node.Parent.Should().BeNull();
        _ = scene.RootNodes.Should().ContainSingle();
    }

    [TestMethod]
    public void SceneTransformMath_WorldMatrix_HonoursIgnoreParentTransform()
    {
        var scene = CreateScene();
        var parent = new SceneNode(scene) { Name = "Parent" };
        parent.Components.OfType<TransformComponent>().Single().LocalPosition = new Vector3(10f, 0f, 0f);
        scene.RootNodes.Add(parent);
        var child = new SceneNode(scene) { Name = "Child", IgnoreParentTransform = true };
        parent.AddChild(child);

        var world = SceneTransformMath.WorldMatrix(child);

        _ = world.Translation.Should().Be(Vector3.Zero, "an ignored-parent child has world == local");
    }

    [TestMethod]
    public async Task ReparentNodesAsync_PreserveWorld_IgnoreParentTransformKeepsLocalAndWorld()
    {
        var fixture = CreateFixture();
        ConfigureHierarchySync(fixture);
        var scene = CreateScene();
        var parent = new SceneNode(scene) { Name = "Parent" };
        parent.Components.OfType<TransformComponent>().Single().LocalPosition = new Vector3(10f, 0f, 0f);
        scene.RootNodes.Add(parent);

        var child = new SceneNode(scene) { Name = "Child", IgnoreParentTransform = true };
        var childTransform = child.Components.OfType<TransformComponent>().Single();
        childTransform.LocalPosition = new Vector3(1f, 2f, 3f);
        parent.AddChild(child);

        var newParent = new SceneNode(scene) { Name = "NewParent" };
        newParent.Components.OfType<TransformComponent>().Single().LocalPosition = new Vector3(20f, 0f, 0f);
        scene.RootNodes.Add(newParent);

        var context = CreateContext(scene);
        var before = SceneTransformMath.WorldMatrix(child);

        var result = await fixture.Sut.ReparentNodesAsync(context, [child.Id], newParent.Id, preserveWorldTransform: true).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = child.Parent.Should().BeSameAs(newParent);
        _ = childTransform.LocalPosition.Should().Be(new Vector3(1f, 2f, 3f), "an ignored-parent node keeps its local TRS unchanged");
        _ = MatricesClose(SceneTransformMath.WorldMatrix(child), before).Should().BeTrue("an ignored-parent node must keep its world pose when reparented");
    }

    [TestMethod]
    public async Task DuplicateNodesAsync_DeepCopiesSubtreeWithIndependentIdentities()
    {
        var fixture = CreateFixture();
        ConfigureHierarchySync(fixture);
        var scene = CreateScene();
        var source = new SceneNode(scene) { Name = "Source" };
        var child = new SceneNode(scene) { Name = "Child" };
        source.AddChild(child);
        scene.RootNodes.Add(source);
        var context = CreateContext(scene);

        var result = await fixture.Sut.DuplicateNodesAsync(context, [source.Id], newParentNodeId: null, newParentFolderId: null).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = scene.RootNodes.Should().HaveCount(2);
        var clone = scene.RootNodes.Single(node => !ReferenceEquals(node, source));
        _ = clone.Id.Should().NotBe(source.Id);
        _ = clone.Name.Should().Be("Source");
        _ = clone.Children.Should().ContainSingle().Which.Name.Should().Be("Child");
        _ = clone.Children[0].Id.Should().NotBe(child.Id);
        _ = clone.Components.Select(component => component.Id).Should().NotIntersectWith(source.Components.Select(component => component.Id));
        _ = context.Metadata.IsDirty.Should().BeTrue();
        _ = context.History.UndoStack.Should().ContainSingle();
    }

    [TestMethod]
    public async Task DuplicateNodesAsync_ResetsCopiedDirectionalLightAtmosphereSlot()
    {
        var fixture = CreateFixture();
        ConfigureHierarchySync(fixture);
        var scene = CreateScene();
        var source = new SceneNode(scene) { Name = "Sun" };
        _ = source.AddComponent(new DirectionalLightComponent { Name = "Directional Light", AtmosphereSlot = AtmosphereLightSlot.Primary });
        scene.RootNodes.Add(source);
        var context = CreateContext(scene);

        var result = await fixture.Sut.DuplicateNodesAsync(context, [source.Id], newParentNodeId: null, newParentFolderId: null).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        var clone = scene.RootNodes.Single(node => !ReferenceEquals(node, source));
        _ = clone.Components.OfType<DirectionalLightComponent>().Single().AtmosphereSlot.Should().Be(AtmosphereLightSlot.None, "duplication must not steal the source's atmosphere role");
    }

    [TestMethod]
    public async Task DuplicateNodesAsync_SyncsEachNodeInSubtree()
    {
        var fixture = CreateFixture();
        ConfigureHierarchySync(fixture);
        var scene = CreateScene();
        var source = new SceneNode(scene) { Name = "Source" };
        var child = new SceneNode(scene) { Name = "Child" };
        source.AddChild(child);
        scene.RootNodes.Add(source);
        var context = CreateContext(scene);

        var result = await fixture.Sut.DuplicateNodesAsync(context, [source.Id], newParentNodeId: null, newParentFolderId: null).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        fixture.Sync.Verify(sync => sync.CreateNodeAsync(It.IsAny<SceneNode>(), It.IsAny<Guid?>()), Times.Exactly(2));
    }

    [TestMethod]
    public async Task DuplicateNodesAsync_StaleRoot_RejectsAtomicallyWithoutPartialCommit()
    {
        var fixture = CreateFixture();
        ConfigureHierarchySync(fixture);
        var scene = CreateScene();
        var a = new SceneNode(scene) { Name = "A" };
        var b = new SceneNode(scene) { Name = "B" };
        scene.RootNodes.Add(a);
        scene.RootNodes.Add(b);
        var context = CreateContext(scene);

        var result = await fixture.Sut.DuplicateNodesAsync(context, [a.Id, Guid.NewGuid()], newParentNodeId: null, newParentFolderId: null).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = scene.RootNodes.Should().HaveCount(2, "no clone may be committed when any source is stale");
        _ = context.History.UndoStack.Should().BeEmpty();
        _ = context.Metadata.IsDirty.Should().BeFalse();
    }

    [TestMethod]
    public async Task DuplicateNodesAsync_WhenSourceIsStale_PublishesFailureOperationResult()
    {
        var fixture = CreateFixture();
        ConfigureHierarchySync(fixture);
        var scene = CreateScene();
        var context = CreateContext(scene);

        var result = await fixture.Sut.DuplicateNodesAsync(context, [Guid.NewGuid()], newParentNodeId: null, newParentFolderId: null).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = result.OperationResultId.Should().NotBeNull("a user-triggered duplication failure must publish an operation result");
        var published = fixture.Results.Published.Should().ContainSingle().Which;
        _ = published.OperationId.Should().Be(result.OperationResultId!.Value);
        _ = published.OperationKind.Should().Be(SceneOperationKinds.NodeDuplicate);
        _ = published.Diagnostics.Should().ContainSingle()
            .Which.Code.Should().Be(DiagnosticCodes.ScenePrefix + "STALE_TARGET");
        _ = scene.RootNodes.Should().BeEmpty("a rejected duplication commits nothing");
        _ = context.History.UndoStack.Should().BeEmpty();
    }

    [TestMethod]
    public async Task DuplicateNodesFromDataAsync_WhenTargetParentIsStale_PublishesFailureOperationResult()
    {
        var fixture = CreateFixture();
        ConfigureHierarchySync(fixture);
        var scene = CreateScene();
        var source = new SceneNode(scene) { Name = "Source" };
        scene.RootNodes.Add(source);
        var context = CreateContext(scene);

        var result = await fixture.Sut.DuplicateNodesFromDataAsync(context, [source.Dehydrate()], Guid.NewGuid(), newParentFolderId: null).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = result.OperationResultId.Should().NotBeNull("a user-triggered paste failure must publish an operation result");
        var published = fixture.Results.Published.Should().ContainSingle().Which;
        _ = published.OperationId.Should().Be(result.OperationResultId!.Value);
        _ = published.OperationKind.Should().Be(SceneOperationKinds.NodeDuplicate);
        _ = published.Diagnostics.Should().ContainSingle()
            .Which.Code.Should().Be(DiagnosticCodes.ScenePrefix + "STALE_TARGET");
        _ = scene.RootNodes.Should().ContainSingle("a rejected paste commits nothing");
        _ = context.History.UndoStack.Should().BeEmpty();
    }

    [TestMethod]
    public async Task DeleteNodesAsync_Undo_RecreatesFullSubtreeInNative()
    {
        var fixture = CreateFixture();
        ConfigureHierarchySync(fixture);
        var scene = CreateScene();
        var source = new SceneNode(scene) { Name = "Source" };
        var child = new SceneNode(scene) { Name = "Child" };
        source.AddChild(child);
        scene.RootNodes.Add(source);
        var context = CreateContext(scene);

        _ = await fixture.Sut.DeleteNodesAsync(context, [source.Id]).ConfigureAwait(false);
        await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = scene.RootNodes.Should().ContainSingle().Which.Children.Should().ContainSingle();
        fixture.Sync.Verify(sync => sync.CreateNodeAsync(It.IsAny<SceneNode>(), It.IsAny<Guid?>()), Times.Exactly(2));
    }

    [TestMethod]
    public async Task ReorderNodesAsync_ReordersSiblingWithinRoot()
    {
        var fixture = CreateFixture();
        ConfigureHierarchySync(fixture);
        var scene = CreateScene();
        var a = new SceneNode(scene) { Name = "A" };
        var b = new SceneNode(scene) { Name = "B" };
        var c = new SceneNode(scene) { Name = "C" };
        scene.RootNodes.Add(a);
        scene.RootNodes.Add(b);
        scene.RootNodes.Add(c);
        var context = CreateContext(scene);

        // Move A to after C: insertion index 3 in the original [A, B, C] list.
        var result = await fixture.Sut.ReorderNodesAsync(context, a.Id, parentFolderId: null, parentNodeId: null, index: 3).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = scene.ExplorerLayout!.Select(entry => entry.NodeId).Should().Equal(b.Id, c.Id, a.Id);
        _ = context.Metadata.IsDirty.Should().BeTrue();
    }

    [TestMethod]
    public async Task ReorderNodesAsync_WhenDescendantIsLocked_AllowsReorderingTheParentRow()
    {
        var (interaction, _) = CreateInteraction();
        var scene = CreateScene();
        var parent = new SceneNode(scene) { Name = "Parent" };
        var child = new SceneNode(scene) { Name = "Locked child" };
        var sibling = new SceneNode(scene) { Name = "Sibling" };
        parent.AddChild(child);
        scene.RootNodes.Add(parent);
        scene.RootNodes.Add(sibling);
        await interaction.RestoreAsync(ProjectContext.FromProjectInfo(SlotTestProjectInfo), scene.Id).ConfigureAwait(false);
        interaction.SetLocked(child.Id, isLocked: true);
        var fixture = CreateFixture(interaction: interaction);
        ConfigureHierarchySync(fixture);
        var context = CreateContext(scene);

        var result = await fixture.Sut.ReorderNodesAsync(context, parent.Id, parentFolderId: null, parentNodeId: null, index: 2).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue("changing the parent's Explorer row position does not edit its locked child");
        _ = scene.ExplorerLayout!.Select(entry => entry.NodeId).Should().Equal(sibling.Id, parent.Id);
        _ = parent.Children.Should().ContainSingle().Which.Should().BeSameAs(child);
    }

    [TestMethod]
    public async Task MoveFolderToParentAsync_MovesFolderUnderAnotherFolder()
    {
        var fixture = CreateFixture();
        ConfigureHierarchySync(fixture);
        var scene = CreateScene();
        scene.RootNodes.Add(new SceneNode(scene) { Name = "Node" });
        var context = CreateContext(scene);

        var first = await fixture.Sut.CreateFolderAsync(context, parentFolderId: null, parentNodeId: null, "First").ConfigureAwait(false);
        var second = await fixture.Sut.CreateFolderAsync(context, parentFolderId: null, parentNodeId: null, "Second").ConfigureAwait(false);

        var result = await fixture.Sut.MoveFolderToParentAsync(context, second.Value!, first.Value).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        var firstEntry = scene.ExplorerLayout!.Single(entry => entry.FolderId == first.Value);
        _ = firstEntry.Children.Should().Contain(entry => entry.FolderId == second.Value);
        _ = scene.ExplorerLayout.Should().NotContain(entry => entry.FolderId == second.Value);
    }

    private static void ConfigureHierarchySync(Fixture fixture)
    {
        _ = fixture.Sync.Setup(sync => sync.CreateNodeAsync(It.IsAny<SceneNode>(), It.IsAny<Guid?>())).Returns(Task.CompletedTask);
        _ = fixture.Sync.Setup(sync => sync.RemoveNodeHierarchiesAsync(It.IsAny<Scene>(), It.IsAny<IReadOnlyList<Guid>>())).Returns(Task.CompletedTask);
        _ = fixture.Sync.Setup(sync => sync.ReparentNodeAsync(It.IsAny<Scene>(), It.IsAny<Guid>(), It.IsAny<Guid?>(), It.IsAny<bool>())).Returns(Task.CompletedTask);
        _ = fixture.Sync.Setup(sync => sync.ReparentHierarchiesAsync(It.IsAny<Scene>(), It.IsAny<IReadOnlyList<Guid>>(), It.IsAny<Guid?>(), It.IsAny<bool>())).Returns(Task.CompletedTask);
        _ = fixture.Sync.Setup(sync => sync.RenameNodeAsync(It.IsAny<Scene>(), It.IsAny<Guid>(), It.IsAny<string>())).ReturnsAsync(new SyncOutcome(SyncStatus.Accepted, SceneOperationKinds.NodeRename, AffectedScope.Empty));
        _ = fixture.Sync.Setup(sync => sync.UpdateNodeTransformAsync(It.IsAny<Scene>(), It.IsAny<SceneNode>(), It.IsAny<CancellationToken>())).ReturnsAsync(new SyncOutcome(SyncStatus.Accepted, SceneOperationKinds.EditTransform, AffectedScope.Empty));
    }

    private static bool MatricesClose(Matrix4x4 left, Matrix4x4 right)
        => (left.Translation - right.Translation).Length() <= MatrixTolerance
           && MathF.Abs(left.M11 - right.M11) <= MatrixTolerance && MathF.Abs(left.M12 - right.M12) <= MatrixTolerance
           && MathF.Abs(left.M13 - right.M13) <= MatrixTolerance && MathF.Abs(left.M21 - right.M21) <= MatrixTolerance
           && MathF.Abs(left.M22 - right.M22) <= MatrixTolerance && MathF.Abs(left.M23 - right.M23) <= MatrixTolerance
           && MathF.Abs(left.M31 - right.M31) <= MatrixTolerance && MathF.Abs(left.M32 - right.M32) <= MatrixTolerance
           && MathF.Abs(left.M33 - right.M33) <= MatrixTolerance;

    private static ExplorerEntryData? FindFolderEntry(IList<ExplorerEntryData>? entries, Guid folderId)
    {
        if (entries is null)
        {
            return null;
        }

        foreach (var entry in entries)
        {
            if (entry.FolderId == folderId)
            {
                return entry;
            }

            if (FindFolderEntry(entry.Children, folderId) is { } found)
            {
                return found;
            }
        }

        return null;
    }
}
