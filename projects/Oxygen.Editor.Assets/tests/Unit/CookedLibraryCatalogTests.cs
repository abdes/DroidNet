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

    /// <summary>A library's own copy of an overridden asset explains the source used by assignments.</summary>
    /// <returns>The asynchronous physical-library composition regression.</returns>
    [TestMethod]
    public async Task OverriddenLibraryCopyExplainsTheSourceUsedByAssignments()
    {
        this.fixture.Publish(this.fixture.FirstRoot, this.fixture.SecondRoot, this.fixture.ProjectRoot);
        var catalog = this.fixture.CreateCatalog();
        var records = await catalog.QueryAsync(new(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false);
        var rows = this.fixture.Reduce(records);
        _ = rows.Where(static row => row.Kind == AssetKind.Material).Should().ContainSingle();
        var effective = rows.Single(static row => row.Kind == AssetKind.Material);
        _ = effective.CookedMetadata!.ContainerPath.Should().Be(this.fixture.ProjectOutput);
        _ = effective.OverriddenCookedSources.Should().HaveCount(2);
        var library = this.fixture.LibraryCopy(effective, this.fixture.First);
        _ = library.DisplayPath.Should().Be("/First/payloads/shared.bin");
        _ = library.CookedMetadata!.ContainerPath.Should().Be(this.fixture.First);
        _ = library.PrimaryBadge.Should().Be("Overridden");
        _ = library.PrimaryBadgeTooltip.Should().Contain("project output");
        _ = library.DescriptorPath.Should().BeNull();
        _ = library.SourcePath.Should().BeNull();
        _ = library.CanCook.Should().BeFalse();
        _ = library.Identity.Should().Be(AssetPath.Parse("/Content/Shared.omat"));
    }

    /// <summary>A new publication order changes the effective source, and a restarted catalog keeps it.</summary>
    /// <returns>The asynchronous ordering and reinitialization regression.</returns>
    [TestMethod]
    public async Task ChangedPriorityAndRestartSelectTheSameSource()
    {
        this.fixture.Publish(this.fixture.FirstRoot, this.fixture.SecondRoot, this.fixture.ProjectRoot);
        var catalog = this.fixture.CreateCatalog();
        _ = await catalog.QueryAsync(new(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false);
        this.fixture.Publish(this.fixture.FirstRoot, this.fixture.ProjectRoot, this.fixture.SecondRoot);
        await catalog.UsePublicationAsync(this.fixture.Publication.Lease(), this.TestContext.CancellationToken).ConfigureAwait(false);
        var current = await catalog.QueryAsync(new(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = SharedMaterial(current).Cooked!.ContainerPath.Should().Be(this.fixture.Second);
        var restarted = this.fixture.CreateCatalog();
        var afterRestart = await restarted.QueryAsync(new(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = SharedMaterial(afterRestart).Cooked!.ContainerPath.Should().Be(this.fixture.Second);
        var effective = this.fixture.Reduce(afterRestart).Single(static row => row.Kind == AssetKind.Material);
        _ = this.fixture.LibraryCopy(effective, this.fixture.Second).IsCookedSourceOverridden.Should().BeFalse();
    }

    private static AssetRecord SharedMaterial(IReadOnlyList<AssetRecord> records)
        => records.Single(static record => record.Identity == AssetPath.Parse("/Content/Shared.omat"));

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
            (this.ProjectOutput, var projectIndex) = this.WriteRoot(".cooked/generations/0123456789abcdef0123456789abcdef", 1);
            (this.First, var firstIndex) = this.WriteRoot("Libraries/First", 2);
            (this.Second, var secondIndex) = this.WriteRoot("Libraries/Second", 3);
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

        /// <summary>Describes the copy of an asset that one library provides.</summary>
        /// <param name="effective">The asset as ordinary resolution selected it.</param>
        /// <param name="library">The library folder.</param>
        /// <returns>The library's own row.</returns>
        public AssetEntry LibraryCopy(AssetEntry effective, FileSystemPath library)
        {
            var source = effective.OverriddenCookedSources.Prepend(effective.CookedMetadata!).Single(metadata => metadata.ContainerPath == library);
            return AssetIdentity.FromCookedSource(effective, source, this.Session.Context);
        }

        public async ValueTask DisposeAsync()
        {
            foreach (var catalog in this.catalogs)
            {
                await catalog.DisposeAsync().ConfigureAwait(false);
            }

            this.Session.Dispose();
            this.directory.Delete(recursive: true);
        }

        private (FileSystemPath Folder, Document Index) WriteRoot(string relative, byte value)
        {
            var root = FileSystemPath.From(Path.Combine(this.directory.FullName, relative));
            _ = Directory.CreateDirectory(root.Combine("payloads").FullPath);
            byte[] bytes = [value];
            File.WriteAllBytes(root.Combine("payloads/shared.bin").FullPath, bytes);
            var index = new Document(
                1,
                IndexFeatures.HasVirtualPaths,
                Guid.CreateVersion7(),
                [new(new AssetKey(1, 2), "payloads/shared.bin", "/Content/Shared.omat", 1, 1, SHA256.HashData(bytes))],
                []);
            using var stream = File.Create(root.Combine("container.index.bin").FullPath);
            LooseCookedIndexFixture.Write(stream, index);
            return (root, index);
        }
    }
}
