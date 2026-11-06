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
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentExplorer.Tests;

/// <summary>What a move into a shared mount asks the user, and the request that answer runs.</summary>
[TestClass]
public sealed class RelocationReviewTests
{
    private static readonly AssetRelocationRequest Request = new() { Moves = [new(AssetPath.Parse("/Content/V1.gltf"), AssetPath.Parse("/Studio/V1.gltf"))] };
    private static readonly AssetPath Source = AssetPath.Parse("/RawMedia/V1.gltf");

    /// <summary>The source choice decides whether the move copies sources in or keeps the references.</summary>
    /// <param name="button">The button the user chose.</param>
    /// <param name="copies">Whether the request copies sources in.</param>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    [DataRow(DialogButton.Primary, true)]
    [DataRow(DialogButton.Secondary, false)]
    public async Task TheSourceChoiceDecidesTheRequest(DialogButton button, bool copies)
    {
        var service = Service(new([], [], ["Studio"], [Source]));
        var dialogs = new Mock<IDialogService>();
        DialogSpec? asked = null;
        _ = dialogs.Setup(value => value.ShowAsync(It.IsAny<DialogSpec>(), It.IsAny<CancellationToken>()))
            .Callback<DialogSpec, CancellationToken>((spec, _) => asked = spec)
            .ReturnsAsync(button);
        using var histories = new HistoryService();
        using var workflow = CreateWorkflow(service.Object, histories, dialogs.Object);

        _ = await workflow.RelocateAsync(Request, "Move").ConfigureAwait(false);

        _ = asked!.Content.Should().BeOfType<string>().Which.Should().Contain(Source.ToString()).And.Contain("'Studio'");
        var expected = Request with { CopiesSourcesIn = copies };
        service.Verify(value => value.RelocateAsync(expected, "Move", It.IsAny<HistoryKeeper>(), It.IsAny<CancellationToken>()), Times.Once);
    }

    /// <summary>Cancelling the source choice moves nothing.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task CancellingTheSourceChoiceMovesNothing()
    {
        var service = Service(new([], [], ["Studio"], [Source]));
        var dialogs = new Mock<IDialogService>();
        _ = dialogs.Setup(value => value.ShowAsync(It.IsAny<DialogSpec>(), It.IsAny<CancellationToken>())).ReturnsAsync(DialogButton.Close);
        using var histories = new HistoryService();
        using var workflow = CreateWorkflow(service.Object, histories, dialogs.Object);

        var outcome = await workflow.RelocateAsync(Request, "Move").ConfigureAwait(false);

        _ = outcome.Should().BeNull();
        service.Verify(value => value.RelocateAsync(It.IsAny<AssetRelocationRequest>(), It.IsAny<string>(), It.IsAny<HistoryKeeper>(), It.IsAny<CancellationToken>()), Times.Never);
    }

    /// <summary>A closure is confirmed, naming what moves with the request and that other projects are not updated.</summary>
    /// <param name="confirmed">The user's answer.</param>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    [DataRow(true)]
    [DataRow(false)]
    public async Task AClosureIsConfirmedBeforeTheMove(bool confirmed)
    {
        var closure = new AssetRelocationMove(AssetPath.Parse("/Content/Rust.otex.json"), AssetPath.Parse("/Studio/Rust.otex.json"));
        var service = Service(new([closure], [], ["Studio"], []));
        var dialogs = new Mock<IDialogService>();
        string? asked = null;
        _ = dialogs.Setup(value => value.ConfirmAsync("Move", It.IsAny<string>(), It.IsAny<CancellationToken>()))
            .Callback<string, string, CancellationToken>((_, message, _) => asked = message)
            .ReturnsAsync(confirmed);
        using var histories = new HistoryService();
        using var workflow = CreateWorkflow(service.Object, histories, dialogs.Object);

        _ = await workflow.RelocateAsync(Request, "Move").ConfigureAwait(false);

        _ = asked.Should().Contain("/Content/Rust.otex.json").And.Contain("Other projects");
        service.Verify(value => value.RelocateAsync(Request, "Move", It.IsAny<HistoryKeeper>(), It.IsAny<CancellationToken>()), confirmed ? Times.Once() : Times.Never());
    }

    /// <summary>A move that stays in own mounts asks nothing.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task AMoveInOwnMountsAsksNothing()
    {
        var service = Service(new([], [], [], []));
        var dialogs = new Mock<IDialogService>(MockBehavior.Strict);
        using var histories = new HistoryService();
        using var workflow = CreateWorkflow(service.Object, histories, dialogs.Object);

        _ = await workflow.RelocateAsync(Request, "Move").ConfigureAwait(false);

        service.Verify(value => value.RelocateAsync(Request, "Move", It.IsAny<HistoryKeeper>(), It.IsAny<CancellationToken>()), Times.Once);
    }

    // The service rejects the move, so the check is the request alone.
    private static Mock<IAssetRelocationService> Service(AssetRelocationReview review)
    {
        var service = new Mock<IAssetRelocationService>();
        _ = service.Setup(value => value.PreviewAsync(It.IsAny<AssetRelocationRequest>(), It.IsAny<CancellationToken>())).ReturnsAsync(review);
        _ = service.Setup(value => value.RelocateAsync(It.IsAny<AssetRelocationRequest>(), "Move", It.IsAny<HistoryKeeper>(), It.IsAny<CancellationToken>()))
            .ThrowsAsync(new AssetRelocationException("Rejected by the test."));
        return service;
    }

    private static AssetRelocationWorkflow CreateWorkflow(IAssetRelocationService service, IHistoryService histories, IDialogService dialogs)
    {
        var documents = new Mock<IDocumentArea>();
        _ = documents.Setup(value => value.SaveBeforeAsync(It.IsAny<SaveBeforeOperation>(), It.IsAny<Func<CancellationToken, Task<IReadOnlyList<UnsavedDocument>>>>(), It.IsAny<CancellationToken>())).ReturnsAsync(value: true);
        return new(service, histories, documents.Object, new OpenDocuments([], Mock.Of<IAtomicFileStore>()), dialogs, Mock.Of<IDesktopIntegration>());
    }
}
