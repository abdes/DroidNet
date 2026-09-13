// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Publication;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Publication drains status scans while keeping persistent reader exclusion.</summary>
public sealed partial class CookOutputLeaseTests
{
    /// <summary>A pending publisher waits for inspections and excludes new ones before mutation.</summary>
    /// <returns>The asynchronous inspection-drain regression.</returns>
    [TestMethod]
    public async Task PublicationDrainsStatusInspectionsBeforeTakingOwnership()
    {
        using var project = new ProjectDirectory();
        using var inspection = await CookOutputLease.AcquireInspectionAsync(project.Root, this.TestContext.CancellationToken).ConfigureAwait(false);
        var publishing = CookOutputLease.AcquireWriteAsync(project.Root, this.TestContext.CancellationToken);
        _ = publishing.IsCompleted.Should().BeFalse();
        Action competingRead = () => CookOutputLease.AcquireRead(project.Root).Dispose();
        _ = competingRead.Should().Throw<CookOutputBusyException>();
        inspection.Dispose();
        using var writer = await publishing.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = writer.ProjectRoot.Should().Be(project.Root);
    }

    /// <summary>Cancelling a publisher leaves the active inspection intact and releases its gate.</summary>
    /// <returns>The asynchronous cancellation regression.</returns>
    [TestMethod]
    public async Task CancelledPublicationDoesNotInvalidateItsStatusReader()
    {
        using var project = new ProjectDirectory();
        using var inspection = await CookOutputLease.AcquireInspectionAsync(project.Root, this.TestContext.CancellationToken).ConfigureAwait(false);
        using var cancellation = new CancellationTokenSource();
        var publishing = CookOutputLease.AcquireWriteAsync(project.Root, cancellation.Token);
        await cancellation.CancelAsync().ConfigureAwait(false);
        _ = await ((Func<Task>)(() => publishing)).Should().ThrowExactlyAsync<TaskCanceledException>().ConfigureAwait(false);
        using var other = CookOutputLease.AcquireRead(project.Root);
        Action synchronousWriter = () => CookOutputLease.AcquireWrite(project.Root).Dispose();
        _ = synchronousWriter.Should().Throw<CookOutputBusyException>();
    }

    /// <summary>Finite-reader draining must not wait forever for another live preview.</summary>
    /// <returns>The asynchronous runtime-reader exclusion regression.</returns>
    [TestMethod]
    public async Task PublicationStillRejectsPersistentPreviewReaders()
    {
        using var project = new ProjectDirectory();
        using var preview = CookOutputLease.AcquireRead(project.Root);
        _ = await ((Func<Task>)(() => CookOutputLease.AcquireWriteAsync(project.Root, this.TestContext.CancellationToken))).Should().ThrowExactlyAsync<CookOutputBusyException>().ConfigureAwait(false);
    }
}
