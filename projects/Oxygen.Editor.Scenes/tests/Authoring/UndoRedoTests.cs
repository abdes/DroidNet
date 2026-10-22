// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.EditorView;
using Oxygen.Editor.Scenes.Edits.Hierarchy;
using Oxygen.Editor.Scenes.Edits.Layout;
using Oxygen.Editor.Scenes.Edits.Properties;
using Oxygen.Editor.Scenes.Layout;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Editor.Schemas;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.Authoring;

/// <summary>
/// Undo and redo through <see cref="SceneAuthoring"/> (scene-edits.md 3.4), with real edits: unless stated, the edit is
/// <see cref="RenameNode"/> of <c>B</c> to "Box".
/// </summary>
[TestClass]
public sealed class UndoRedoTests
{
    [TestMethod]
    public async Task Undo_AfterRename_RestoresTheNameAndRaisesNodeRenamed()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        _ = await fixture.Authoring.ExecuteAsync(new RenameNode(SampleScene.B, "Box")).ConfigureAwait(false);
        fixture.Changes.Clear();

        var undone = await fixture.Authoring.UndoAsync().ConfigureAwait(false);

        SceneAssert.Applied(undone);
        _ = fixture.Node(SampleScene.B).Metadata.Name.Should().Be("B");
        SceneAssert.Changes(fixture.Changes.Only(), new SceneChange.NodeRenamed(SampleScene.B));
    }

    [TestMethod]
    public async Task Redo_AfterUndo_RenamesAgain()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        _ = await fixture.Authoring.ExecuteAsync(new RenameNode(SampleScene.B, "Box")).ConfigureAwait(false);
        _ = await fixture.Authoring.UndoAsync().ConfigureAwait(false);
        fixture.Changes.Clear();

        var redone = await fixture.Authoring.RedoAsync().ConfigureAwait(false);

        SceneAssert.Applied(redone);
        _ = fixture.Node(SampleScene.B).Metadata.Name.Should().Be("Box");
        SceneAssert.Changes(fixture.Changes.Only(), new SceneChange.NodeRenamed(SampleScene.B));
    }

    [TestMethod]
    public async Task Undo_EmptyHistory_IsNothingToEdit()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var undone = await fixture.Authoring.UndoAsync().ConfigureAwait(false);

        SceneAssert.Invalid(undone, SceneEditCodes.NothingToEdit);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task Redo_NothingUndone_IsNothingToEdit()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        _ = await fixture.Authoring.ExecuteAsync(new RenameNode(SampleScene.B, "Box")).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var redone = await fixture.Authoring.RedoAsync().ConfigureAwait(false);

        SceneAssert.Invalid(redone, SceneEditCodes.NothingToEdit);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task Undo_TargetLockedSinceTheEdit_StillUndoes()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        _ = await fixture.Authoring.ExecuteAsync(new RenameNode(SampleScene.B, "Box")).ConfigureAwait(false);
        fixture.Interaction!.Lock(SampleScene.B);

        var undone = await fixture.Authoring.UndoAsync().ConfigureAwait(false);
        var nameAfterUndo = fixture.Node(SampleScene.B).Metadata.Name;
        var redone = await fixture.Authoring.RedoAsync().ConfigureAwait(false);

        SceneAssert.Applied(undone);
        _ = nameAfterUndo.Should().Be("B");
        SceneAssert.Applied(redone);
        _ = fixture.Node(SampleScene.B).Metadata.Name.Should().Be("Box");
    }

    [TestMethod]
    public async Task Undo_TargetRemovedOutsideTheHistory_IsRefusedAndKeepsTheEntry()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        _ = await fixture.Authoring.ExecuteAsync(new RenameNode(SampleScene.B, "Box")).ConfigureAwait(false);
        _ = await fixture.Authoring.ExecuteAsync(RemovingOutsideTheHistory(SampleScene.B)).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var undone = await fixture.Authoring.UndoAsync().ConfigureAwait(false);

        SceneAssert.Refused(undone, SceneEditCodes.TargetMissing, SampleScene.B);
        _ = fixture.History.UndoStack.Should().ContainSingle();
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task Undo_BatchWhoseRevertNoLongerFits_IsRefusedAndKeepsTheEntry()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var batch = new SceneEditBatch("Rename both", [new RenameNode(SampleScene.B, "Box"), new RenameNode(SampleScene.A, "Alpha")]);
        _ = await fixture.Authoring.ExecuteAsync(batch).ConfigureAwait(false);
        _ = await fixture.Authoring.ExecuteAsync(RemovingOutsideTheHistory(SampleScene.A)).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var undone = await fixture.Authoring.UndoAsync().ConfigureAwait(false);

        SceneAssert.Refused(undone, SceneEditCodes.TargetMissing, SampleScene.A);
        _ = fixture.Node(SampleScene.B).Metadata.Name.Should().Be("Box");
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task Undo_AfterDispose_IsRetiredAndKeepsTheEntry()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        _ = await fixture.Authoring.ExecuteAsync(new RenameNode(SampleScene.B, "Box")).ConfigureAwait(false);
        fixture.Authoring.Dispose();
        var before = fixture.Snapshot();

        var undone = await fixture.Authoring.UndoAsync().ConfigureAwait(false);

        SceneAssert.Retired(undone);
        _ = fixture.History.UndoStack.Should().ContainSingle();
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task Execute_AfterUndo_ClearsRedo()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        _ = await fixture.Authoring.ExecuteAsync(new RenameNode(SampleScene.B, "Box")).ConfigureAwait(false);
        _ = await fixture.Authoring.UndoAsync().ConfigureAwait(false);

        _ = await fixture.Authoring.ExecuteAsync(new RenameNode(SampleScene.Cam, "Eye")).ConfigureAwait(false);

        SceneAssert.Invalid(await fixture.Authoring.RedoAsync().ConfigureAwait(false), SceneEditCodes.NothingToEdit);
        _ = fixture.History.RedoStack.Should().BeEmpty();
    }

    [TestMethod]
    public async Task UndoAllThenRedoAll_ReturnsToEachStateInOrder()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var edits = new ISceneEdit[]
        {
            new RenameNode(SampleScene.B, "Box"),
            new ReparentNodes([SampleScene.Cam], SampleScene.A, preserveWorld: false),
            new CreateFolder(FolderId.New(), LayoutPosition.AtRoot(0), "New"),
            new SetProperties(
                new Dictionary<NodeId, PropertyEdit> { [SampleScene.A] = PropertyEdit.SingleEdit(SceneSchema.Transform.PositionX, 7f) },
                "Move"),
            new SetEditorHidden([SampleScene.Sun], hidden: true),
        };
        var recorded = new List<string> { State(fixture) };
        foreach (var edit in edits)
        {
            _ = await fixture.Authoring.ExecuteAsync(edit).ConfigureAwait(false);
            recorded.Add(State(fixture));
        }

        var afterUndo = new List<string>();
        for (var i = 0; i < edits.Length; i++)
        {
            _ = await fixture.Authoring.UndoAsync().ConfigureAwait(false);
            afterUndo.Add(State(fixture));
        }

        var afterRedo = new List<string>();
        for (var i = 0; i < edits.Length; i++)
        {
            _ = await fixture.Authoring.RedoAsync().ConfigureAwait(false);
            afterRedo.Add(State(fixture));
        }

        _ = afterUndo.Should().Equal(recorded.Take(edits.Length).Reverse());
        _ = afterRedo.Should().Equal(recorded.Skip(1));
    }

    [TestMethod]
    public async Task Undo_HideInEditor_KeepsTheDocumentClean()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        _ = await fixture.Authoring.ExecuteAsync(new SetEditorHidden([SampleScene.Cam], hidden: true)).ConfigureAwait(false);

        var undone = await fixture.Authoring.UndoAsync().ConfigureAwait(false);

        // The entry the undo leaves for redo is still an editor-view change, not a document change.
        SceneAssert.Applied(undone);
        _ = fixture.Interaction!.IsHidden(SampleScene.Cam).Should().BeFalse();
        _ = fixture.History.RedoStack.Should().ContainSingle().Which.ChangesState.Should().BeFalse();
    }

    [TestMethod]
    public async Task Undo_MovesTheEntryToRedo()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        _ = await fixture.Authoring.ExecuteAsync(new RenameNode(SampleScene.B, "Box")).ConfigureAwait(false);

        _ = await fixture.Authoring.UndoAsync().ConfigureAwait(false);

        _ = fixture.History.UndoStack.Should().BeEmpty();
        _ = fixture.History.RedoStack.Should().ContainSingle();
    }

    // Removes a node without recording it: the edit reports no changes, so the frame keeps no entry and raises nothing.
    private static ProbeEdit RemovingOutsideTheHistory(NodeId node)
        => new()
        {
            Label = "Remove outside the history",
            ChangesDocument = false,
            Changes = [],
            Effect = scope => _ = ((ISceneEdit)new RemoveNodes([node])).Apply(scope),
        };

    // The document and the hidden nodes: what an undo or a redo must return to.
    private static string State(AuthoringFixture fixture)
    {
        var snapshot = fixture.Snapshot();
        return $"{snapshot.Document}|{snapshot.Hidden}";
    }
}
