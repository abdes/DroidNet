// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text;
using AwesomeAssertions;
using DroidNet.Storage.Native;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Verifies multi-root publication, rollback and persisted interruption recovery.</summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed partial class CookPublicationTransactionTests
{
    /// <summary>Gets or sets the running test context.</summary>
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Successful publication updates all affected roots and metadata while leaving unrelated roots intact.</summary>
    /// <returns>The asynchronous publication regression.</returns>
    [TestMethod]
    public async Task CommitInstallsAllRootsAndHandsReadOwnershipToPreview()
    {
        using var project = new PublicationProject(hadPrevious: true);
        using var staging = await project.StageAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var transaction = await project.PrepareAsync(staging, this.TestContext.CancellationToken).ConfigureAwait(false);
        var preview = new Preview(project.Root);
        await using var previewLifetime = preview.ConfigureAwait(false);
        await transaction.PublishAsync(preview, static () => { }, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = transaction.Phase.Should().Be(CookPublicationPhase.Committed);
        project.AssertNew();
        _ = preview.Resumed.Should().BeTrue();
        _ = preview.MountedRoots.Should().HaveCount(3);
        Action competing = () => CookOutputLease.AcquireWrite(project.Root).Dispose();
        _ = competing.Should().Throw<CookOutputBusyException>();
        await preview.DisposeAsync().ConfigureAwait(false);
        using var writer = await CookOutputLease.AcquireWriteAsync(project.Root, this.TestContext.CancellationToken).ConfigureAwait(false);
        var loaded = await CookPublicationTransaction.LoadAsync(project.Context, project.Operation.OperationId, project.Files, writer, this.TestContext.CancellationToken).ConfigureAwait(false);
        await loaded.VerifyCommittedAsync(writer).ConfigureAwait(false);
        await loaded.CleanupAsync(writer).ConfigureAwait(false);
        project.AssertNew();
    }

    /// <summary>Failure between any directory or metadata steps restores the whole prior publication.</summary>
    /// <param name="hadPrevious">Whether previous output existed.</param>
    /// <param name="boundary">The boundary that fails.</param>
    /// <returns>The asynchronous failure regression.</returns>
    [TestMethod]
    [DataRow(true, "Retained:Content")]
    [DataRow(true, "Retained:Second")]
    [DataRow(true, "OldRetained")]
    [DataRow(true, "Installed:Content")]
    [DataRow(true, "Installed:Second")]
    [DataRow(true, "RootsInstalled")]
    [DataRow(true, "RuntimeReady")]
    [DataRow(true, "Metadata:.build/cook/provenance.json")]
    [DataRow(true, "Metadata:.cooked/publication.json")]
    [DataRow(false, "OldRetained")]
    [DataRow(false, "Installed:Content")]
    [DataRow(false, "Installed:Second")]
    [DataRow(false, "RootsInstalled")]
    [DataRow(false, "Metadata:.cooked/publication.json")]
    public async Task FailureRestoresRootsAndMetadata(bool hadPrevious, string boundary)
    {
        using var project = new PublicationProject(hadPrevious);
        using var staging = await project.StageAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var transaction = await project.PrepareAsync(
            staging,
            this.TestContext.CancellationToken,
            name => string.Equals(name, boundary, StringComparison.Ordinal) ? Task.FromException(new IOException("Injected publication failure")) : Task.CompletedTask).ConfigureAwait(false);
        var preview = new Preview(project.Root);
        await using var previewLifetime = preview.ConfigureAwait(false);
        Func<Task> publish = () => transaction.PublishAsync(preview, static () => { }, this.TestContext.CancellationToken);
        _ = await publish.Should().ThrowAsync<IOException>().ConfigureAwait(false);
        _ = transaction.Phase.Should().Be(CookPublicationPhase.RolledBack);
        project.AssertOld();
        _ = preview.Resumed.Should().BeTrue();
    }

    /// <summary>A failed new mount restores old roots before remounting the previous preview.</summary>
    /// <returns>The asynchronous runtime rollback regression.</returns>
    [TestMethod]
    public async Task FailedMountRestoresPreviousPublication()
    {
        using var project = new PublicationProject(hadPrevious: true);
        using var staging = await project.StageAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var transaction = await project.PrepareAsync(staging, this.TestContext.CancellationToken).ConfigureAwait(false);
        var preview = new Preview(project.Root) { FailFirstMount = true };
        await using var previewLifetime = preview.ConfigureAwait(false);
        Func<Task> publish = () => transaction.PublishAsync(preview, static () => { }, this.TestContext.CancellationToken);
        _ = await publish.Should().ThrowAsync<IOException>().ConfigureAwait(false);
        project.AssertOld();
        _ = preview.MountCount.Should().Be(2);
        _ = preview.Resumed.Should().BeTrue();
    }

    /// <summary>Failure to remount the restored output retains recovery state and does not resume preview.</summary>
    /// <returns>The asynchronous double-mount-failure regression.</returns>
    [TestMethod]
    public async Task FailedRollbackMountRetainsRecoverableState()
    {
        using var project = new PublicationProject(hadPrevious: true);
        using var staging = await project.StageAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var transaction = await project.PrepareAsync(staging, this.TestContext.CancellationToken).ConfigureAwait(false);
        var preview = new Preview(project.Root) { FailAllMounts = true };
        await using var previewLifetime = preview.ConfigureAwait(false);
        Func<Task> publish = () => transaction.PublishAsync(preview, static () => { }, this.TestContext.CancellationToken);
        _ = await publish.Should().ThrowAsync<AggregateException>().ConfigureAwait(false);
        project.AssertOld();
        _ = preview.Resumed.Should().BeFalse();
        _ = transaction.Phase.Should().NotBe(CookPublicationPhase.RolledBack);
        await preview.DisposeAsync().ConfigureAwait(false);
        using var writer = await CookOutputLease.AcquireWriteAsync(project.Root, this.TestContext.CancellationToken).ConfigureAwait(false);
        var recovery = await CookPublicationTransaction.LoadAsync(project.Context, project.Operation.OperationId, project.Files, writer, this.TestContext.CancellationToken).ConfigureAwait(false);
        await recovery.RecoverAsync(writer).ConfigureAwait(false);
        await recovery.CleanupAsync(writer).ConfigureAwait(false);
        project.AssertOld();
    }

    /// <summary>A busy standalone reader causes no replacement and the paused preview is restored.</summary>
    /// <returns>The asynchronous busy-output regression.</returns>
    [TestMethod]
    public async Task BusyReaderRestoresUnchangedPreview()
    {
        using var project = new PublicationProject(hadPrevious: true);
        using var staging = await project.StageAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var transaction = await project.PrepareAsync(staging, this.TestContext.CancellationToken).ConfigureAwait(false);
        using var external = await CookOutputLease.AcquireReadAsync(project.Root, this.TestContext.CancellationToken).ConfigureAwait(false);
        var preview = new Preview(project.Root);
        await using var previewLifetime = preview.ConfigureAwait(false);
        Func<Task> publish = () => transaction.PublishAsync(preview, static () => { }, this.TestContext.CancellationToken);
        _ = await publish.Should().ThrowAsync<CookOutputBusyException>().ConfigureAwait(false);
        project.AssertOld();
        _ = preview.Resumed.Should().BeTrue();
    }

    /// <summary>Cancellation during replacement finishes the actual committed outcome.</summary>
    /// <returns>The asynchronous critical-section cancellation regression.</returns>
    [TestMethod]
    public async Task CancellationAfterReplacementStartsDefersUntilCommit()
    {
        using var project = new PublicationProject(hadPrevious: true);
        using var cancellation = new CancellationTokenSource();
        using var staging = await project.StageAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var transaction = await project.PrepareAsync(staging, this.TestContext.CancellationToken, async name =>
        {
            if (string.Equals(name, "Installed:Content", StringComparison.Ordinal))
            {
                await cancellation.CancelAsync().ConfigureAwait(false);
            }
        }).ConfigureAwait(false);
        await transaction.PublishAsync(preview: null, static () => { }, cancellation.Token).ConfigureAwait(false);
        _ = transaction.Phase.Should().Be(CookPublicationPhase.Committed);
        project.AssertNew();
    }

    /// <summary>A project closed during replacement cannot commit its partially installed roots.</summary>
    /// <param name="boundary">The point where project ownership changes.</param>
    /// <returns>The asynchronous ownership regression.</returns>
    [TestMethod]
    [DataRow("Installed:Content")]
    [DataRow("Metadata:.cooked/publication.json")]
    public async Task LostProjectOwnershipRollsBackAllRoots(string boundary)
    {
        using var project = new PublicationProject(hadPrevious: true);
        using var staging = await project.StageAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var current = true;
        var transaction = await project.PrepareAsync(staging, this.TestContext.CancellationToken, name =>
        {
            if (string.Equals(name, boundary, StringComparison.Ordinal))
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

        Func<Task> publish = () => transaction.PublishAsync(preview: null, Verify, this.TestContext.CancellationToken);
        _ = await publish.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
        project.AssertOld();
    }

    /// <summary>Cancellation before publication leaves the running preview and published files untouched.</summary>
    /// <returns>The asynchronous cancellation regression.</returns>
    [TestMethod]
    public async Task CancelledPreparedCookNeverPausesPreview()
    {
        using var project = new PublicationProject(hadPrevious: true);
        using var staging = await project.StageAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var transaction = await project.PrepareAsync(staging, this.TestContext.CancellationToken).ConfigureAwait(false);
        var preview = new Preview(project.Root);
        await using var previewLifetime = preview.ConfigureAwait(false);
        Func<Task> publish = () => transaction.PublishAsync(preview, static () => { }, new CancellationToken(canceled: true));
        _ = await publish.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
        _ = preview.Resumed.Should().BeTrue();
        _ = preview.MountCount.Should().Be(0);
        project.AssertOld();
    }

    private sealed partial class Preview(string projectRoot) : ICookPublicationPreview
    {
        private readonly List<FileStream> openFiles = [];
        private IDisposable? reader = CookOutputLease.AcquireRead(projectRoot);

        public bool IsRuntimeAvailable => true;

        public bool FailFirstMount { get; init; }

        public bool FailAllMounts { get; init; }

        public bool Resumed { get; private set; } = true;

        public int MountCount { get; private set; }

        public IReadOnlyList<string> MountedRoots { get; private set; } = [];

        public async Task PrepareReplacementAsync()
        {
            this.Resumed = false;
            foreach (var file in this.openFiles)
            {
                await file.DisposeAsync().ConfigureAwait(false);
            }

            this.openFiles.Clear();
            this.reader?.Dispose();
            this.reader = null;
        }

        public async Task MountAsync(IReadOnlyList<string> roots, CookOutputWriteLease? writer)
        {
            this.MountCount++;
            this.reader = writer?.CreateReader() ?? await CookOutputLease.AcquireReadAsync(projectRoot, CancellationToken.None).ConfigureAwait(false);
            this.MountedRoots = roots;
            this.openFiles.AddRange(roots.Select(root => new FileStream(Path.Combine(root, "container.index.bin"), FileMode.Open, FileAccess.Read, FileShare.Read)));
            if (this.FailAllMounts || (this.FailFirstMount && this.MountCount == 1))
            {
                throw new IOException("Injected native mount failure");
            }
        }

        public Task ResumeAsync()
        {
            this.Resumed = true;
            return Task.CompletedTask;
        }

        public async ValueTask DisposeAsync() => await this.PrepareReplacementAsync().ConfigureAwait(false);
    }

    private sealed partial class PublicationProject : IDisposable
    {
        private readonly DirectoryInfo directory = Directory.CreateTempSubdirectory("oxygen-publication-");
        private readonly bool hadPrevious;

        public PublicationProject(bool hadPrevious)
        {
            this.hadPrevious = hadPrevious;
            this.Context = new ProjectContext
            {
                ProjectId = Guid.NewGuid(), ProjectRoot = this.Root, Name = "Publication", Category = Category.Games,
                AuthoringMounts = [new("Content", "Content"), new("Second", "Second")], LocalFolderMounts = [], Scenes = [],
            };
            this.Operation = new(Guid.NewGuid(), this.Context, 1);
            if (hadPrevious)
            {
                foreach (var mount in new[] { "Content", "Second", "Unrelated" })
                {
                    this.Write(".cooked/" + mount + "/container.index.bin", "old:" + mount);
                    this.Write(".cooked/" + mount + "/keep.bin", "keep:" + mount);
                }

                this.Write(CookPublicationTransaction.PublicationMetadata, "old-receipt");
                this.Write(CookPublicationTransaction.ProvenanceMetadata, "old-provenance");
            }
        }

        public string Root => this.directory.FullName;

        public ProjectContext Context { get; }

        public ContentCookOperation Operation { get; }

        public NativeAtomicFileStore Files { get; } = new(new Testably.Abstractions.RealFileSystem());

        public async Task<CookStagingArea> StageAsync(CancellationToken cancellationToken)
        {
            var staging = await CookStagingArea.CreateAsync(this.Operation, ["Content", "Second"], cancellationToken).ConfigureAwait(false);
            foreach (var root in staging.Roots)
            {
                await File.WriteAllTextAsync(Path.Combine(root.StagingPath, "container.index.bin"), "new:" + root.Mount, cancellationToken).ConfigureAwait(false);
            }

            return staging;
        }

        public Task<CookPublicationTransaction> PrepareAsync(CookStagingArea staging, CancellationToken cancellationToken, Func<string, Task>? checkpoint = null, CookSourceReplacement? sourceReplacement = null)
            => CookPublicationTransaction.PrepareAsync(
                this.Operation,
                staging,
                new Dictionary<string, byte[]>(StringComparer.Ordinal)
                {
                    [CookPublicationTransaction.PublicationMetadata] = Encoding.UTF8.GetBytes("new-receipt"),
                    [CookPublicationTransaction.ProvenanceMetadata] = Encoding.UTF8.GetBytes("new-provenance"),
                },
                this.Files,
                cancellationToken,
                checkpoint,
                sourceReplacement);

        public void AssertOld()
        {
            foreach (var mount in new[] { "Content", "Second" })
            {
                var path = Path.Combine(this.Root, ".cooked", mount, "container.index.bin");
                if (this.hadPrevious)
                {
                    _ = File.ReadAllText(path).Should().Be("old:" + mount);
                }
                else
                {
                    _ = Directory.Exists(Path.GetDirectoryName(path)).Should().BeFalse();
                }
            }

            this.AssertMetadata(CookPublicationTransaction.PublicationMetadata, "old-receipt");
            this.AssertMetadata(CookPublicationTransaction.ProvenanceMetadata, "old-provenance");
        }

        public void AssertNew()
        {
            foreach (var mount in new[] { "Content", "Second" })
            {
                _ = File.ReadAllText(Path.Combine(this.Root, ".cooked", mount, "container.index.bin")).Should().Be("new:" + mount);
                if (this.hadPrevious)
                {
                    _ = File.ReadAllText(Path.Combine(this.Root, ".cooked", mount, "keep.bin")).Should().Be("keep:" + mount);
                }
            }

            _ = File.ReadAllText(Path.Combine(this.Root, CookPublicationTransaction.PublicationMetadata)).Should().Be("new-receipt");
            _ = File.ReadAllText(Path.Combine(this.Root, CookPublicationTransaction.ProvenanceMetadata)).Should().Be("new-provenance");
            if (this.hadPrevious)
            {
                _ = File.ReadAllText(Path.Combine(this.Root, ".cooked", "Unrelated", "container.index.bin")).Should().Be("old:Unrelated");
            }
        }

        public void Dispose() => this.directory.Delete(recursive: true);

        private void Write(string relative, string text)
        {
            var path = Path.Combine(this.Root, relative);
            _ = Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            File.WriteAllText(path, text);
        }

        private void AssertMetadata(string relative, string previous)
        {
            var path = Path.Combine(this.Root, relative);
            if (this.hadPrevious)
            {
                _ = File.ReadAllText(path).Should().Be(previous);
            }
            else
            {
                _ = File.Exists(path).Should().BeFalse();
            }
        }
    }
}
