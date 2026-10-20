// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.TimeMachine;
using Moq;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Shell.Documents;

namespace Oxygen.Editor.Shell.Unit.Tests.Documents;

/// <summary>The conflict actions of an unsaved document that a close asks about.</summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository's discovery configuration.")]
public sealed class DocumentConflictTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Recovery actions queue behind pending I/O and only a clean reload acknowledges the original.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task CopyAndReloadSerializeWithoutAcknowledgingTheOriginalEarly()
    {
        using var notes = new Notes();
        var tab = await OpenDirtyAsync(notes, this.TestContext.CancellationToken).ConfigureAwait(false);
        var copyCompleted = new TaskCompletionSource<DocumentConflictResult>(TaskCreationOptions.RunContinuationsAsynchronously);
        var documents = new Mock<IDocumentService>();
        _ = documents.Setup(value => value.SaveCopyAsync(tab.Document, "Copy")).Returns(copyCompleted.Task);
        _ = documents.Setup(value => value.ReloadAsync(tab.Document)).Returns(async () =>
        {
            _ = await tab.Document.ReloadAsync().ConfigureAwait(false);
            return new DocumentConflictResult(Succeeded: true, "Reloaded");
        });
        var item = new DocumentCloseItem(tab, () => Task.FromResult(false), documents.Object);
        var copy = item.SaveCopyAsync("Copy");
        var reload = item.ReloadAsync();

        _ = item.Pending.IsCompleted.Should().BeFalse();
        _ = item.IsSaved.Should().BeFalse();
        documents.Verify(value => value.ReloadAsync(It.IsAny<Document>()), Times.Never);
        copyCompleted.SetResult(new(Succeeded: true, "Copied"));
        _ = (await copy.ConfigureAwait(false)).Succeeded.Should().BeTrue();
        _ = (await reload.ConfigureAwait(false)).Succeeded.Should().BeTrue();
        await item.Pending.ConfigureAwait(false);
        _ = item.IsSaved.Should().BeTrue();
        documents.Verify(value => value.ReloadAsync(tab.Document), Times.Once);
    }

    /// <summary>A failed action does not poison retries or permit closing a still-dirty original.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task FailedRecoveryCanRetryWithoutLosingTheDirtyDocument()
    {
        using var notes = new Notes();
        var tab = await OpenDirtyAsync(notes, this.TestContext.CancellationToken).ConfigureAwait(false);
        var documents = new Mock<IDocumentService>();
        _ = documents.SetupSequence(value => value.ReloadAsync(tab.Document))
            .ThrowsAsync(new IOException("Read denied"))
            .ReturnsAsync(new DocumentConflictResult(Succeeded: true, "Newer changes remain"));
        var item = new DocumentCloseItem(tab, () => Task.FromResult(false), documents.Object);

        var failed = item.ReloadAsync;
        _ = await failed.Should().ThrowExactlyAsync<IOException>().ConfigureAwait(false);
        await item.Pending.ConfigureAwait(false);
        _ = item.IsSaved.Should().BeFalse();
        _ = (await item.ReloadAsync().ConfigureAwait(false)).Succeeded.Should().BeTrue();
        _ = item.IsSaved.Should().BeFalse();
        _ = tab.IsDirty.Should().BeTrue();
    }

    private static async Task<DocumentTab> OpenDirtyAsync(Notes notes, CancellationToken cancellationToken)
    {
        var document = await notes.OpenAsync("Original", cancellationToken).ConfigureAwait(false);
        document.History.AddChange("Edit", static () => { });
        return new DocumentTab(document, new DocumentEditorDescriptor(notes.Kind, static _ => new object()));
    }
}
