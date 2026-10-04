// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using Moq;
using Oxygen.Editor.World;
using Oxygen.Editor.World.SceneExplorer.Operations;
using Oxygen.Editor.WorldEditor.Documents.Commands;

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
        var fixture = CreateFixture();
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Cube" };
        scene.RootNodes.Add(node);
        var context = CreateContext(scene);

        var result = await fixture.Sut.RenameNodeAsync(context, node.Id, "Sphere").ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = node.Name.Should().Be("Sphere");
        _ = context.Metadata.IsDirty.Should().BeTrue();
        _ = context.History.UndoStack.Should().ContainSingle();

        await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = node.Name.Should().Be("Cube");
        _ = context.History.RedoStack.Should().ContainSingle();

        await context.History.RedoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = node.Name.Should().Be("Sphere");
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
    }

    private static bool MatricesClose(Matrix4x4 left, Matrix4x4 right)
        => (left.Translation - right.Translation).Length() <= MatrixTolerance
           && MathF.Abs(left.M11 - right.M11) <= MatrixTolerance && MathF.Abs(left.M12 - right.M12) <= MatrixTolerance
           && MathF.Abs(left.M13 - right.M13) <= MatrixTolerance && MathF.Abs(left.M21 - right.M21) <= MatrixTolerance
           && MathF.Abs(left.M22 - right.M22) <= MatrixTolerance && MathF.Abs(left.M23 - right.M23) <= MatrixTolerance
           && MathF.Abs(left.M31 - right.M31) <= MatrixTolerance && MathF.Abs(left.M32 - right.M32) <= MatrixTolerance
           && MathF.Abs(left.M33 - right.M33) <= MatrixTolerance;
}
