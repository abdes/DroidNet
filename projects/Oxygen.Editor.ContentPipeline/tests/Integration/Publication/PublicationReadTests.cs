// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using AwesomeAssertions;
using DroidNet.Storage;
using DroidNet.Storage.Native;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.ContentPipeline.Mounting;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.TestSupport;
using Oxygen.Managed.Core;
using Oxygen.Testing;
using Testably.Abstractions;
using static Oxygen.Editor.ContentPipeline.TestSupport.GenerationScenario;

namespace Oxygen.Editor.ContentPipeline.Integration.Tests.Publication;

[TestClass]
public sealed class PublicationReadTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Old readers retain the exact prior roots without excluding a new publication.</summary>
    /// <returns>The asynchronous head and lifetime regression.</returns>
    [TestMethod]
    public async Task CapturedPublicationSurvivesHeadReplacement()
    {
        using var project = new ProjectDirectory();
        var context = PublicationContext(project.Root);
        var files = new NativeAtomicFileStore(new RealFileSystem());
        using var firstGate = await CookOutputLease.AcquireWriteAsync(FileSystemPath.From(project.Root), this.TestContext.CancellationToken).ConfigureAwait(false);
        var oldRoot = await WritePublicationFixtureAsync(context, files, FileVersion.Missing, this.TestContext.CancellationToken).ConfigureAwait(false);
        using var old = await CookPublicationReadLease.OpenUnderGateAsync(context, files, firstGate, this.TestContext.CancellationToken).ConfigureAwait(false);
        using var retained = old.Retain();
        firstGate.Dispose();

        using var nextGate = await CookOutputLease.AcquireWriteAsync(FileSystemPath.From(project.Root), this.TestContext.CancellationToken).ConfigureAwait(false);
        var nextRoot = await WritePublicationFixtureAsync(context, files, old.Head.Version, this.TestContext.CancellationToken).ConfigureAwait(false);
        using var next = await CookPublicationReadLease.OpenUnderGateAsync(context, files, nextGate, this.TestContext.CancellationToken).ConfigureAwait(false);
        nextGate.Dispose();
        _ = old.FindProjectRoot("Content").Should().Be(FileSystemPath.From(oldRoot));
        _ = next.FindProjectRoot("Content").Should().Be(FileSystemPath.From(nextRoot));
        _ = (next.PublicationId == old.PublicationId).Should().BeFalse();
        _ = ReclaimGeneration(oldRoot).Should().BeFalse();
        old.Dispose();
        _ = ReclaimGeneration(oldRoot).Should().BeFalse();
        Action retainDisposed = () => old.Retain().Dispose();
        _ = retainDisposed.Should().Throw<ObjectDisposedException>();
        retained.Dispose();
        _ = ReclaimGeneration(oldRoot).Should().BeTrue();
        _ = ReclaimGeneration(nextRoot).Should().BeFalse();
    }

    /// <summary>A bad document digest is rejected without leaking a file handle.</summary>
    /// <returns>The asynchronous metadata-integrity regression.</returns>
    [TestMethod]
    public async Task InvalidPublicationDigestReleasesItsOpening()
    {
        using var project = new ProjectDirectory();
        var context = PublicationContext(project.Root);
        var files = new NativeAtomicFileStore(new RealFileSystem());
        using var gate = await CookOutputLease.AcquireWriteAsync(FileSystemPath.From(project.Root), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = await WritePublicationFixtureAsync(context, files, FileVersion.Missing, this.TestContext.CancellationToken).ConfigureAwait(false);
        var headBytes = await files.ReadAsync(CookPublicationPaths.Head(project.Root), this.TestContext.CancellationToken).ConfigureAwait(false);
        var head = JsonSerializer.Deserialize<CookPublicationHead>(headBytes.Content.AsSpan(), CookPublicationDocument.JsonOptions)!;
        var documentPath = CookPublicationPaths.Document(project.Root, head.PublicationId);
        await File.WriteAllTextAsync(documentPath, "changed", this.TestContext.CancellationToken).ConfigureAwait(false);
        Func<Task> open = async () =>
        {
            using var reader = await CookPublicationReadLease.OpenUnderGateAsync(context, files, gate, this.TestContext.CancellationToken).ConfigureAwait(false);
        };
        _ = await open.Should().ThrowAsync<InvalidDataException>().ConfigureAwait(false);
        File.Delete(documentPath);
        _ = File.Exists(documentPath).Should().BeFalse();
    }

    /// <summary>A busy later generation releases earlier generation and document ownership.</summary>
    /// <returns>The asynchronous partial-acquisition regression.</returns>
    [TestMethod]
    public async Task BusyGenerationReleasesEarlierRootLeases()
    {
        using var project = new ProjectDirectory();
        var context = PublicationContext(project.Root);
        var files = new NativeAtomicFileStore(new RealFileSystem());
        using var gate = await CookOutputLease.AcquireWriteAsync(FileSystemPath.From(project.Root), this.TestContext.CancellationToken).ConfigureAwait(false);
        var root = await WritePublicationFixtureAsync(context, files, FileVersion.Missing, this.TestContext.CancellationToken).ConfigureAwait(false);
        var headBytes = await files.ReadAsync(CookPublicationPaths.Head(project.Root), this.TestContext.CancellationToken).ConfigureAwait(false);
        var head = JsonSerializer.Deserialize<CookPublicationHead>(headBytes.Content.AsSpan(), CookPublicationDocument.JsonOptions)!;
        var documentPath = CookPublicationPaths.Document(project.Root, head.PublicationId);
        var bytes = await files.ReadAsync(documentPath, this.TestContext.CancellationToken).ConfigureAwait(false);
        var document = JsonSerializer.Deserialize<CookPublicationDocument>(bytes.Content.AsSpan(), CookPublicationDocument.JsonOptions)!;
        var busyKey = Guid.CreateVersion7();
        var busyRoot = CookPublicationPaths.Generation(project.Root, busyKey);
        _ = Directory.CreateDirectory(busyRoot);
        var busyMarker = Path.Combine(busyRoot, CookedGeneration.MarkerFileName);
        await File.WriteAllBytesAsync(busyMarker, [], this.TestContext.CancellationToken).ConfigureAwait(false);
        using var claimant = WindowsCookFile.TryClaimGeneration(busyMarker);
        _ = claimant.Should().NotBeNull();
        document = document with
        {
            Roots = [.. document.Roots, new(CookPublicationRootOwner.Project, "Busy", busyKey, document.Roots[0].IndexSha256, LibraryPath: null)],
        };
        var changed = await files.WriteAsync(documentPath, JsonSerializer.SerializeToUtf8Bytes(document, CookPublicationDocument.JsonOptions), bytes.Version, this.TestContext.CancellationToken).ConfigureAwait(false);
        head = head with { DocumentSha256 = changed.Sha256 };
        _ = await files.WriteAsync(CookPublicationPaths.Head(project.Root), JsonSerializer.SerializeToUtf8Bytes(head, CookPublicationDocument.JsonOptions), headBytes.Version, this.TestContext.CancellationToken).ConfigureAwait(false);
        Func<Task> open = async () =>
        {
            using var reader = await CookPublicationReadLease.OpenUnderGateAsync(context, files, gate, this.TestContext.CancellationToken).ConfigureAwait(false);
        };
        _ = await open.Should().ThrowAsync<CookOutputBusyException>().ConfigureAwait(false);
        _ = ReclaimGeneration(root).Should().BeTrue();
        File.Delete(documentPath);
        _ = File.Exists(documentPath).Should().BeFalse();
    }

    /// <summary>Deleted output remains identifiable for status and repair, but cannot be admitted as usable content.</summary>
    /// <param name="deleteRoot">Whether the whole root or only its marker disappears.</param>
    /// <returns>The asynchronous missing-generation regression.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task MissingGenerationPreservesMetadataForRepair(bool deleteRoot)
    {
        using var project = new ProjectDirectory();
        var context = PublicationContext(project.Root);
        var files = new NativeAtomicFileStore(new RealFileSystem());
        using var gate = await CookOutputLease.AcquireWriteAsync(FileSystemPath.From(project.Root), this.TestContext.CancellationToken).ConfigureAwait(false);
        var root = await WritePublicationFixtureAsync(context, files, FileVersion.Missing, this.TestContext.CancellationToken).ConfigureAwait(false);
        if (deleteRoot)
        {
            Directory.Delete(root, recursive: true);
        }
        else
        {
            File.Delete(Path.Combine(root, CookedGeneration.MarkerFileName));
            _ = NativeInventoryFixture.Read(root).IsValid.Should().BeTrue();
        }

        using var publication = await CookPublicationReadLease.OpenUnderGateAsync(context, files, gate, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = publication.Roots.Should().ContainSingle();
        _ = publication.RootPaths.Should().Equal(root);
        _ = publication.UnavailableRoots.Should().ContainSingle();
        _ = publication.FindProjectRoot("Content").Should().Be(FileSystemPath.From(root));
        Action admit = publication.RequireAvailableGenerations;
        _ = admit.Should().Throw<InvalidDataException>();
    }

    /// <summary>
    /// A root cooked in another index format is unavailable from the moment the publication opens: catalogs and mounting
    /// skip it while the publication is still admitted, and every reader shares one read of its index until the index
    /// changes.
    /// </summary>
    /// <returns>The asynchronous stale-format regression.</returns>
    [TestMethod]
    public async Task StaleIndexFormatMakesTheRootUnavailable()
    {
        // Arrange
        using var project = new ProjectDirectory();
        var context = PublicationContext(project.Root);
        var files = new NativeAtomicFileStore(new RealFileSystem());
        using var gate = await CookOutputLease.AcquireWriteAsync(FileSystemPath.From(project.Root), this.TestContext.CancellationToken).ConfigureAwait(false);
        var root = await WritePublicationFixtureAsync(context, files, FileVersion.Missing, this.TestContext.CancellationToken).ConfigureAwait(false);
        var indexPath = Path.Combine(root, "container.index.bin");
        await LooseCookedIndexFixture.RewriteFormatVersionAsync(indexPath, 4).ConfigureAwait(false);

        // Act
        using var publication = await CookPublicationReadLease.OpenUnderGateAsync(context, files, gate, this.TestContext.CancellationToken).ConfigureAwait(false);
        var catalogs = await publication.CreateCatalogsAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        using var mounts = await new CookedContentMountService().PrepareAsync(context, publication, this.TestContext.CancellationToken).ConfigureAwait(false);
        var first = await CookedIndexSnapshot.ReadAsync(root, this.TestContext.CancellationToken).ConfigureAwait(false);
        var again = await CookedIndexSnapshot.ReadAsync(root, this.TestContext.CancellationToken).ConfigureAwait(false);
        await LooseCookedIndexFixture.RewriteFormatVersionAsync(indexPath, 3).ConfigureAwait(false);
        var changed = await CookedIndexSnapshot.ReadAsync(root, this.TestContext.CancellationToken).ConfigureAwait(false);

        // Assert
        _ = publication.UnavailableRoots.Should().ContainSingle();
        _ = catalogs.Should().BeEmpty();
        _ = publication.MountableRoots.Should().BeEmpty();
        _ = mounts.Roots.Should().BeEmpty();
        _ = first.Should().Be(new CookedIndexRead.Stale(4));
        _ = again.Should().BeSameAs(first);
        _ = changed.Should().Be(new CookedIndexRead.Stale(3));
    }

    /// <summary>A new project has an empty selection instead of invented cooked roots or producer evidence.</summary>
    /// <returns>The asynchronous empty-publication regression.</returns>
    [TestMethod]
    public async Task MissingHeadRepresentsAnUncookedProject()
    {
        using var project = new ProjectDirectory();
        var context = PublicationContext(project.Root);
        var files = new NativeAtomicFileStore(new RealFileSystem());
        using var gate = await CookOutputLease.AcquireWriteAsync(FileSystemPath.From(project.Root), this.TestContext.CancellationToken).ConfigureAwait(false);
        using var publication = await CookPublicationReadLease.OpenUnderGateAsync(context, files, gate, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = publication.PublicationId.Should().BeNull();
        _ = publication.Roots.Should().BeEmpty();
        _ = publication.ProductState.Products.Should().BeEmpty();
        _ = publication.Head.Version.Should().Be(FileVersion.Missing);
    }
}
