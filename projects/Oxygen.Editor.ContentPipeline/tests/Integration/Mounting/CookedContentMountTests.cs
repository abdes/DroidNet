// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using System.Text.Json;
using AwesomeAssertions;
using DroidNet.Storage;
using DroidNet.Storage.Native;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.ContentPipeline.Mounting;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.TestSupport;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Managed.Assets.Persistence;
using Oxygen.Managed.Assets.Persistence.LooseCooked;
using Oxygen.Managed.Assets.Persistence.Pak;
using Oxygen.Managed.Core;
using Oxygen.Testing;
using Testably.Abstractions;

namespace Oxygen.Editor.ContentPipeline.Integration.Tests.Mounting;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class CookedContentMountTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>The default order is stable and every mounted source retains protected bytes.</summary>
    /// <returns>The asynchronous mount lifetime regression.</returns>
    [TestMethod]
    public async Task DefaultOrderAndReadersSurviveUntilTheMountSetIsReleased()
    {
        using var fixture = new MountFixture();
        using var reader = await fixture.CaptureAsync(fixture.Project, this.TestContext.CancellationToken).ConfigureAwait(false);
        var mounts = await fixture.Service.PrepareAsync(fixture.Project, reader, this.TestContext.CancellationToken).ConfigureAwait(false);
        using (mounts)
        {
            _ = mounts.Roots.Should().Equal(fixture.First, fixture.Second, fixture.ProjectOutput);
            reader.Dispose();
            _ = mounts.Publication.PublicationId.Should().NotBeNull();
            foreach (var root in mounts.Roots)
            {
                Action write = () => File.WriteAllBytes(Path.Combine(root, "Materials", "Shared.omat"), [9]);
                _ = write.Should().Throw<IOException>();
            }
        }

        File.WriteAllBytes(Path.Combine(fixture.First, "Materials", "Shared.omat"), [9]);
    }

    /// <summary>A library cooked in another index format is left out and named, and the remaining content still mounts.</summary>
    /// <returns>The asynchronous stale-library regression.</returns>
    [TestMethod]
    public async Task StaleLibraryIsLeftOutWhileTheRestMounts()
    {
        using var fixture = new MountFixture();
        await LooseCookedIndexFixture.RewriteFormatVersionAsync(Path.Combine(fixture.First, "container.index.bin"), 4).ConfigureAwait(false);

        using var libraries = await CookedLibraryReadSet.AcquireAsync(fixture.Project, new EngineContentPakFixture(), this.TestContext.CancellationToken).ConfigureAwait(false);
        using var reader = await fixture.CaptureAsync(fixture.Project, this.TestContext.CancellationToken).ConfigureAwait(false);
        using var mounts = await fixture.Service.PrepareAsync(fixture.Project, reader, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = libraries.Unreadable.Should().ContainSingle().Which.Name.Should().Be("First");
        _ = mounts.Roots.Should().Equal(fixture.Second, fixture.ProjectOutput);
    }

    /// <summary>Explicit overrides and repeated aliases use the final occurrence of a physical root.</summary>
    /// <returns>The asynchronous source-order regression.</returns>
    [TestMethod]
    public async Task ExplicitOrderKeepsAnOverrideAboveProjectOutputAndDeduplicatesAliases()
    {
        using var fixture = new MountFixture();
        var project = fixture.Project with
        {
            LocalFolderMounts = [.. fixture.Project.LocalFolderMounts, new("Alias", FileSystemPath.From(fixture.First))],
            CookedContentOrder = [new(CookedContentSourceKind.LocalFolder, "First"), new(CookedContentSourceKind.LocalFolder, "Second"), new(CookedContentSourceKind.ProjectOutput), new(CookedContentSourceKind.LocalFolder, "Alias")],
        };
        using var reader = await fixture.CaptureAsync(project, this.TestContext.CancellationToken).ConfigureAwait(false);
        using var mounts = await fixture.Service.PrepareAsync(project, reader, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = mounts.Roots.Should().Equal(fixture.Second, fixture.ProjectOutput, fixture.First);
    }

    /// <summary>A damaged library cannot be accepted merely because project output masks its top-level assets.</summary>
    /// <returns>The asynchronous validation regression.</returns>
    [TestMethod]
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Reliability", "CA2025:Do not pass IDisposable instances into unawaited tasks", Justification = "the test awaits the preparation before the reader is disposed")]
    public async Task DamagedMaskedLibraryFailsBeforeMountAndReleasesReaders()
    {
        using var fixture = new MountFixture();
        using var reader = await fixture.CaptureAsync(fixture.Project, this.TestContext.CancellationToken).ConfigureAwait(false);
        var path = Path.Combine(fixture.First, "Materials", "Shared.omat");
        File.WriteAllBytes(path, [8, 8, 8, 8]);
        Func<Task> prepare = () => fixture.Service.PrepareAsync(fixture.Project, reader, this.TestContext.CancellationToken);
        _ = await prepare.Should().ThrowAsync<InvalidDataException>().WithMessage("*metadata does not match*").ConfigureAwait(false);
        using var borrowed = reader.Retain();
        File.WriteAllBytes(path, [9]);
    }

    /// <summary>A library re-cooked after capture is rejected with the root and both index identities.</summary>
    /// <returns>The asynchronous stale-library diagnosis regression.</returns>
    [TestMethod]
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Reliability", "CA2025:Do not pass IDisposable instances into unawaited tasks", Justification = "the test awaits the preparation before the reader is disposed")]
    public async Task RecookedLibraryFailsAdmissionNamingTheChangedRoot()
    {
        using var fixture = new MountFixture();
        using var reader = await fixture.CaptureAsync(fixture.Project, this.TestContext.CancellationToken).ConfigureAwait(false);
        var recorded = reader.Roots.Single(static root => string.Equals(root.Name, "First", StringComparison.Ordinal));
        fixture.RecookFirst();
        Func<Task> prepare = () => fixture.Service.PrepareAsync(fixture.Project, reader, this.TestContext.CancellationToken);
        var changed = (await prepare.Should().ThrowAsync<CookedLibraryChangedException>().ConfigureAwait(false)).Which;
        _ = changed.Root.Should().Be(recorded);
        _ = changed.RootPath.Should().Be(fixture.First);
        _ = changed.ActualIndexSha256.Should().NotBe(recorded.IndexSha256);
        _ = changed.Message.Should().Contain("'First'").And.Contain(recorded.IndexSha256).And.Contain(changed.ActualIndexSha256!);
    }

    /// <summary>Opening the catalog of a library re-cooked after capture names the root and both index identities.</summary>
    /// <returns>The asynchronous stale-catalog diagnosis regression.</returns>
    [TestMethod]
    public async Task RecookedLibraryFailsCatalogOpeningNamingBothIdentities()
    {
        using var fixture = new MountFixture();
        using var reader = await fixture.CaptureAsync(fixture.Project, this.TestContext.CancellationToken).ConfigureAwait(false);
        var recorded = reader.Roots.Single(static root => string.Equals(root.Name, "First", StringComparison.Ordinal));
        fixture.RecookFirst();
        var current = (await CookedIndexSnapshot.ReadAsync(fixture.First, this.TestContext.CancellationToken).ConfigureAwait(false)).RequireCurrent(fixture.First);
        Func<Task> open = () => reader.CreateCatalogAsync(recorded, this.TestContext.CancellationToken);
        var changed = (await open.Should().ThrowAsync<CookedLibraryChangedException>().ConfigureAwait(false)).Which;
        _ = changed.Message.Should().Contain("'First'").And.Contain(fixture.First)
            .And.Contain(recorded.SourceKey.ToString("D")).And.Contain(recorded.IndexSha256)
            .And.Contain(current.Index.SourceGuid.ToString("D")).And.Contain(current.Fingerprint);
    }

    /// <summary>Cancellation waits for preparation to end and releases every acquired file reader.</summary>
    /// <returns>The asynchronous cancellation regression.</returns>
    [TestMethod]
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Reliability", "CA2025:Do not pass IDisposable instances into unawaited tasks", Justification = "the test awaits the preparation before the reader is disposed")]
    public async Task CancelledPreparationReleasesTheBorrowedReader()
    {
        using var fixture = new MountFixture();
        using var reader = await fixture.CaptureAsync(fixture.Project, this.TestContext.CancellationToken).ConfigureAwait(false);
        using var cancellation = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        await cancellation.CancelAsync().ConfigureAwait(false);
        var preparation = fixture.Service.PrepareAsync(fixture.Project, reader, cancellation.Token);
        Func<Task> finish = () => preparation;
        _ = await finish.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
        using var borrowed = reader.Retain();
        File.WriteAllBytes(Path.Combine(fixture.First, "Materials", "Shared.omat"), [9]);
    }

    /// <summary>A declared pak is a library: its catalog identity is published, it mounts as a pak by that identity, and a replaced pak is refused.</summary>
    /// <returns>The asynchronous pak library regression.</returns>
    [TestMethod]
    public async Task DeclaredPakMountsByItsCatalogIdentity()
    {
        using var fixture = new MountFixture();
        var pak = fixture.WritePak("Props.pak", Guid.CreateVersion7(), digestByte: 0xA1);
        var project = fixture.Project with { PakFileMounts = [new("Props", FileSystemPath.From(pak.Path))] };
        using var reader = await fixture.CaptureAsync(project, this.TestContext.CancellationToken).ConfigureAwait(false);

        var root = reader.Roots.Single(static root => root.Name == "Props");
        _ = root.Container.Should().Be(CookPublicationRootContainer.PakFile);
        _ = root.SourceKey.Should().Be(pak.Document.SourceIdentity);
        _ = root.IndexSha256.Should().Be(pak.Document.CatalogDigest);
        using (var mounts = await fixture.Service.PrepareAsync(project, reader, this.TestContext.CancellationToken).ConfigureAwait(false))
        {
            _ = mounts.Roots.Should().Contain(pak.Path);
            Action replace = () => File.WriteAllBytes(pak.Path, [0]);
            _ = replace.Should().Throw<IOException>("a mounted pak is held open for shared reading");
        }

        _ = fixture.WritePak("Props.pak", pak.Document.SourceIdentity, digestByte: 0xB2);
        Func<Task> stale = () => fixture.Service.PrepareAsync(project, reader, this.TestContext.CancellationToken);
        _ = await stale.Should().ThrowAsync<CookedLibraryChangedException>().ConfigureAwait(false);
    }

    private sealed partial class MountFixture : IDisposable
    {
        private readonly DirectoryInfo directory = Directory.CreateTempSubdirectory("Oxygen-CookedMounts-");
        private readonly Guid sourceKey = Guid.CreateVersion7();
        private readonly NativeAtomicFileStore files = new(new RealFileSystem());

        public MountFixture()
        {
            this.ProjectOutput = this.WriteRoot(Path.GetRelativePath(this.directory.FullName, CookPublicationPaths.Generation(this.directory.FullName, this.sourceKey)), 1, this.sourceKey);
            File.WriteAllBytes(Path.Combine(this.ProjectOutput, CookedGeneration.MarkerFileName), []);
            this.First = this.WriteRoot("First", 2);
            this.Second = this.WriteRoot("Second", 3);
            this.Project = TestProject.Create(new RealFileSystem(), this.directory.FullName).Context with
            {
                Name = "Mounts",
                LocalFolderMounts = [new("First", FileSystemPath.From(this.First)), new("Second", FileSystemPath.From(this.Second))],
            };
            this.Service = new();
        }

        public string ProjectOutput { get; }

        public string First { get; }

        public string Second { get; }

        public ProjectContext Project { get; }

        public CookedContentMountService Service { get; }

        public async Task<CookPublicationReadLease> CaptureAsync(ProjectContext project, CancellationToken token)
        {
            using var libraries = await CookedLibraryReadSet.AcquireAsync(project, new EngineContentPakFixture(), token).ConfigureAwait(false);
            var index = (await CookedIndexSnapshot.ReadAsync(this.ProjectOutput, token).ConfigureAwait(false)).RequireCurrent(this.ProjectOutput);
            var roots = libraries.OrderBindings([new(CookPublicationRootOwner.Project, "Content", this.sourceKey, index.Fingerprint, LibraryPath: null)]);
            var document = new CookPublicationDocument(
                CookPublicationDocument.CurrentVersion,
                project.ProjectId,
                Guid.NewGuid(),
                DateTimeOffset.UtcNow,
                CookPublicationDocument.ConfigurationIdentity(project),
                roots,
                [],
                CookInputs: null);
            var path = CookPublicationPaths.Document(project.ProjectRoot.FullPath, document.OperationId);
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            using var gate = await CookOutputLease.AcquireWriteAsync(project.ProjectRoot, token).ConfigureAwait(false);
            var version = await this.files.WriteAsync(path, JsonSerializer.SerializeToUtf8Bytes(document, CookPublicationDocument.JsonOptions), FileVersion.Missing, token).ConfigureAwait(false);
            var head = new CookPublicationHead(CookPublicationHead.CurrentVersion, document.OperationId, version.Sha256);
            _ = await this.files.WriteAsync(CookPublicationPaths.Head(project.ProjectRoot.FullPath), JsonSerializer.SerializeToUtf8Bytes(head, CookPublicationDocument.JsonOptions), FileVersion.Missing, token).ConfigureAwait(false);
            return await CookPublicationReadLease.OpenUnderGateAsync(project, this.files, gate, token).ConfigureAwait(false);
        }

        public void RecookFirst() => _ = this.WriteRoot("First", 4);

        public (string Path, PakDocument Document) WritePak(string name, Guid identity, byte digestByte)
        {
            var path = Path.Combine(this.directory.FullName, name);
            var document = new PakDocument(1, identity, Convert.ToHexString(Enumerable.Repeat(digestByte, 32).ToArray()), [new(new AssetKey(1, 2), 1, "/Props/Materials/Rock.omat", 0, 16)]);
            using var stream = File.Create(path);
            PakFixture.Write(stream, document);
            return (path, document);
        }

        public void Dispose() => this.directory.Delete(recursive: true);

        private string WriteRoot(string relative, byte value, Guid? sourceKey = null)
        {
            var root = Path.GetFullPath(Path.Combine(this.directory.FullName, relative));
            _ = Directory.CreateDirectory(Path.Combine(root, "Materials"));
            byte[] bytes = [value, value, value];
            File.WriteAllBytes(Path.Combine(root, "Materials", "Shared.omat"), bytes);
            using var index = File.Create(Path.Combine(root, "container.index.bin"));
            Oxygen.Testing.LooseCookedIndexFixture.Write(index, new Document(1, IndexFeatures.HasVirtualPaths, sourceKey ?? Guid.CreateVersion7(), [new(new AssetKey(1, 2), "Materials/Shared.omat", "/Content/Materials/Shared.omat", 1, (ulong)bytes.Length, SHA256.HashData(bytes))], []));
            return root;
        }
    }
}
