// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using System.Text.Json;
using DroidNet.Storage;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.ContentPipeline.Publication;
using AwesomeAssertions;
using DroidNet.Storage.Native;
using Oxygen.Editor.ContentPipeline.Mounting;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Managed.Assets.Persistence.LooseCooked.V2;
using Testably.Abstractions;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Verifies ordered mounting and physical file ownership through validation and native use.</summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed partial class CookedContentMountServiceTests
{
    /// <summary>Gets or sets the active test context.</summary>
    public TestContext TestContext { get; set; } = null!;

    /// <summary>The default order is stable and every mounted source retains protected bytes.</summary>
    /// <returns>The asynchronous mount lifetime regression.</returns>
    [TestMethod]
    public async Task DefaultOrderAndReadersSurviveUntilTheMountSetIsReleased()
    {
        using var fixture = new Fixture();
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

    /// <summary>Explicit overrides and repeated aliases use the final occurrence of a physical root.</summary>
    /// <returns>The asynchronous source-order regression.</returns>
    [TestMethod]
    public async Task ExplicitOrderKeepsAnOverrideAboveProjectOutputAndDeduplicatesAliases()
    {
        using var fixture = new Fixture();
        var project = fixture.Project with
        {
            LocalFolderMounts = [.. fixture.Project.LocalFolderMounts, new("Alias", fixture.First)],
            CookedContentOrder = [new(CookedContentSourceKind.LocalFolder, "First"), new(CookedContentSourceKind.LocalFolder, "Second"), new(CookedContentSourceKind.ProjectOutput), new(CookedContentSourceKind.LocalFolder, "Alias")],
        };
        using var reader = await fixture.CaptureAsync(project, this.TestContext.CancellationToken).ConfigureAwait(false);
        using var mounts = await fixture.Service.PrepareAsync(project, reader, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = mounts.Roots.Should().Equal(fixture.Second, fixture.ProjectOutput, fixture.First);
    }

    /// <summary>A damaged library cannot be accepted merely because project output masks its top-level assets.</summary>
    /// <returns>The asynchronous validation regression.</returns>
    [TestMethod]
    public async Task DamagedMaskedLibraryFailsBeforeMountAndReleasesReaders()
    {
        using var fixture = new Fixture();
        using var reader = await fixture.CaptureAsync(fixture.Project, this.TestContext.CancellationToken).ConfigureAwait(false);
        var path = Path.Combine(fixture.First, "Materials", "Shared.omat");
        File.WriteAllBytes(path, [8, 8, 8, 8]);
        Func<Task> prepare = () => fixture.Service.PrepareAsync(fixture.Project, reader, this.TestContext.CancellationToken);
        _ = await prepare.Should().ThrowAsync<InvalidDataException>().WithMessage("*metadata does not match*").ConfigureAwait(false);
        using var borrowed = reader.Retain();
        File.WriteAllBytes(path, [9]);
    }

    /// <summary>Cancellation waits for preparation to end and releases every acquired file reader.</summary>
    /// <returns>The asynchronous cancellation regression.</returns>
    [TestMethod]
    public async Task CancelledPreparationReleasesTheBorrowedReader()
    {
        using var fixture = new Fixture();
        using var reader = await fixture.CaptureAsync(fixture.Project, this.TestContext.CancellationToken).ConfigureAwait(false);
        using var cancellation = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        await cancellation.CancelAsync().ConfigureAwait(false);
        var preparation = fixture.Service.PrepareAsync(fixture.Project, reader, cancellation.Token);
        Func<Task> finish = () => preparation;
        _ = await finish.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
        using var borrowed = reader.Retain();
        File.WriteAllBytes(Path.Combine(fixture.First, "Materials", "Shared.omat"), [9]);
    }

    private sealed partial class Fixture : IDisposable
    {
        private readonly DirectoryInfo directory = Directory.CreateTempSubdirectory("Oxygen-CookedMounts-");
        private readonly Guid sourceKey = Guid.CreateVersion7();
        private readonly NativeAtomicFileStore files = new(new RealFileSystem());

        public Fixture()
        {
            this.ProjectOutput = this.WriteRoot(Path.GetRelativePath(this.directory.FullName, CookPublicationPaths.Generation(this.directory.FullName, this.sourceKey)), 1, this.sourceKey);
            File.WriteAllBytes(Path.Combine(this.ProjectOutput, CookedGeneration.MarkerFileName), []);
            this.First = this.WriteRoot("First", 2);
            this.Second = this.WriteRoot("Second", 3);
            this.Project = new ProjectContext
            {
                ProjectId = Guid.NewGuid(), Name = "Mounts", Category = Category.Games, ProjectRoot = this.directory.FullName,
                AuthoringMounts = [new("Content", "Content")], LocalFolderMounts = [new("First", this.First), new("Second", this.Second)], Scenes = [],
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
            using var libraries = await CookedLibraryReadSet.AcquireAsync(project, token).ConfigureAwait(false);
            var index = await CookedIndexSnapshot.ReadAsync(this.ProjectOutput, token).ConfigureAwait(false);
            var roots = libraries.OrderBindings([new(CookPublicationRootOwner.Project, "Content", this.sourceKey, index.Fingerprint, null)]);
            var document = new CookPublicationDocument(CookPublicationDocument.CurrentVersion, project.ProjectId, Guid.NewGuid(),
                DateTimeOffset.UtcNow, CookPublicationDocument.ConfigurationIdentity(project), roots, [], null);
            var path = CookPublicationPaths.Document(project.ProjectRoot, document.OperationId);
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            using var gate = await CookOutputLease.AcquireWriteAsync(project.ProjectRoot, token).ConfigureAwait(false);
            var version = await this.files.WriteAsync(path, JsonSerializer.SerializeToUtf8Bytes(document, CookPublicationDocument.JsonOptions), FileVersion.Missing, token).ConfigureAwait(false);
            var head = new CookPublicationHead(CookPublicationHead.CurrentVersion, document.OperationId, version.Sha256);
            _ = await this.files.WriteAsync(CookPublicationPaths.Head(project.ProjectRoot), JsonSerializer.SerializeToUtf8Bytes(head, CookPublicationDocument.JsonOptions), FileVersion.Missing, token).ConfigureAwait(false);
            return await CookPublicationReadLease.OpenUnderGateAsync(project, this.files, gate, token).ConfigureAwait(false);
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
