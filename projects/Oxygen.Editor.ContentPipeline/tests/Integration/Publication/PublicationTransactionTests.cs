// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.TestSupport;

namespace Oxygen.Editor.ContentPipeline.Integration.Tests.Publication;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class PublicationTransactionTests
{
    public TestContext TestContext { get; set; } = null!;

    [TestMethod]
    public async Task CommitSelectsAllRootsWhileOldReadersRemainUsable()
    {
        using var project = new PublicationProject(hadPrevious: true);
        await using var staging = await project.StageAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        using var old = project.Baseline.Retain();
        var oldContent = old.FindProjectRoot("Content")!;
        var transaction = await project.PrepareAsync(staging, this.TestContext.CancellationToken).ConfigureAwait(false);
        await using var preview = new Preview(project.Root);
        using var accepted = await transaction.PublishAsync(preview, project.Baseline, static () => { }, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = transaction.Phase.Should().Be(CookPublicationPhase.Committed);
        project.AssertNew();
        _ = preview.Resumed.Should().BeTrue();
        _ = preview.MountedRoots.Should().HaveCount(3);
        _ = File.ReadAllText(Path.Combine(oldContent, "value.txt")).Should().Be("old:Content");
        using var gate = await CookOutputLease.AcquireWriteAsync(project.Root, this.TestContext.CancellationToken).ConfigureAwait(false);
        using var selected = await CookPublicationReadLease.OpenUnderGateAsync(project.Context, project.Files, gate, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = selected.PublicationId.Should().Be(accepted.PublicationId);
    }

    [TestMethod]
    [DataRow(true, "Applying")]
    [DataRow(true, "SourcesApplied")]
    [DataRow(true, "HeadSelected")]
    [DataRow(true, "RuntimeReady")]
    [DataRow(false, "Applying")]
    [DataRow(false, "SourcesApplied")]
    [DataRow(false, "HeadSelected")]
    [DataRow(false, "RuntimeReady")]
    public async Task FailureRestoresTheWholePublication(bool hadPrevious, string boundary)
    {
        using var project = new PublicationProject(hadPrevious);
        await using var staging = await project.StageAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var transaction = await project.PrepareAsync(staging, this.TestContext.CancellationToken,
            name => name == boundary ? Task.FromException(new IOException("Injected publication failure")) : Task.CompletedTask).ConfigureAwait(false);
        await using var preview = new Preview(project.Root);
        Func<Task> publish = () => transaction.PublishAsync(preview, project.Baseline, static () => { }, this.TestContext.CancellationToken);
        _ = await publish.Should().ThrowAsync<IOException>().ConfigureAwait(false);
        _ = transaction.Phase.Should().Be(CookPublicationPhase.RolledBack);
        project.AssertOld();
        _ = preview.Resumed.Should().BeTrue();
    }

    [TestMethod]
    public async Task FailedMountRestoresPreviousPublication()
    {
        using var project = new PublicationProject(hadPrevious: true);
        await using var staging = await project.StageAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var transaction = await project.PrepareAsync(staging, this.TestContext.CancellationToken).ConfigureAwait(false);
        await using var preview = new Preview(project.Root) { FailFirstMount = true };
        Func<Task> publish = () => transaction.PublishAsync(preview, project.Baseline, static () => { }, this.TestContext.CancellationToken);
        _ = await publish.Should().ThrowAsync<IOException>().ConfigureAwait(false);
        project.AssertOld();
        _ = preview.MountCount.Should().Be(2);
        _ = preview.Resumed.Should().BeTrue();
    }

    [TestMethod]
    public async Task FailedRollbackMountRetainsRecoverableState()
    {
        using var project = new PublicationProject(hadPrevious: true);
        await using var staging = await project.StageAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var transaction = await project.PrepareAsync(staging, this.TestContext.CancellationToken).ConfigureAwait(false);
        await using var preview = new Preview(project.Root) { FailAllMounts = true };
        Func<Task> publish = () => transaction.PublishAsync(preview, project.Baseline, static () => { }, this.TestContext.CancellationToken);
        _ = await publish.Should().ThrowAsync<AggregateException>().ConfigureAwait(false);
        project.AssertOld();
        _ = preview.Resumed.Should().BeFalse();
        using var gate = await CookOutputLease.AcquireWriteAsync(project.Root, this.TestContext.CancellationToken).ConfigureAwait(false);
        var recovery = await CookPublicationTransaction.LoadAsync(project.Context, project.Operation.OperationId, project.Files, project.Manager, gate, this.TestContext.CancellationToken).ConfigureAwait(false);
        await recovery.RecoverAsync(gate).ConfigureAwait(false);
        _ = recovery.Phase.Should().Be(CookPublicationPhase.RolledBack);
    }

    [TestMethod]
    public async Task CancellationAfterApplyingDefersUntilCommit()
    {
        using var project = new PublicationProject(hadPrevious: true);
        using var cancellation = new CancellationTokenSource();
        await using var staging = await project.StageAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var transaction = await project.PrepareAsync(staging, this.TestContext.CancellationToken,
            name => name == "Applying" ? cancellation.CancelAsync() : Task.CompletedTask).ConfigureAwait(false);
        using var accepted = await transaction.PublishAsync(preview: null, project.Baseline, static () => { }, cancellation.Token).ConfigureAwait(false);
        _ = transaction.Phase.Should().Be(CookPublicationPhase.Committed);
        project.AssertNew();
    }

    [TestMethod]
    [DataRow("SourcesApplied")]
    [DataRow("RuntimeReady")]
    public async Task LostProjectOwnershipRestoresThePreviousHead(string boundary)
    {
        using var project = new PublicationProject(hadPrevious: true);
        await using var staging = await project.StageAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var current = true;
        var transaction = await project.PrepareAsync(staging, this.TestContext.CancellationToken, name =>
        {
            if (name == boundary)
            {
                current = false;
            }

            return Task.CompletedTask;
        }).ConfigureAwait(false);
        void Verify()
        {
            if (!current)
            {
                throw new OperationCanceledException("Project closed");
            }
        }

        Func<Task> publish = () => transaction.PublishAsync(preview: null, project.Baseline, Verify, this.TestContext.CancellationToken);
        _ = await publish.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
        project.AssertOld();
    }

    [TestMethod]
    public async Task CancelledPreparedCookNeverPausesPreview()
    {
        using var project = new PublicationProject(hadPrevious: true);
        await using var staging = await project.StageAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var transaction = await project.PrepareAsync(staging, this.TestContext.CancellationToken).ConfigureAwait(false);
        await using var preview = new Preview(project.Root);
        Func<Task> publish = () => transaction.PublishAsync(preview, project.Baseline, static () => { }, new CancellationToken(canceled: true));
        _ = await publish.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
        project.AssertOld();
        _ = preview.Resumed.Should().BeTrue();
    }

    private sealed partial class Preview(string projectRoot) : ICookPublicationPreview
    {
        private global::Oxygen.Editor.ContentPipeline.Mounting.CookedContentMountSet? reader;

        public bool IsRuntimeAvailable => true;

        public bool FailFirstMount { get; init; }

        public bool FailAllMounts { get; init; }

        public bool Resumed { get; private set; } = true;

        public int MountCount { get; private set; }

        public IReadOnlyList<string> MountedRoots { get; private set; } = [];

        public Task PrepareReplacementAsync()
        {
            this.Resumed = false;
            return Task.CompletedTask;
        }

        public Task MountAsync(global::Oxygen.Editor.ContentPipeline.Mounting.CookedContentMountSet mounts)
        {
            this.MountCount++;
            if (this.FailAllMounts || (this.FailFirstMount && this.MountCount == 1))
            {
                mounts.Dispose();
                throw new IOException("Injected native mount failure");
            }

            var publication = mounts.Publication;
            if (!string.Equals(publication.ProjectRoot, projectRoot, StringComparison.OrdinalIgnoreCase))
            {
                mounts.Dispose();
                throw new InvalidOperationException("Unexpected preview project.");
            }

            this.reader?.Dispose();
            this.reader = mounts;
            this.MountedRoots = publication.RootPaths;
            return Task.CompletedTask;
        }

        public Task ResumeAsync()
        {
            this.Resumed = true;
            return Task.CompletedTask;
        }

        public Task CommittedAsync(CookPublicationReadLease publication) => Task.CompletedTask;

        public ValueTask DisposeAsync()
        {
            this.reader?.Dispose();
            this.reader = null;
            return ValueTask.CompletedTask;
        }
    }
}
