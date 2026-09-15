// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Publication;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Checks that coherent verification never registers nested readers behind a waiting publisher.</summary>
public sealed partial class CookPublicationTransactionTests
{
    /// <summary>An existing inspection can finish journal and metadata reads while a publisher waits for it.</summary>
    /// <returns>The nested-reader deadlock regression.</returns>
    [TestMethod]
    public async Task CommittedVerificationFinishesUnderExistingReaderWhilePublisherWaits()
    {
        using var project = new PublicationProject(hadPrevious: true);
        using var cancellation = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        cancellation.CancelAfter(TimeSpan.FromSeconds(5));
        using var staging = await project.StageAsync(cancellation.Token).ConfigureAwait(false);
        var transaction = await project.PrepareAsync(staging, cancellation.Token).ConfigureAwait(false);
        await transaction.PublishAsync(preview: null, static () => { }, cancellation.Token).ConfigureAwait(false);
        using var reader = await CookOutputLease.AcquireInspectionAsync(project.Root, cancellation.Token).ConfigureAwait(false);
        var publication = CookOutputLease.AcquireWriteAsync(project.Root, cancellation.Token);
        _ = publication.IsCompleted.Should().BeFalse();
        var loaded = await CookPublicationTransaction.LoadReadOnlyUnderLeaseAsync(project.Context, project.Operation.OperationId, project.Files, cancellation.Token).ConfigureAwait(false);
        await loaded.VerifyCommittedMetadataUnderLeaseAsync().WaitAsync(cancellation.Token).ConfigureAwait(false);
        await loaded.VerifyCommittedUnderLeaseAsync().WaitAsync(cancellation.Token).ConfigureAwait(false);
        _ = publication.IsCompleted.Should().BeFalse();
        reader.Dispose();
        using var writer = await publication.WaitAsync(cancellation.Token).ConfigureAwait(false);
        project.AssertNew();
    }
}
