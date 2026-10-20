// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Aura.Dialogs;
using DroidNet.TimeMachine;
using Moq;
using Oxygen.Editor.ContentBrowser.Relocation;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Shell.Desktop;
using Oxygen.Editor.Shell.Documents;

namespace Oxygen.Editor.ContentBrowser.Tests;

/// <summary>Checks that the Content Browser's Undo and Redo run the session's global history.</summary>
[TestClass]
public sealed class RelocationHistoryTests
{
    /// <summary>Undo and Redo replay the global history's entries and say what they replayed.</summary>
    /// <returns>The asynchronous replay check.</returns>
    [TestMethod]
    public async Task UndoAndRedoReplayTheGlobalHistory()
    {
        using var histories = new HistoryService();
        using var workflow = CreateWorkflow(histories);
        var name = "Blue";
        Record(histories, "Rename", from: "Red", to: "Blue");
        var changes = 0;
        workflow.HistoryChanged += (_, _) => changes++;

        _ = workflow.UndoLabel.Should().Be("Rename");
        var undone = await workflow.UndoAsync().ConfigureAwait(false);
        _ = name.Should().Be("Red");
        _ = undone.Should().Be(new AssetRelocationOutcome(Succeeded: true, "Undo Rename", "Restored the previous names, locations and references."));
        _ = workflow.CanUndo.Should().BeFalse();
        _ = workflow.RedoLabel.Should().Be("Rename");

        var redone = await workflow.RedoAsync().ConfigureAwait(false);
        _ = name.Should().Be("Blue");
        _ = redone!.Title.Should().Be("Redo Rename");
        _ = workflow.CanUndo.Should().BeTrue();
        _ = workflow.CanRedo.Should().BeFalse();
        _ = changes.Should().BePositive();

        void Record(HistoryService service, string label, string from, string to)
            => service.GlobalHistory.AddChange(label, () =>
            {
                name = from;
                Record(service, label, to, from);
            });
    }

    /// <summary>A rejected undo changes nothing, reports why and stays available.</summary>
    /// <returns>The asynchronous rejection check.</returns>
    [TestMethod]
    public async Task RejectedUndoStaysInTheHistory()
    {
        using var histories = new HistoryService();
        using var workflow = CreateWorkflow(histories);
        histories.GlobalHistory.AddChange("Move", () => throw new AssetRelocationException("'/Content/Red.omat.json' already exists."));

        var outcome = await workflow.UndoAsync().ConfigureAwait(false);

        _ = outcome.Should().Be(new AssetRelocationOutcome(Succeeded: false, "Undo Move", "'/Content/Red.omat.json' already exists."));
        _ = workflow.CanUndo.Should().BeTrue();
    }

    /// <summary>With nothing recorded there is nothing to undo or redo.</summary>
    /// <returns>The asynchronous empty-history check.</returns>
    [TestMethod]
    public async Task EmptyHistoryHasNothingToReplay()
    {
        using var histories = new HistoryService();
        using var workflow = CreateWorkflow(histories);

        _ = await workflow.UndoAsync().ConfigureAwait(false);
        _ = (await workflow.RedoAsync().ConfigureAwait(false)).Should().BeNull();
        _ = workflow.UndoLabel.Should().BeNull();
    }

    private static AssetRelocationWorkflow CreateWorkflow(IHistoryService histories)
        => new(
            Mock.Of<IAssetRelocationService>(),
            histories,
            Mock.Of<IDialogService>(),
            Mock.Of<IDesktopIntegration>());
}
