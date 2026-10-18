// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using AwesomeAssertions;
using DroidNet.Storage.Native;
using Moq;
using Oxygen.Editor.ContentPipeline.Discovery;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Assets.Persistence.LooseCooked.V3;
using Oxygen.Testing;
using Testably.Abstractions;

namespace Oxygen.Editor.Assets.Unit.Tests;

/// <summary>Checks how the composed catalog presents the project output and cooked libraries a publication selects.</summary>
[TestClass]
public sealed class CookedLibraryCatalogTests
{
    /// <summary>Gets or sets the running test context.</summary>
    public TestContext TestContext { get; set; } = null!;

    /// <summary>A library's own copy of an overridden asset explains the source used by assignments.</summary>
    /// <returns>The asynchronous physical-library composition regression.</returns>
    [TestMethod]
    public async Task OverriddenLibraryCopyExplainsTheSourceUsedByAssignments()
    {
        using var fixture = new Fixture();
        fixture.Publish(fixture.FirstRoot, fixture.SecondRoot, fixture.ProjectRoot);
        using var catalog = fixture.CreateCatalog();
        var records = await catalog.QueryAsync(new(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false);
        var rows = fixture.Reduce(records);
        _ = rows.Where(static row => row.Kind == AssetKind.Material).Should().ContainSingle();
        var effective = rows.Single(static row => row.Kind == AssetKind.Material);
        _ = effective.CookedMetadata!.RootFolderPath.Should().Be(fixture.ProjectOutput);
        _ = effective.OverriddenCookedSources.Should().HaveCount(2);
        var library = fixture.LibraryCopy(effective, fixture.First);
        _ = library.DisplayPath.Should().Be("/First/payloads/shared.bin");
        _ = library.CookedMetadata!.RootFolderPath.Should().Be(fixture.First);
        _ = library.PrimaryBadge.Should().Be("Overridden");
        _ = library.PrimaryBadgeTooltip.Should().Contain("project output");
        _ = library.DescriptorPath.Should().BeNull();
        _ = library.SourcePath.Should().BeNull();
        _ = library.CanCook.Should().BeFalse();
        _ = library.CookedUri.Should().Be(new Uri("asset:///Content/Shared.omat"));
    }

    /// <summary>A new publication order changes the effective source, and a restarted catalog keeps it.</summary>
    /// <returns>The asynchronous ordering and reinitialization regression.</returns>
    [TestMethod]
    public async Task ChangedPriorityAndRestartSelectTheSameSource()
    {
        using var fixture = new Fixture();
        fixture.Publish(fixture.FirstRoot, fixture.SecondRoot, fixture.ProjectRoot);
        using var catalog = fixture.CreateCatalog();
        _ = await catalog.QueryAsync(new(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false);
        fixture.Publish(fixture.FirstRoot, fixture.ProjectRoot, fixture.SecondRoot);
        await catalog.UsePublicationAsync(fixture.Publication.Lease(), this.TestContext.CancellationToken).ConfigureAwait(false);
        var current = await catalog.QueryAsync(new(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = SharedMaterial(current).Cooked!.RootFolderPath.Should().Be(fixture.Second);
        using var restarted = fixture.CreateCatalog();
        var afterRestart = await restarted.QueryAsync(new(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = SharedMaterial(afterRestart).Cooked!.RootFolderPath.Should().Be(fixture.Second);
        var effective = fixture.Reduce(afterRestart).Single(static row => row.Kind == AssetKind.Material);
        _ = fixture.LibraryCopy(effective, fixture.Second).IsCookedSourceOverridden.Should().BeFalse();
    }

    private static AssetRecord SharedMaterial(IReadOnlyList<AssetRecord> records)
        => records.Single(static record => string.Equals(record.Uri.AbsolutePath, "/Content/Shared.omat", StringComparison.Ordinal));

    private sealed class Fixture : IDisposable
    {
        private readonly DirectoryInfo directory = Directory.CreateTempSubdirectory("Oxygen-LibraryCatalog-");
        private readonly NativeStorageProvider storage = new(new RealFileSystem());

        public Fixture()
        {
            _ = Directory.CreateDirectory(Path.Combine(this.directory.FullName, "Content"));
            File.WriteAllText(
                Path.Combine(this.directory.FullName, "Content", "Shared.omat.json"),
                """{"name":"Shared","parameters":{"base_color":[1,0,0,1],"metalness":0,"roughness":0.5}}""");
            (this.ProjectOutput, var projectIndex) = this.WriteRoot(".cooked/Content", 1);
            (this.First, var firstIndex) = this.WriteRoot("Libraries/First", 2);
            (this.Second, var secondIndex) = this.WriteRoot("Libraries/Second", 3);
            this.Projects.Activate(new()
            {
                ProjectId = Guid.NewGuid(),
                Name = "Libraries",
                Category = Category.Games,
                ProjectRoot = this.directory.FullName,
                AuthoringMounts = [new("Content", "Content")],
                LocalFolderMounts = [new("First", this.First), new("Second", this.Second)],
            });
            var project = this.Projects.ActiveProject!;
            this.Publication = new(project.ProjectId, project.ProjectRoot);
            this.ProjectRoot = FakeCookPublication.Root(CookPublicationRootOwner.Project, "Content", projectIndex, this.ProjectOutput);
            this.FirstRoot = FakeCookPublication.Root(CookPublicationRootOwner.Library, "First", firstIndex, this.First);
            this.SecondRoot = FakeCookPublication.Root(CookPublicationRootOwner.Library, "Second", secondIndex, this.Second);
        }

        public string ProjectOutput { get; }

        public string First { get; }

        public string Second { get; }

        public FakeCookPublication.PublishedRoot ProjectRoot { get; }

        public FakeCookPublication.PublishedRoot FirstRoot { get; }

        public FakeCookPublication.PublishedRoot SecondRoot { get; }

        public ProjectContextService Projects { get; } = new();

        public FakeCookPublication Publication { get; }

        public void Publish(params FakeCookPublication.PublishedRoot[] roots)
            => this.Publication.Publish(roots, new Dictionary<string, string> { ["Content"] = this.ProjectOutput });

        public AssetCatalog CreateCatalog()
        {
            var builtins = new Mock<IBuiltinCatalogDiscovery>();
            var snapshot = new BuiltinCatalogSnapshot(Catalog: null, IsLastKnown: false, Notice: null);
            _ = builtins.SetupGet(value => value.Snapshot).Returns(snapshot);
            _ = builtins.Setup(value => value.GetAsync(It.IsAny<CancellationToken>())).ReturnsAsync(snapshot);
            return new(this.Projects, this.storage, builtins.Object, this.Publication);
        }

        public IReadOnlyList<AssetEntry> Reduce(IReadOnlyList<AssetRecord> records)
            => new AssetIdentityReducer().Reduce(records, this.Projects.ActiveProject!, new(this.Projects.ActiveProject!.ProjectId, this.directory.FullName, Path.Combine(this.directory.FullName, ".cooked")));

        /// <summary>Describes the copy of an asset that one library provides.</summary>
        /// <param name="effective">The asset as ordinary resolution selected it.</param>
        /// <param name="library">The library folder.</param>
        /// <returns>The library's own row.</returns>
        public AssetEntry LibraryCopy(AssetEntry effective, string library)
        {
            var source = effective.OverriddenCookedSources.Prepend(effective.CookedMetadata!).Single(metadata => string.Equals(metadata.RootFolderPath, library, StringComparison.OrdinalIgnoreCase));
            return AssetIdentity.FromCookedSource(effective, source, this.Projects.ActiveProject!);
        }

        public void Dispose() => this.directory.Delete(recursive: true);

        private (string Folder, Document Index) WriteRoot(string relative, byte value)
        {
            var root = Path.GetFullPath(Path.Combine(this.directory.FullName, relative));
            _ = Directory.CreateDirectory(Path.Combine(root, "payloads"));
            byte[] bytes = [value];
            File.WriteAllBytes(Path.Combine(root, "payloads", "shared.bin"), bytes);
            var index = new Document(
                1,
                IndexFeatures.HasVirtualPaths,
                Guid.CreateVersion7(),
                [new(new AssetKey(1, 2), "payloads/shared.bin", "/Content/Shared.omat", 1, 1, SHA256.HashData(bytes))],
                []);
            using var stream = File.Create(Path.Combine(root, "container.index.bin"));
            LooseCookedIndexFixture.Write(stream, index);
            return (root, index);
        }
    }
}
