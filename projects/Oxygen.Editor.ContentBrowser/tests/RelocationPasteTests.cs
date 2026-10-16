// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Aura.Dialogs;
using DroidNet.TimeMachine;
using Moq;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentBrowser.Relocation;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Shell.Documents;

namespace Oxygen.Editor.ContentBrowser.Tests;

/// <summary>Checks that Paste, from the asset list or the sources tree, copies or moves what was put on the clipboard.</summary>
[TestClass]
public sealed class RelocationPasteTests
{
    /// <summary>Pasting copied items copies them into the folder.</summary>
    /// <returns>The asynchronous copy check.</returns>
    [TestMethod]
    public async Task PasteOfCopiedItemsCopiesThem()
    {
        var service = new Mock<IAssetRelocationService>();
        _ = service.Setup(value => value.CopyAsync(It.IsAny<IReadOnlyList<string>>(), "/Content/Props", It.IsAny<CancellationToken>()))
            .ReturnsAsync(["/Content/Props/Materials"]);
        using var histories = new HistoryService();
        using var workflow = CreateWorkflow(service.Object, histories);

        var outcome = await workflow.PasteAsync(["/Content/Materials"], move: false, "/Content/Props").ConfigureAwait(false);

        _ = outcome!.Succeeded.Should().BeTrue();
        service.Verify(value => value.CopyAsync(It.Is<IReadOnlyList<string>>(items => items.SequenceEqual(new[] { "/Content/Materials" })), "/Content/Props", It.IsAny<CancellationToken>()));
    }

    /// <summary>Pasting cut items moves those not already in the folder, and does nothing when all are.</summary>
    /// <returns>The asynchronous move check.</returns>
    [TestMethod]
    public async Task PasteOfCutItemsMovesThoseElsewhere()
    {
        var service = new Mock<IAssetRelocationService>();

        // The service rejects the move, so the check is the request alone.
        _ = service.Setup(value => value.RelocateAsync(It.IsAny<AssetRelocationRequest>(), "Move", It.IsAny<HistoryKeeper>(), It.IsAny<CancellationToken>()))
            .ThrowsAsync(new AssetRelocationException("Rejected by the test."));
        using var histories = new HistoryService();
        using var workflow = CreateWorkflow(service.Object, histories);

        _ = await workflow.PasteAsync(["/Content/Materials", "/Content/Props/Box.ogeo.json"], move: true, "/Content/Props").ConfigureAwait(false);
        var none = await workflow.PasteAsync(["/Content/Props/Box.ogeo.json"], move: true, "/Content/Props").ConfigureAwait(false);

        service.Verify(value => value.RelocateAsync(
            It.Is<AssetRelocationRequest>(request => request.Moves.Single().SourcePath == "/Content/Materials" && request.Moves.Single().TargetPath == "/Content/Props/Materials"),
            "Move",
            It.IsAny<HistoryKeeper>(),
            It.IsAny<CancellationToken>()));
        _ = none.Should().BeNull("every cut item is already in that folder");
    }

    private static AssetRelocationWorkflow CreateWorkflow(IAssetRelocationService service, IHistoryService histories)
        => new(
            service,
            histories,
            Mock.Of<IDialogService>(),
            Mock.Of<IMessenger>(),
            Mock.Of<IAssetFeed>(),
            Mock.Of<IAssetShell>());
}
