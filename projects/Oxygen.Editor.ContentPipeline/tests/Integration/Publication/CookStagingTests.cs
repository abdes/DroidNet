// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using AwesomeAssertions;
using DroidNet.Storage;
using DroidNet.Storage.Native;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Testing;
using Testably.Abstractions;

namespace Oxygen.Editor.ContentPipeline.Integration.Tests.Publication;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class CookStagingTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Native edits to staging leave published bytes intact and preserve seeded unrelated files.</summary>
    /// <returns>The asynchronous staging regression.</returns>
    [TestMethod]
    public async Task SeededOutputIsPrivateAndPreservesUnrelatedFiles()
    {
        using var project = new StagingProject();
        var root = project.PublishedRoot;
        _ = Directory.CreateDirectory(root);
        var changed = Path.Combine(root, "changed.bin");
        var unrelated = Path.Combine(root, "unrelated.bin");
        await File.WriteAllTextAsync(changed, "old", this.TestContext.CancellationToken).ConfigureAwait(false);
        await File.WriteAllTextAsync(unrelated, "keep", this.TestContext.CancellationToken).ConfigureAwait(false);
        var timestamp = new DateTime(2024, 1, 2, 3, 4, 5, DateTimeKind.Utc);
        File.SetLastWriteTimeUtc(unrelated, timestamp);
        await using var staging = await project.StageAsync(["Content"], this.TestContext.CancellationToken).ConfigureAwait(false);
        var output = staging.Roots.Single().Path;
        await File.WriteAllTextAsync(Path.Combine(output, "changed.bin"), "new", this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = (await File.ReadAllTextAsync(changed, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Be("old");
        _ = (await File.ReadAllTextAsync(Path.Combine(output, "unrelated.bin"), this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Be("keep");
        _ = File.GetLastWriteTimeUtc(Path.Combine(output, "unrelated.bin")).Should().Be(timestamp);
        await staging.DisposeAsync().ConfigureAwait(false);
        _ = Directory.Exists(output).Should().BeFalse();
        _ = File.GetLastWriteTimeUtc(unrelated).Should().Be(timestamp);
    }

    /// <summary>First publication prepares private output without creating a published root.</summary>
    /// <returns>The asynchronous first-cook regression.</returns>
    [TestMethod]
    public async Task FirstCookDoesNotCreatePublishedOutputDuringPreparation()
    {
        using var project = new StagingProject();
        await using var staging = await project.StageAsync(["Content"], this.TestContext.CancellationToken).ConfigureAwait(false);
        var root = staging.Roots.Single();
        _ = root.Before.Exists.Should().BeFalse();
        _ = Directory.Exists(root.Path).Should().BeTrue();
        _ = File.Exists(CookPublicationPaths.Head(project.Root)).Should().BeFalse();
    }

    /// <summary>Brief catalog registration contention delays staging instead of failing the cook.</summary>
    /// <returns>The asynchronous registration-race regression.</returns>
    [TestMethod]
    public async Task StagingWaitsForReaderRegistrationGate()
    {
        using var project = new StagingProject();
        using var cancellation = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        cancellation.CancelAfter(TimeSpan.FromSeconds(5));
        using (await CookOutputLease.AcquireWriteAsync(project.Root, cancellation.Token).ConfigureAwait(false))
        {
        }

        var gate = new FileStream(Path.Combine(project.Root, ".build", "cook", "publication.lock"), FileMode.Open, FileAccess.ReadWrite, FileShare.None);
        await using var gateLifetime = gate.ConfigureAwait(false);
        var pending = project.StageAsync(["Content"], cancellation.Token);
        _ = pending.IsCompleted.Should().BeFalse();
        _ = Directory.Exists(Path.Combine(project.Root, ".build", "cook", project.Operation.OperationId.ToString("N"), "output")).Should().BeFalse();
        await gate.DisposeAsync().ConfigureAwait(false);
        await using var staging = await pending.WaitAsync(cancellation.Token).ConfigureAwait(false);
        _ = staging.Roots.Should().ContainSingle();
        _ = File.Exists(CookPublicationPaths.Head(project.Root)).Should().BeFalse();
    }

    /// <summary>Reusing an operation identity cannot overwrite or clean up its existing staging.</summary>
    /// <returns>The asynchronous ownership regression.</returns>
    [TestMethod]
    public async Task ExistingStagingIsNotOverwritten()
    {
        using var project = new StagingProject();
        await using var first = await project.StageAsync(["Content"], this.TestContext.CancellationToken).ConfigureAwait(false);
        var marker = Path.Combine(first.Roots.Single().Path, "keep.bin");
        await File.WriteAllTextAsync(marker, "keep", this.TestContext.CancellationToken).ConfigureAwait(false);
        Func<Task> duplicate = () => project.StageAsync(["Content"], this.TestContext.CancellationToken);
        _ = await duplicate.Should().ThrowAsync<IOException>().ConfigureAwait(false);
        _ = (await File.ReadAllTextAsync(marker, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Be("keep");
    }

    /// <summary>A failed protected read removes only its private staging and preserves published output.</summary>
    /// <returns>The asynchronous preparation-failure regression.</returns>
    [TestMethod]
    public async Task SeedFailurePreservesPublishedFilesAndReleasesPrivateOutput()
    {
        using var project = new StagingProject();
        var root = project.PublishedRoot;
        _ = Directory.CreateDirectory(root);
        var path = Path.Combine(root, "asset.bin");
        await File.WriteAllTextAsync(path, "keep", this.TestContext.CancellationToken).ConfigureAwait(false);
        await project.CaptureBaselineAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var external = new FileStream(path, FileMode.Open, FileAccess.ReadWrite, FileShare.None);
        await using var externalLifetime = external.ConfigureAwait(false);
        Func<Task> prepare = () => project.StageAsync(["Content"], this.TestContext.CancellationToken);
        _ = await prepare.Should().ThrowAsync<IOException>().ConfigureAwait(false);
        var output = Path.Combine(project.Root, ".build", "cook", project.Operation.OperationId.ToString("N"), "output");
        _ = Directory.Exists(output).Should().BeFalse();
        await external.DisposeAsync().ConfigureAwait(false);
        _ = (await File.ReadAllTextAsync(path, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Be("keep");
    }

    /// <summary>Equal size and timestamp do not hide an external output change.</summary>
    /// <returns>The asynchronous content-identity regression.</returns>
    [TestMethod]
    public async Task RootIdentityDetectsChangedBytesWithUnchangedMetadata()
    {
        using var project = new StagingProject();
        var root = project.PublishedRoot;
        _ = Directory.CreateDirectory(root);
        var path = Path.Combine(root, "asset.bin");
        await File.WriteAllTextAsync(path, "old", this.TestContext.CancellationToken).ConfigureAwait(false);
        await using var staging = await project.StageAsync(["Content"], this.TestContext.CancellationToken).ConfigureAwait(false);
        var timestamp = File.GetLastWriteTimeUtc(path);
        await File.WriteAllTextAsync(path, "new", this.TestContext.CancellationToken).ConfigureAwait(false);
        File.SetLastWriteTimeUtc(path, timestamp);
        var current = await CookRootImage.CaptureAsync(root, copyTo: null, this.TestContext.CancellationToken, excludeGenerationMarker: true).ConfigureAwait(false);
        _ = staging.Roots.Single().Before.Matches(current).Should().BeFalse();
    }

    /// <summary>Traversal and Windows path aliases are rejected before staging is created.</summary>
    /// <param name="mount">The invalid mount name.</param>
    /// <returns>The asynchronous path-scope regression.</returns>
    [TestMethod]
    [DataRow("..")]
    [DataRow("../outside")]
    [DataRow("Content.")]
    [DataRow("Content ")]
    public async Task InvalidMountCannotEscapeOrAliasPublication(string mount)
    {
        using var project = new StagingProject();
        Func<Task> prepare = () => project.StageAsync([mount], this.TestContext.CancellationToken);
        _ = await prepare.Should().ThrowAsync<ArgumentException>().ConfigureAwait(false);
        _ = File.Exists(Path.Combine(project.Root, ".build", "cook", project.Operation.OperationId.ToString("N"), "publication.json")).Should().BeFalse();
    }

    private sealed partial class StagingProject : IDisposable
    {
        private readonly DirectoryInfo directory = Directory.CreateTempSubdirectory("oxygen-cook-staging-");
        private readonly Guid sourceKey = Guid.CreateVersion7();
        private readonly NativeAtomicFileStore files = new(new RealFileSystem());
        private readonly ProjectManagerService manager = new(new NativeStorageProvider(new RealFileSystem()));
        private CookPublicationReadLease? baseline;
        private FileStream? ownership;
        public StagingProject()
        {
            this.Operation = new(
                Guid.NewGuid(),
                new ProjectContext
                {
                    ProjectId = Guid.NewGuid(),
                    ProjectRoot = this.Root,
                    Name = "Staging",
                    Category = Category.Games,
                    AuthoringMounts = [new("Content", "Content")],
                    LocalFolderMounts = [],
                    Scenes = [],
                },
                1);
        }

        public string Root => this.directory.FullName;

        public ContentCookOperation Operation { get; }

        public string PublishedRoot => CookPublicationPaths.Generation(this.Root, this.sourceKey);

        public async Task<CookStagingArea> StageAsync(IEnumerable<string> mounts, CancellationToken token)
        {
            var baseline = await this.CaptureBaselineAsync(token).ConfigureAwait(false);
            return await CookStagingArea.CreateAsync(this.Operation, baseline, mounts, this.files, this.manager, token).ConfigureAwait(false);
        }

        public async Task<CookPublicationReadLease> CaptureBaselineAsync(CancellationToken token)
        {
            this.ownership ??= CookOutputLease.AcquireOperation(this.Root, this.Operation.OperationId);
            if (this.baseline is null)
            {
                using var gate = await CookOutputLease.AcquireWriteAsync(this.Root, token).ConfigureAwait(false);
                if (Directory.Exists(this.PublishedRoot))
                {
                    NativeInventoryFixture.WriteIndex(this.PublishedRoot, [], this.sourceKey);
                    File.WriteAllBytes(Path.Combine(this.PublishedRoot, CookedGeneration.MarkerFileName), []);
                    var digest = NativeInventoryFixture.Read(this.PublishedRoot).IndexSha256;
                    var document = new CookPublicationDocument(CookPublicationDocument.CurrentVersion, this.Operation.Project.ProjectId,
                        Guid.NewGuid(), DateTimeOffset.UtcNow, CookPublicationDocument.ConfigurationIdentity(this.Operation.Project),
                        [new(CookPublicationRootOwner.Project, "Content", this.sourceKey, digest, null)], [], null);
                    var path = CookPublicationPaths.Document(this.Root, document.OperationId);
                    Directory.CreateDirectory(Path.GetDirectoryName(path)!);
                    var version = await this.files.WriteAsync(path, JsonSerializer.SerializeToUtf8Bytes(document, CookPublicationDocument.JsonOptions), FileVersion.Missing, token).ConfigureAwait(false);
                    var head = new CookPublicationHead(CookPublicationHead.CurrentVersion, document.OperationId, version.Sha256);
                    _ = await this.files.WriteAsync(CookPublicationPaths.Head(this.Root), JsonSerializer.SerializeToUtf8Bytes(head, CookPublicationDocument.JsonOptions), FileVersion.Missing, token).ConfigureAwait(false);
                }

                this.baseline = await CookPublicationReadLease.OpenUnderGateAsync(this.Operation.Project, this.files, gate, token).ConfigureAwait(false);
            }

            return this.baseline;
        }

        public void Dispose()
        {
            this.baseline?.Dispose();
            this.ownership?.Dispose();
            this.directory.Delete(recursive: true);
        }
    }
}
