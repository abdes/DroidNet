// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.EditorView;
using Oxygen.Editor.Scenes.Edits.Hierarchy;
using Oxygen.Editor.Scenes.Edits.Layout;
using Oxygen.Editor.Scenes.Layout;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.Authoring;

/// <summary>
/// <see cref="SceneEditBatch"/> (scene-edits.md 3.5): the edits of a batch apply in order as one entry, or none of them
/// does.
/// </summary>
[TestClass]
public sealed class SceneEditBatchTests
{
    /// <summary>The edits whose <see cref="ISceneEdit.ChangesDocument"/> a batch combines.</summary>
    public enum Mix
    {
        /// <summary>Every edit hides a node in the editor.</summary>
        AllHide,

        /// <summary>One edit hides a node in the editor, another renames a node.</summary>
        HideAndRename,
    }

    [TestMethod]
    public async Task TwoValidEdits_ApplyInOrderAsOneEntry()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var batch = new SceneEditBatch("Rename both", [new RenameNode(SampleScene.A, "X"), new RenameNode(SampleScene.B, "Y")]);

        var result = await fixture.Authoring.ExecuteAsync(batch).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = fixture.History.UndoStack.Should().ContainSingle().Which.Key.Should().Be("Rename both");
        SceneAssert.Changes(fixture.Changes.Only(), new SceneChange.NodeRenamed(SampleScene.A), new SceneChange.NodeRenamed(SampleScene.B));
    }

    [TestMethod]
    public async Task InsertThenRenameTheInsertedNode_Applies()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var inserted = NodeId.New();
        var batch = new SceneEditBatch("Create and rename", [InsertingRoot(inserted, "Fresh"), new RenameNode(inserted, "Renamed")]);

        var result = await fixture.Authoring.ExecuteAsync(batch).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = fixture.Node(inserted).Metadata.Name.Should().Be("Renamed");
        _ = fixture.History.UndoStack.Should().ContainSingle();
    }

    [TestMethod]
    public async Task SecondEditInvalid_RollsBackTheFirst()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var batch = new SceneEditBatch("Rename both", [new RenameNode(SampleScene.A, "X"), new RenameNode(SampleScene.B, string.Empty)]);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(batch).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.NameEmpty, SampleScene.B);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task SecondEditTargetLocked_RollsBackTheFirst()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        fixture.Interaction!.Lock(SampleScene.B);
        var batch = new SceneEditBatch("Rename both", [new RenameNode(SampleScene.A, "X"), new RenameNode(SampleScene.B, "Y")]);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(batch).ConfigureAwait(false);

        SceneAssert.Refused(result, SceneEditCodes.TargetLocked, SampleScene.B);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task ThirdOfThreeFails_RevertsTheFirstTwoInReverseOrder()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var log = new ProbeLog();
        var batch = new SceneEditBatch(
            "Three",
            [
                new ProbeEdit { Label = "first", Log = log },
                new ProbeEdit { Label = "second", Log = log },
                new ProbeEdit { Label = "third", Log = log, Validation = SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.NameEmpty, "empty")) },
            ]);

        var result = await fixture.Authoring.ExecuteAsync(batch).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.NameEmpty);
        _ = log.Steps.Where(static step => step.EndsWith(":Apply", StringComparison.Ordinal)).Should().Equal(
            "first:Apply",
            "second:Apply",
            "second (revert):Apply",
            "first (revert):Apply");
    }

    [TestMethod]
    public async Task RevertRejectedDuringRollback_Throws()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var failing = new ProbeEdit { Validation = SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.NameEmpty, "empty")) };
        var batch = new SceneEditBatch("Broken", [new RevertRefusedEdit(), failing]);

        var execute = () => fixture.Authoring.ExecuteAsync(batch);

        _ = await execute.Should().ThrowAsync<InvalidOperationException>().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task Undo_RestoresTheSceneBeforeTheBatch()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();
        _ = await fixture.Authoring.ExecuteAsync(new SceneEditBatch("Rename both", [new RenameNode(SampleScene.A, "X"), new RenameNode(SampleScene.B, "Y")])).ConfigureAwait(false);

        var undone = await fixture.Authoring.UndoAsync().ConfigureAwait(false);

        SceneAssert.Applied(undone);
        _ = fixture.Snapshot().Document.Should().Be(before.Document);
    }

    [TestMethod]
    public async Task Redo_ReappliesEveryInnerEdit()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        _ = await fixture.Authoring.ExecuteAsync(new SceneEditBatch("Rename both", [new RenameNode(SampleScene.A, "X"), new RenameNode(SampleScene.B, "Y")])).ConfigureAwait(false);
        _ = await fixture.Authoring.UndoAsync().ConfigureAwait(false);

        var redone = await fixture.Authoring.RedoAsync().ConfigureAwait(false);

        SceneAssert.Applied(redone);
        _ = fixture.Node(SampleScene.A).Metadata.Name.Should().Be("X");
        _ = fixture.Node(SampleScene.B).Metadata.Name.Should().Be("Y");
    }

    [TestMethod]
    public async Task Empty_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new SceneEditBatch("Nothing", [])).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.BatchEmpty);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task InnerEditsChangingNothing_RecordNothing()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        fixture.Interaction!.Hide(SampleScene.Cam);
        var batch = new SceneEditBatch("Nothing to do", [new RenameNode(SampleScene.A, "A"), new SetEditorHidden([NodeReference.To(SampleScene.Cam)], hidden: true)]);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(batch).ConfigureAwait(false);

        SceneAssert.Applied(result);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task NestedBatchFailing_RollsBackTheOuterBatch()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var missing = NodeId.New();
        var inner = new SceneEditBatch("Inner", [new RenameNode(SampleScene.B, "Y"), new RenameNode(missing, "Z")]);
        var outer = new SceneEditBatch("Outer", [new RenameNode(SampleScene.A, "X"), inner]);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(outer).ConfigureAwait(false);

        SceneAssert.Refused(result, SceneEditCodes.TargetMissing, missing);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    [DataRow(Mix.AllHide, false, DisplayName = "AllHide")]
    [DataRow(Mix.HideAndRename, true, DisplayName = "HideAndRename")]
    public void ChangesDocument(Mix mix, bool expected)
    {
        ISceneEdit[] edits = mix == Mix.AllHide
            ? [new SetEditorHidden([NodeReference.To(SampleScene.Cam)], hidden: true), new SetEditorHidden([NodeReference.To(SampleScene.Sun)], hidden: true)]
            : [new SetEditorHidden([NodeReference.To(SampleScene.Cam)], hidden: true), new RenameNode(SampleScene.B, "Box")];
        var batch = new SceneEditBatch("Mixed", edits);

        var changesDocument = batch.ChangesDocument;

        _ = changesDocument.Should().Be(expected);
    }

    [TestMethod]
    public async Task Validate_ValidBatch_ChangesNothing()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var batch = new SceneEditBatch("Rename both", [new RenameNode(SampleScene.A, "X"), new RenameNode(SampleScene.B, "Y")]);
        var before = fixture.Snapshot();

        var validation = await fixture.Authoring.ValidateAsync(batch).ConfigureAwait(false);

        _ = validation.Should().Be(SceneEditValidation.Valid);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task Validate_SecondEditInvalid_ReturnsItsDiagnosticAndChangesNothing()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var batch = new SceneEditBatch("Rename both", [new RenameNode(SampleScene.A, "X"), new RenameNode(SampleScene.B, string.Empty)]);
        var before = fixture.Snapshot();

        var validation = await fixture.Authoring.ValidateAsync(batch).ConfigureAwait(false);

        SceneAssert.Invalid(validation, SceneEditCodes.NameEmpty, SampleScene.B);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task Validate_FolderEdits_DoNotTouchTheLiveLayout()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var batch = new SceneEditBatch("Reorganize", [new RemoveFolder(SampleScene.F, PromoteChildren: true), CollapsingFolder(SampleScene.AF)]);
        var before = fixture.Snapshot();

        var validation = await fixture.Authoring.ValidateAsync(batch).ConfigureAwait(false);

        _ = validation.Should().Be(SceneEditValidation.Valid);
        _ = LayoutQuery.Folder(fixture.Layout, SampleScene.F).Should().NotBeNull();
        _ = LayoutQuery.Folder(fixture.Layout, SampleScene.AF)!.IsExpanded.Should().BeTrue();
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task InnerWarnings_AreReturned()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        fixture.Redirects.Delete(SampleScene.SphereGeometry);
        var ball = NodeId.New();
        var node = SceneBuilder.NodeData(
            ball,
            "Ball",
            components: [new GeometryComponentData { Id = ComponentId.New(), Name = "Geometry", GeometryUri = SampleScene.SphereGeometry }]);
        var batch = new SceneEditBatch("Create", [new InsertNodes([new NodeInsert(node, new NodePosition(Parent: null, Index: 0), LayoutPosition.AtRoot(0))])]);

        var result = await fixture.Authoring.ExecuteAsync(batch).ConfigureAwait(false);

        SceneAssert.Warned(result, SceneEditCodes.AssetReferenceDeleted, ball);
    }

    private static InsertNodes InsertingRoot(NodeId id, string name)
        => new([new NodeInsert(
            SceneBuilder.NodeData(id, name),
            new NodePosition(Parent: null, Index: 0),
            LayoutPosition.AtRoot(0))]);

    // An edit that collapses a folder of the layout it is applied to, as the explorer does when the user folds a folder.
    private static ProbeEdit CollapsingFolder(FolderId folder)
        => new()
        {
            Label = "Collapse folder",
            Changes = [new SceneChange.LayoutChanged()],
            Effect = scope => LayoutQuery.Folder(scope.Layout, folder)!.IsExpanded = false,
        };

    // Applies, and returns a revert that is always rejected: a defect the batch must report while rolling back.
    private sealed record RevertRefusedEdit : ISceneEdit
    {
        public string Label => "Revert refused";

        public IReadOnlyCollection<NodeId> Targets => [];

        public SceneEditValidation Validate(SceneEditScope scope) => SceneEditValidation.Valid;

        public SceneEditApplication Apply(SceneEditScope scope)
            => new SceneEditApplication.Applied(new AlwaysRejected(), [new SceneChange.LayoutChanged()]);
    }

    private sealed record AlwaysRejected : ISceneEdit
    {
        public string Label => "Always rejected";

        public IReadOnlyCollection<NodeId> Targets => [];

        public SceneEditValidation Validate(SceneEditScope scope) => SceneEditValidation.Valid;

        public SceneEditApplication Apply(SceneEditScope scope)
            => new SceneEditApplication.Rejected(SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.NameEmpty, "rejected")));
    }
}
