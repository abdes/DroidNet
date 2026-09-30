// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Text.Json;
using DroidNet.Storage;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Testing;
using Testably.Abstractions;
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
        private Mounting.CookedContentMountSet? reader;

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

        public Task MountAsync(Mounting.CookedContentMountSet mounts)
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

    private sealed partial class PublicationProject : IDisposable
    {
        private readonly DirectoryInfo directory = Directory.CreateTempSubdirectory("oxygen-publication-");
        private readonly bool hadPrevious;
        private readonly ImmutableArray<CookPublicationRoot> originalRoots;
        private readonly byte[]? originalHead;
        private FileStream? ownership;
        private CookPublicationReadLease? baseline;
        private Func<string, Task> checkpoint = static _ => Task.CompletedTask;

        public PublicationProject(bool hadPrevious)
        {
            this.hadPrevious = hadPrevious;
            var info = new ProjectInfo(Guid.NewGuid(), "Publication", Category.Games, this.Root)
            {
                AuthoringMounts = [new("Content", "Content"), new("Second", "Second"), new("Unrelated", "Unrelated")],
            };
            this.Context = ProjectContext.FromProjectInfo(info, []);
            this.Operation = new(Guid.NewGuid(), this.Context, 1);
            this.Write("Project.oxy", ProjectInfo.ToJson(info));
            var roots = ImmutableArray.CreateBuilder<CookPublicationRoot>();
            if (hadPrevious)
            {
                foreach (var mount in new[] { "Content", "Second", "Unrelated" })
                {
                    var key = Guid.CreateVersion7();
                    var root = CookPublicationPaths.Generation(this.Root, key);
                    Directory.CreateDirectory(root);
                    File.WriteAllText(Path.Combine(root, "value.txt"), "old:" + mount);
                    File.WriteAllText(Path.Combine(root, "keep.bin"), "keep:" + mount);
                    NativeInventoryFixture.WriteIndex(root, [], key);
                    File.WriteAllBytes(Path.Combine(root, CookedGeneration.MarkerFileName), []);
                    roots.Add(new(CookPublicationRootOwner.Project, mount, key, NativeInventoryFixture.Read(root).IndexSha256, null));
                }

                var document = new CookPublicationDocument(CookPublicationDocument.CurrentVersion, this.Context.ProjectId, Guid.NewGuid(),
                    DateTimeOffset.UtcNow, CookPublicationDocument.ConfigurationIdentity(this.Context), roots.ToImmutable(), [], null);
                var bytes = JsonSerializer.SerializeToUtf8Bytes(document, CookPublicationDocument.JsonOptions);
                var path = CookPublicationPaths.Document(this.Root, document.OperationId);
                Directory.CreateDirectory(Path.GetDirectoryName(path)!);
                File.WriteAllBytes(path, bytes);
                this.originalHead = JsonSerializer.SerializeToUtf8Bytes(new CookPublicationHead(CookPublicationHead.CurrentVersion,
                    document.OperationId, Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(bytes))), CookPublicationDocument.JsonOptions);
                File.WriteAllBytes(CookPublicationPaths.Head(this.Root), this.originalHead);
            }

            this.originalRoots = roots.ToImmutable();
        }

        public string Root => this.directory.FullName;
        public ProjectContext Context { get; }
        public ContentCookOperation Operation { get; }
        public NativeAtomicFileStore Files { get; } = new(new RealFileSystem());
        public ProjectManagerService Manager { get; } = new(new NativeStorageProvider(new RealFileSystem()));
        public CookPublicationReadLease Baseline => this.baseline ?? throw new InvalidOperationException("Prepare staging first.");

        public async Task<CookStagingArea> StageAsync(CancellationToken cancellationToken)
        {
            this.ownership = CookOutputLease.AcquireOperation(this.Root, this.Operation.OperationId);
            using (var gate = await CookOutputLease.AcquireWriteAsync(this.Root, cancellationToken).ConfigureAwait(false))
            {
                this.baseline = await CookPublicationReadLease.OpenUnderGateAsync(this.Context, this.Files, gate, cancellationToken).ConfigureAwait(false);
            }

            var staging = await CookStagingArea.CreateAsync(this.Operation, this.baseline, ["Content", "Second"], this.Files, this.Manager,
                cancellationToken, checkpoint: name => this.checkpoint(name)).ConfigureAwait(false);
            foreach (var root in staging.Roots)
            {
                await File.WriteAllTextAsync(Path.Combine(root.Path, "value.txt"), "new:" + root.Mount, cancellationToken).ConfigureAwait(false);
                NativeInventoryFixture.WriteIndex(root.Path, [], root.SourceKey);
                var opening = await CookOutputReadLease.AcquireAsync(root.Path, cancellationToken).ConfigureAwait(false);
                root.AcceptVerification(opening, NativeInventoryFixture.Read(root.Path));
            }

            return staging;
        }

        public async Task<CookPublicationTransaction> PrepareAsync(CookStagingArea staging, CancellationToken cancellationToken,
            Func<string, Task>? checkpoint = null, CookSourceReplacement? sourceReplacement = null, ImmutableArray<CookProducedSourceFile> producedSourceFiles = default)
        {
            this.checkpoint = checkpoint ?? (static _ => Task.CompletedTask);
            var replacements = staging.SealRoots();
            var roots = staging.Baseline.Roots.Where(root => !replacements.Any(replacement => replacement.Name == root.Name)).Concat(replacements).ToImmutableArray();
            var document = new CookPublicationDocument(CookPublicationDocument.CurrentVersion, this.Context.ProjectId, this.Operation.OperationId,
                DateTimeOffset.UtcNow, CookPublicationDocument.ConfigurationIdentity(this.Context), roots, [], null);
            await staging.Transaction.PrepareAsync(this.Operation, document, sourceReplacement,
                producedSourceFiles.IsDefault ? [] : producedSourceFiles, projectChange: null, cancellationToken).ConfigureAwait(false);
            staging.RetainForPublication();
            return staging.Transaction;
        }

        public void AssertOld()
        {
            var head = CookPublicationPaths.Head(this.Root);
            if (this.hadPrevious)
            {
                _ = File.ReadAllBytes(head).Should().Equal(this.originalHead!);
                foreach (var root in this.originalRoots)
                {
                    _ = File.ReadAllText(Path.Combine(root.ResolvePath(this.Root), "value.txt")).Should().Be("old:" + root.Name);
                }
            }
            else
            {
                _ = File.Exists(head).Should().BeFalse();
            }
        }

        public void AssertNew()
        {
            var head = JsonSerializer.Deserialize<CookPublicationHead>(File.ReadAllBytes(CookPublicationPaths.Head(this.Root)), CookPublicationDocument.JsonOptions)!;
            _ = head.PublicationId.Should().Be(this.Operation.OperationId);
            var document = JsonSerializer.Deserialize<CookPublicationDocument>(File.ReadAllBytes(CookPublicationPaths.Document(this.Root, head.PublicationId)), CookPublicationDocument.JsonOptions)!;
            foreach (var mount in new[] { "Content", "Second" })
            {
                var root = document.Roots.Single(root => root.Name == mount);
                _ = File.ReadAllText(Path.Combine(root.ResolvePath(this.Root), "value.txt")).Should().Be("new:" + mount);
                if (this.hadPrevious)
                {
                    _ = File.ReadAllText(Path.Combine(root.ResolvePath(this.Root), "keep.bin")).Should().Be("keep:" + mount);
                }
            }

            if (this.hadPrevious)
            {
                _ = document.Roots.Single(root => root.Name == "Unrelated").Should().Be(this.originalRoots.Single(root => root.Name == "Unrelated"));
            }
        }

        public void Dispose()
        {
            this.baseline?.Dispose();
            this.ownership?.Dispose();
            this.directory.Delete(recursive: true);
        }

        private void Write(string relative, string text)
        {
            var path = Path.Combine(this.Root, relative);
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            File.WriteAllText(path, text);
        }
    }
}
