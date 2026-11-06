// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using AwesomeAssertions;
using DroidNet.Storage.Native;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.Testing;
using Oxygen.Editor.Projects.Mounts;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Assets.Persistence;
using Oxygen.Managed.Assets.Persistence.LooseCooked;
using Oxygen.Managed.Assets.Testing;
using Oxygen.Managed.Core;
using Testably.Abstractions;

namespace Oxygen.Editor.Assets.Unit.Tests;

/// <summary>Checks how the composed catalog presents the project output and cooked libraries a publication selects.</summary>
[TestClass]
public sealed partial class CookedLibraryCatalogTests
{
    private Fixture fixture = null!;

    [TestInitialize]
    public void CreateFixture() => this.fixture = new Fixture();

    [TestCleanup]
    public async Task DisposeFixtureAsync() => await this.fixture.DisposeAsync().ConfigureAwait(false);

    /// <summary>Gets or sets the running test context.</summary>
    public TestContext TestContext { get; set; } = null!;

    /// <summary>A library's asset is a row at its identity, read from its own root, also after a restart.</summary>
    /// <returns>The asynchronous restart regression.</returns>
    [TestMethod]
    public async Task ALibrarysAssetIsARowAtItsIdentity()
    {
        this.fixture.Publish(this.fixture.FirstRoot, this.fixture.SecondRoot, this.fixture.ProjectRoot);
        _ = await this.fixture.CreateCatalog().QueryAsync(new(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false);

        var records = await this.fixture.CreateCatalog().QueryAsync(new(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false);

        var row = this.fixture.Reduce(records).Single(static row => row.Identity == AssetPath.Parse("/Second/Shared.omat"));
        _ = row.DisplayPath.Should().Be("/Second/Shared.omat");
        _ = row.CookedMetadata!.ContainerPath.Should().Be(this.fixture.Second);
    }

    private sealed partial class Fixture : IAsyncDisposable
    {
        private readonly DirectoryInfo directory = Directory.CreateTempSubdirectory("Oxygen-LibraryCatalog-");
        private readonly NativeStorageProvider storage = new(new RealFileSystem());
        private readonly List<AssetCatalog> catalogs = [];

        public Fixture()
        {
            _ = Directory.CreateDirectory(Path.Combine(this.directory.FullName, "Content"));
            File.WriteAllText(
                Path.Combine(this.directory.FullName, "Content", "Shared.omat.json"),
                """{"name":"Shared","parameters":{"base_color":[1,0,0,1],"metalness":0,"roughness":0.5}}""");
            (this.ProjectOutput, var projectIndex) = this.WriteRoot(".cooked/generations/0123456789abcdef0123456789abcdef", "Content", 1);
            (this.First, var firstIndex) = this.WriteRoot("Libraries/First", "First", 2);
            (this.Second, var secondIndex) = this.WriteRoot("Libraries/Second", "Second", 3);
            var mounts = new ProjectMount[]
            {
                new("Content", MountKind.Source, "Content"),
                new("First", MountKind.Cooked, "Libraries/First"),
                new("Second", MountKind.Cooked, "Libraries/Second"),
            };
            this.Session = new TestSession(TestProject.Create(new RealFileSystem(), this.directory.FullName, mounts).Context with { Name = "Libraries" });
            var project = this.Session.Context;
            this.Publication = new(project.ProjectId, project.ProjectRoot);
            this.ProjectRoot = FakeCookPublicationReader.Root(CookPublicationRootOwner.Project, "Content", projectIndex, this.ProjectOutput);
            this.FirstRoot = FakeCookPublicationReader.Root(CookPublicationRootOwner.Library, "First", firstIndex, this.First);
            this.SecondRoot = FakeCookPublicationReader.Root(CookPublicationRootOwner.Library, "Second", secondIndex, this.Second);
        }

        public FileSystemPath ProjectOutput { get; }

        public FileSystemPath First { get; }

        public FileSystemPath Second { get; }

        public PublishedRoot ProjectRoot { get; }

        public PublishedRoot FirstRoot { get; }

        public PublishedRoot SecondRoot { get; }

        public TestSession Session { get; }

        public FakeCookPublicationReader Publication { get; }

        public void Publish(params PublishedRoot[] roots)
            => this.Publication.Publish(roots, new Dictionary<string, FileSystemPath>(StringComparer.Ordinal) { ["Content"] = this.ProjectOutput });

        public AssetCatalog CreateCatalog()
        {
            var catalog = new AssetCatalog(this.Session, this.storage, new EngineContentPakFixture(), this.Publication);
            this.catalogs.Add(catalog);
            return catalog;
        }

        public IReadOnlyList<AssetEntry> Reduce(IReadOnlyList<AssetRecord> records)
            => new AssetIdentityReducer().Reduce(records, this.Session.Context, new(this.Session.Context.ProjectId, FileSystemPath.From(this.directory.FullName)));

        public async ValueTask DisposeAsync()
        {
            foreach (var catalog in this.catalogs)
            {
                await catalog.DisposeAsync().ConfigureAwait(false);
            }

            this.Session.Dispose();
            this.directory.Delete(recursive: true);
        }

        private (FileSystemPath Folder, Document Index) WriteRoot(string relative, string mount, byte value)
        {
            var root = FileSystemPath.From(Path.Combine(this.directory.FullName, relative));
            _ = Directory.CreateDirectory(root.Combine("payloads").FullPath);
            byte[] bytes = [value];
            File.WriteAllBytes(root.Combine("payloads/shared.bin").FullPath, bytes);
            var index = new Document(
                1,
                IndexFeatures.HasVirtualPaths,
                Guid.CreateVersion7(),
                [new(AssetKey.FromVirtualPath($"/{mount}/Shared.omat"), "payloads/shared.bin", $"/{mount}/Shared.omat", 1, 1, SHA256.HashData(bytes))],
                []);
            using var stream = File.Create(root.Combine("container.index.bin").FullPath);
            LooseCookedIndexFixture.Write(stream, index);
            return (root, index);
        }
    }
}
