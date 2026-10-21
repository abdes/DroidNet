// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Aura.Dialogs;
using DroidNet.Storage;
using DroidNet.TimeMachine;
using Moq;
using Oxygen.Editor.ContentExplorer.Relocation;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Shell.Desktop;
using Oxygen.Editor.Shell.Documents;

namespace Oxygen.Editor.ContentExplorer.Tests;

/// <summary>Checks the outcome the Content Explorer shows for an Undo or Redo the relocation service ran.</summary>
[TestClass]
public sealed class RelocationHistoryTests
{
    /// <summary>A replayed entry is reported under its name.</summary>
    /// <returns>The asynchronous replay check.</returns>
    [TestMethod]
    public async Task ReplayedEntriesAreReportedUnderTheirNames()
    {
        using var histories = new HistoryService();
        var service = new Mock<IAssetRelocationService>();
        _ = service.Setup(value => value.UndoAsync(histories.GlobalHistory, It.IsAny<CancellationToken>())).ReturnsAsync("Rename");
        _ = service.Setup(value => value.RedoAsync(histories.GlobalHistory, It.IsAny<CancellationToken>())).ReturnsAsync("Rename");
        using var workflow = CreateWorkflow(service.Object, histories);

        var undone = await workflow.UndoAsync().ConfigureAwait(false);
        var redone = await workflow.RedoAsync().ConfigureAwait(false);

        _ = undone.Should().Be(new AssetRelocationOutcome(Succeeded: true, "Undo Rename", "Restored the previous names, locations and references."));
        _ = redone.Should().Be(new AssetRelocationOutcome(Succeeded: true, "Redo Rename", "Applied the names, locations and references again."));
    }

    /// <summary>A rejected undo reports why, under the entry the history named.</summary>
    /// <returns>The asynchronous rejection check.</returns>
    [TestMethod]
    public async Task RejectedUndoReportsWhy()
    {
        using var histories = new HistoryService();
        histories.GlobalHistory.AddChange("Move", () => { });
        var service = new Mock<IAssetRelocationService>();
        _ = service.Setup(value => value.UndoAsync(histories.GlobalHistory, It.IsAny<CancellationToken>()))
            .ThrowsAsync(new AssetRelocationException("'/Content/Red.omat.json' already exists."));
        using var workflow = CreateWorkflow(service.Object, histories);

        var outcome = await workflow.UndoAsync().ConfigureAwait(false);

        _ = outcome.Should().Be(new AssetRelocationOutcome(Succeeded: false, "Undo Move", "'/Content/Red.omat.json' already exists."));
    }

    /// <summary>When the service replays nothing, nothing is reported.</summary>
    /// <returns>The asynchronous empty-history check.</returns>
    [TestMethod]
    public async Task NothingReplayedReportsNothing()
    {
        using var histories = new HistoryService();
        using var workflow = CreateWorkflow(Mock.Of<IAssetRelocationService>(), histories);

        _ = (await workflow.UndoAsync().ConfigureAwait(false)).Should().BeNull();
        _ = (await workflow.RedoAsync().ConfigureAwait(false)).Should().BeNull();
        _ = workflow.UndoLabel.Should().BeNull();
    }

    private static AssetRelocationWorkflow CreateWorkflow(IAssetRelocationService service, IHistoryService histories)
        => new(
            service,
            histories,
            SavingDocuments(),
            new OpenDocuments([], Mock.Of<IAtomicFileStore>()),
            Mock.Of<IDialogService>(),
            Mock.Of<IDesktopIntegration>());

    private static IDocumentService SavingDocuments()
    {
        var documents = new Mock<IDocumentService>();
        _ = documents.Setup(value => value.SaveBeforeAsync(It.IsAny<SaveBeforeOperation>(), It.IsAny<Func<CancellationToken, Task<IReadOnlyList<UnsavedDocument>>>>(), It.IsAny<CancellationToken>())).ReturnsAsync(value: true);
        return documents.Object;
    }
}
