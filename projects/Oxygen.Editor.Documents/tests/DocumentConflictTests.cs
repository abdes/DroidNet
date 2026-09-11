// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Moq;

namespace Oxygen.Editor.Documents.Tests;

/// <summary>Verifies queued recovery actions and the original document's close eligibility.</summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository's discovery configuration.")]
public sealed class DocumentConflictTests
{
    /// <summary>Recovery actions queue behind pending I/O and only a clean reload acknowledges the original.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task CopyAndReloadSerializeWithoutAcknowledgingTheOriginalEarly()
    {
        var metadata = new TestDocumentMetadata { IsDirty = true };
        var copyCompleted = new TaskCompletionSource<DocumentConflictResult>(TaskCreationOptions.RunContinuationsAsynchronously);
        var participant = new Mock<IDocumentConflictParticipant>();
        _ = participant.Setup(value => value.SaveCopyAsync("Copy")).Returns(copyCompleted.Task);
        _ = participant.Setup(value => value.ReloadFromDiskAsync()).Returns(() =>
        {
            metadata.IsDirty = false;
            return Task.FromResult(new DocumentConflictResult(Succeeded: true, "Reloaded"));
        });
        var item = new DocumentCloseItem(metadata, () => Task.FromResult(false), participant.Object);
        var copy = item.SaveCopyAsync("Copy");
        var reload = item.ReloadAsync();

        _ = item.Pending.IsCompleted.Should().BeFalse();
        _ = item.IsSaved.Should().BeFalse();
        participant.Verify(value => value.ReloadFromDiskAsync(), Times.Never);
        copyCompleted.SetResult(new(Succeeded: true, "Copied"));
        _ = (await copy.ConfigureAwait(false)).Succeeded.Should().BeTrue();
        _ = (await reload.ConfigureAwait(false)).Succeeded.Should().BeTrue();
        await item.Pending.ConfigureAwait(false);
        _ = item.IsSaved.Should().BeTrue();
        participant.Verify(value => value.ReloadFromDiskAsync(), Times.Once);
    }

    /// <summary>A failed action does not poison retries or permit closing a still-dirty original.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task FailedRecoveryCanRetryWithoutLosingTheDirtyDocument()
    {
        var metadata = new TestDocumentMetadata { IsDirty = true };
        var participant = new Mock<IDocumentConflictParticipant>();
        _ = participant.SetupSequence(value => value.ReloadFromDiskAsync())
            .ThrowsAsync(new IOException("Read denied"))
            .ReturnsAsync(new DocumentConflictResult(Succeeded: true, "Newer changes remain"));
        var item = new DocumentCloseItem(metadata, () => Task.FromResult(false), participant.Object);

        var failed = item.ReloadAsync;
        _ = await failed.Should().ThrowExactlyAsync<IOException>().ConfigureAwait(false);
        await item.Pending.ConfigureAwait(false);
        _ = item.IsSaved.Should().BeFalse();
        _ = (await item.ReloadAsync().ConfigureAwait(false)).Succeeded.Should().BeTrue();
        _ = item.IsSaved.Should().BeFalse();
        _ = metadata.IsDirty.Should().BeTrue();
    }
}
