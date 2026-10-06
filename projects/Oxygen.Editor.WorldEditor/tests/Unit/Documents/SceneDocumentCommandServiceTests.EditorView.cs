// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.World;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Documents;

/// <summary>Editor-view hide command: undoable, non-dirtying, idempotent, and visible on failure.</summary>
public sealed partial class SceneDocumentCommandServiceTests
{
    [TestMethod]
    public async Task SetEditorHiddenAsync_HidesNode_RecordsOneUndoStep_WithoutDirtying()
    {
        var (interaction, _) = CreateInteraction();
        var fixture = CreateFixture(interaction: interaction);
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Cube" };
        scene.RootNodes.Add(node);
        var context = CreateContext(scene);

        var result = await fixture.Sut.SetEditorHiddenAsync(context, [node.Id], hidden: true).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = interaction.IsHidden(node.Id).Should().BeTrue("the batch was requested hidden");

        // The ratified exception (D8 R4): undoable but never dirties the document.
        _ = context.Metadata.IsDirty.Should().BeFalse("editor hide is workspace presentation state, not authored content");
        _ = context.History.UndoStack.Should().ContainSingle("one batch is one undo step");

        await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = interaction.IsHidden(node.Id).Should().BeFalse("undo restores the prior shown state");
        _ = context.Metadata.IsDirty.Should().BeFalse("undo of a non-dirtying step stays non-dirtying");
        _ = context.History.RedoStack.Should().ContainSingle();

        await context.History.RedoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = interaction.IsHidden(node.Id).Should().BeTrue("redo re-applies the hide");
    }

    [TestMethod]
    public async Task SetEditorHiddenAsync_IdempotentRequest_RecordsNoExtraStep()
    {
        var (interaction, _) = CreateInteraction();
        var fixture = CreateFixture(interaction: interaction);
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Cube" };
        scene.RootNodes.Add(node);
        var context = CreateContext(scene);

        _ = await fixture.Sut.SetEditorHiddenAsync(context, [node.Id], hidden: true).ConfigureAwait(false);
        _ = context.History.UndoStack.Should().ContainSingle();

        // Re-issuing the same state changes nothing, so it must not litter the history stack.
        _ = await fixture.Sut.SetEditorHiddenAsync(context, [node.Id], hidden: true).ConfigureAwait(false);
        _ = context.History.UndoStack.Should().ContainSingle("an idempotent request records no history step");
    }

    [TestMethod]
    public async Task SetEditorHiddenAsync_MixedBatch_RecordsOneStepForAllIds()
    {
        var (interaction, _) = CreateInteraction();
        var fixture = CreateFixture(interaction: interaction);
        var scene = CreateScene();
        var a = new SceneNode(scene) { Name = "A" };
        var b = new SceneNode(scene) { Name = "B" };
        scene.RootNodes.Add(a);
        scene.RootNodes.Add(b);
        var context = CreateContext(scene);

        _ = await fixture.Sut.SetEditorHiddenAsync(context, [a.Id, b.Id], hidden: true).ConfigureAwait(false);

        _ = interaction.IsHidden(a.Id).Should().BeTrue();
        _ = interaction.IsHidden(b.Id).Should().BeTrue();
        _ = context.History.UndoStack.Should().ContainSingle("a two-node batch is one user intent");

        await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = interaction.IsHidden(a.Id).Should().BeFalse();
        _ = interaction.IsHidden(b.Id).Should().BeFalse("undo reverts the whole batch together");
    }

    [TestMethod]
    public async Task SetEditorHiddenAsync_WithoutInteraction_PublishesVisibleFailure()
    {
        var fixture = CreateFixture(); // no interaction composed
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Cube" };
        scene.RootNodes.Add(node);
        var context = CreateContext(scene);

        var result = await fixture.Sut.SetEditorHiddenAsync(context, [node.Id], hidden: true).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = fixture.Results.Published.Should().NotBeEmpty();
        _ = fixture.Results.Published
            .SelectMany(published => published.Diagnostics)
            .Should()
            .Contain(d => d.Code == DiagnosticCodes.SettingsPrefix + "WORKSPACE_STATE_UNAVAILABLE",
                "a missing owner is reported visibly, not logged and swallowed (D-f)");
    }
}
