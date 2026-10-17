// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.EditorView;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.Edits.EditorView;

/// <summary>
/// What is specific to <see cref="SetEditorHidden"/> (scene-edits.md 4, "Hiding"): the state belongs to the workspace,
/// not to the document, and a lock does not refuse it. Undo and the change stream of every edit are
/// <c>EditContractTests</c>.
/// </summary>
[TestClass]
public sealed class SetEditorHiddenTests
{
    [TestMethod]
    public async Task Hide_RaisesVisibilityAndKeepsTheDocumentClean()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new SetEditorHidden([SampleScene.B], hidden: true)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = fixture.Interaction!.IsHidden(SampleScene.B).Should().BeTrue();
        SceneAssert.Changes(fixture.Changes.Only(), new SceneChange.EditorVisibilityChanged([SampleScene.B]));
        _ = fixture.Snapshot().Document.Should().Be(before.Document, "hiding is not authoring");
        _ = fixture.History.UndoStack.Should().ContainSingle().Which.ChangesState.Should().BeFalse();
        _ = (await fixture.Authoring.UndoAsync().ConfigureAwait(false)).Outcome.Should().Be(SceneEditOutcome.Applied);
        _ = fixture.Interaction.IsHidden(SampleScene.B).Should().BeFalse("undo shows the node again");
    }

    [TestMethod]
    public async Task HideHiddenNode_AppliesWithoutAnEntry()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        fixture.Interaction!.Hide(SampleScene.B);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new SetEditorHidden([SampleScene.B], hidden: true)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task HiddenAndShownNodes_RecordOneEntryRaisingOnlyTheChanged()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        fixture.Interaction!.Hide(SampleScene.B);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new SetEditorHidden([SampleScene.B, SampleScene.Cam], hidden: true)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = fixture.Snapshot().UndoCount.Should().Be(before.UndoCount + 1);
        SceneAssert.Changes(fixture.Changes.Only(), new SceneChange.EditorVisibilityChanged([SampleScene.Cam]));
        _ = (await fixture.Authoring.UndoAsync().ConfigureAwait(false)).Outcome.Should().Be(SceneEditOutcome.Applied);
        _ = fixture.Interaction.Hidden.Should().Equal([SampleScene.B], "undo shows only the node the edit hid");
    }

    [TestMethod]
    public async Task LockedNode_Hides()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        fixture.Interaction!.Lock(SampleScene.B);

        var result = await fixture.Authoring.ExecuteAsync(new SetEditorHidden([SampleScene.B], hidden: true)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = fixture.Interaction.IsHidden(SampleScene.B).Should().BeTrue();
    }

    [TestMethod]
    public async Task MissingIds_AreSkipped()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var missing = NodeId.New();

        var result = await fixture.Authoring.ExecuteAsync(new SetEditorHidden([SampleScene.B, missing], hidden: true)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = fixture.Interaction!.Hidden.Should().Equal([SampleScene.B]);
    }

    [TestMethod]
    public async Task OnlyMissingIds_AppliesWithoutAnEntry()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new SetEditorHidden([NodeId.New()], hidden: true)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task NoIds_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new SetEditorHidden([], hidden: true)).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.NothingToEdit);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task WithoutInteractionPort_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build(), withInteraction: false).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new SetEditorHidden([SampleScene.B], hidden: true)).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.InteractionPortMissing);
        SceneAssert.Unchanged(fixture, before);
    }
}
