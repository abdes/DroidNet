// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Edits.Hierarchy;
using Oxygen.Editor.Scenes.Edits.Layout;
using Oxygen.Editor.Scenes.Nodes;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.Scenarios;

/// <summary>
/// What a user does to the scene's hierarchy from the explorer and the toolbar, each through the edits the views build.
/// </summary>
[TestClass]
public sealed class HierarchyScenarioTests
{
    [TestMethod]
    public async Task CreatePrimitiveFromToolbar_ThenUndo_RemovesIt()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();
        var cube = SceneNodeTemplates.Primitive(fixture.Scene, PrimitiveKind.Cube);
        var created = await fixture.Authoring.ExecuteAsync(new InsertNodes([SceneNodeTemplates.Append(fixture.Scene, fixture.Layout, cube)])).ConfigureAwait(false);
        var recorded = fixture.History.UndoStack.Count;

        var undone = await fixture.Authoring.UndoAsync().ConfigureAwait(false);

        SceneAssert.Applied(created);
        _ = recorded.Should().Be(before.UndoCount + 1, "the toolbar's create is one entry");
        SceneAssert.Applied(undone);
        _ = fixture.Scene.AllNodes.Select(static node => node.Id).Should().NotContain(cube.Id);
        _ = fixture.Snapshot().Document.Should().Be(before.Document);
    }

    [TestMethod]
    public async Task DeleteNodesAndFolders_IsOneUndoStep()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        // Deleting Cam and folder F deletes what F holds: B and the empty folder G.
        var deleted = await fixture.Authoring.ExecuteAsync(DeleteCamAndF()).ConfigureAwait(false);
        var recorded = fixture.History.UndoStack.Count;
        var undone = await fixture.Authoring.UndoAsync().ConfigureAwait(false);

        SceneAssert.Applied(deleted);
        _ = recorded.Should().Be(before.UndoCount + 1);
        SceneAssert.Applied(undone);
        _ = fixture.Snapshot().Document.Should().Be(before.Document, "one undo restores the nodes, the folders and their places");
    }

    [TestMethod]
    public async Task DeleteWithAStaleItem_ChangesNothing()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var removedMeanwhile = NodeId.New();
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new SceneEditBatch(
            "Delete",
            [new RemoveNodes([SampleScene.Cam, removedMeanwhile]), new RemoveFolder(SampleScene.G, PromoteChildren: false)])).ConfigureAwait(false);

        SceneAssert.Refused(result, SceneEditCodes.TargetMissing, removedMeanwhile);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task GroupIntoFolderOfAnotherScope_ThenDeleteFolder_KeepsParentAndLocal()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var local = LocalOf(fixture, SampleScene.B);

        // Dropping B into AF, under A, moves B under A first: a folder holds only nodes of its own scope.
        _ = await fixture.Authoring.ExecuteAsync(new SceneEditBatch(
            "Group",
            [
                new ReparentNodes([SampleScene.B], SampleScene.A, preserveWorld: false),
                new MoveLayoutEntries([LayoutEntryId.Of(SampleScene.B)], LayoutPosition.InFolder(SampleScene.AF, 0)),
            ])).ConfigureAwait(false);
        var deleted = await fixture.Authoring.ExecuteAsync(new RemoveFolder(SampleScene.AF, PromoteChildren: true)).ConfigureAwait(false);

        SceneAssert.Applied(deleted);
        _ = fixture.Node(SampleScene.B).Hierarchy.Parent!.Id.Should().Be(SampleScene.A);
        _ = LocalOf(fixture, SampleScene.B).Should().Be(local);
        _ = LayoutScopes.TryScopeOf(fixture.Layout, LayoutEntryId.Of(SampleScene.B), out var scope).Should().BeTrue();
        _ = scope.Should().Be(SampleScene.A, "the promoted entry stays in A's scope");
    }

    [TestMethod]
    public async Task CopyPasteBetweenTwoScenes_UndoInEachIsIndependent()
    {
        using var source = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        using var target = await AuthoringFixture.CreateAsync(SampleScene.Builder().Without(SampleScene.B).Build()).ConfigureAwait(false);
        _ = await source.Authoring.ExecuteAsync(new RenameNode(SampleScene.A, "Renamed")).ConfigureAwait(false);
        var sourceEdited = source.Snapshot();
        var targetBefore = target.Snapshot();
        var clipboard = SceneClipboard.Capture(source.Scene, source.Layout, [SampleScene.B], []) ?? throw new InvalidOperationException("B was not captured.");
        _ = clipboard.TryPaste(target.Scene, target.Layout, new PasteTarget(), cut: false, preserveWorld: false, out var paste, out _).Should().BeTrue();
        _ = await target.Authoring.ExecuteAsync(paste!).ConfigureAwait(false);

        var undoneInTarget = await target.Authoring.UndoAsync().ConfigureAwait(false);

        SceneAssert.Applied(undoneInTarget);
        _ = target.Snapshot().Document.Should().Be(targetBefore.Document);
        _ = source.Snapshot().Should().Be(sourceEdited, "an undo in one scene leaves the other alone");
        SceneAssert.Applied(await source.Authoring.UndoAsync().ConfigureAwait(false));
        _ = source.Node(SampleScene.A).Metadata.Name.Should().Be("A");
        _ = target.History.UndoStack.Should().BeEmpty();
    }

    [TestMethod]
    public async Task CanExecuteChecks_ValidateRepeatedly_ChangeNothing()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var clipboard = SceneClipboard.Capture(fixture.Scene, fixture.Layout, [SampleScene.B], []) ?? throw new InvalidOperationException("B was not captured.");
        _ = clipboard.TryPaste(fixture.Scene, fixture.Layout, new PasteTarget(Parent: SampleScene.A), cut: false, preserveWorld: false, out var paste, out _).Should().BeTrue();
        ISceneEdit[] checks = [paste!, DeleteCamAndF(), new ReparentNodes([SampleScene.B], SampleScene.A, preserveWorld: true)];
        var before = fixture.Snapshot();

        var validations = new List<SceneEditValidation>();
        for (var round = 0; round < 10; round++)
        {
            foreach (var check in checks)
            {
                validations.Add(await fixture.Authoring.ValidateAsync(check).ConfigureAwait(false));
            }
        }

        _ = validations.Should().AllSatisfy(static validation => validation.IsValid.Should().BeTrue());
        SceneAssert.Unchanged(fixture, before);
    }

    private static SceneEditBatch DeleteCamAndF()
        => new(
            "Delete",
            [
                new RemoveNodes([SampleScene.Cam, SampleScene.B]),
                new RemoveFolder(SampleScene.G, PromoteChildren: false),
                new RemoveFolder(SampleScene.F, PromoteChildren: false),
            ]);

    private static (Vector3 Position, Quaternion Rotation, Vector3 Scale) LocalOf(AuthoringFixture fixture, NodeId node)
    {
        var transform = fixture.Node(node).Transform;
        return (transform.LocalPosition, transform.LocalRotation, transform.LocalScale);
    }
}
