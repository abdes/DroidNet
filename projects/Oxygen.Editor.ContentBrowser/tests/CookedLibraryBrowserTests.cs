// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using AwesomeAssertions;
using DroidNet.Storage.Native;
using Moq;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Infrastructure.Assets;
using Oxygen.Editor.ContentPipeline.Discovery;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Assets.Persistence.LooseCooked.V1;
using Testably.Abstractions;

namespace Oxygen.Editor.ContentBrowser.Tests;

/// <summary>Exercises accepted project configuration, native resolution and physical library browsing together.</summary>
[TestClass]
public sealed partial class CookedLibraryBrowserTests
{
    /// <summary>Gets or sets the running test context.</summary>
    public TestContext TestContext { get; set; } = null!;

    /// <summary>A library scope shows its own copy and explains the source used by assignments.</summary>
    /// <returns>The asynchronous physical-library browsing regression.</returns>
    [TestMethod]
    public async Task LibraryScopeShowsItsOwnOverriddenCopyWithoutInventingSourceOwnership()
    {
        using var fixture = new Fixture();
        using var catalog = fixture.CreateCatalog();
        var records = await catalog.QueryAsync(new(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false);
        var rows = fixture.Reduce(records);
        _ = rows.Where(static row => row.Kind == AssetKind.Material).Should().ContainSingle();
        var effective = rows.Single(static row => row.Kind == AssetKind.Material);
        _ = effective.CookedMetadata!.RootFolderPath.Should().Be(fixture.ProjectOutput);
        _ = effective.OverriddenCookedSources.Should().HaveCount(2);
        var library = CookedLibraryProjection.ForFolders(rows, fixture.Projects.ActiveProject!, ["/First"]).Single();
        _ = library.DisplayPath.Should().Be("/First/payloads/shared.bin");
        _ = library.CookedMetadata!.RootFolderPath.Should().Be(fixture.First);
        _ = library.PrimaryBadge.Should().Be("Overridden");
        _ = library.PrimaryBadgeTooltip.Should().Contain("project output");
        _ = library.DescriptorPath.Should().BeNull();
        _ = library.SourcePath.Should().BeNull();
        _ = library.CanCook.Should().BeFalse();
        _ = library.CookedUri.Should().Be(new Uri("asset:///Content/Shared.omat"));
    }

    /// <summary>Reconfiguration replaces registrations and restart preserves the same effective source.</summary>
    /// <returns>The asynchronous ordering and reinitialization regression.</returns>
    [TestMethod]
    public async Task ChangedPriorityAndRestartSelectTheSameSource()
    {
        using var fixture = new Fixture();
        using var catalog = fixture.CreateCatalog();
        _ = await catalog.QueryAsync(new(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false);
        fixture.Projects.Activate(fixture.Projects.ActiveProject! with
        {
            CookedContentOrder = [new(CookedContentSourceKind.LocalFolder, "First"), new(CookedContentSourceKind.ProjectOutput), new(CookedContentSourceKind.LocalFolder, "Second")],
        });
        var current = await catalog.QueryAsync(new(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = current.Single(static record => string.Equals(record.Uri.AbsolutePath, "/Content/Shared.omat", StringComparison.Ordinal)).Cooked!.RootFolderPath.Should().Be(fixture.Second);
        using var restarted = fixture.CreateCatalog();
        var afterRestart = await restarted.QueryAsync(new(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = afterRestart.Single(static record => string.Equals(record.Uri.AbsolutePath, "/Content/Shared.omat", StringComparison.Ordinal)).Cooked!.RootFolderPath.Should().Be(fixture.Second);
        var second = CookedLibraryProjection.ForFolders(fixture.Reduce(afterRestart), fixture.Projects.ActiveProject!, ["/Second"]).Single();
        _ = second.IsCookedSourceOverridden.Should().BeFalse();
    }

    private sealed partial class Fixture : IDisposable
    {
        private readonly DirectoryInfo directory = Directory.CreateTempSubdirectory("Oxygen-LibraryBrowser-");
        private readonly NativeStorageProvider storage = new(new RealFileSystem());

        public Fixture()
        {
            _ = Directory.CreateDirectory(Path.Combine(this.directory.FullName, "Content"));
            File.WriteAllText(
                Path.Combine(this.directory.FullName, "Content", "Shared.omat.json"),
                """{"Schema":"oxygen.material.v1","Type":"PBR","Name":"Shared","PbrMetallicRoughness":{"BaseColorFactor":[1,0,0,1],"MetallicFactor":0,"RoughnessFactor":0.5}}""");
            this.ProjectOutput = this.WriteRoot(".cooked/Content", 1);
            this.First = this.WriteRoot("Libraries/First", 2);
            this.Second = this.WriteRoot("Libraries/Second", 3);
            this.Projects.Activate(new()
            {
                ProjectId = Guid.NewGuid(), Name = "Libraries", Category = Category.Games, ProjectRoot = this.directory.FullName,
                AuthoringMounts = [new("Content", "Content")], LocalFolderMounts = [new("First", this.First), new("Second", this.Second)], Scenes = [],
            });
        }

        public string ProjectOutput { get; }

        public string First { get; }

        public string Second { get; }

        public ProjectContextService Projects { get; } = new();

        public ProjectAssetCatalog CreateCatalog()
        {
            var builtins = new Mock<IBuiltinCatalogDiscovery>();
            var snapshot = new BuiltinCatalogSnapshot(Catalog: null, IsLastKnown: false, Notice: null);
            _ = builtins.SetupGet(value => value.Snapshot).Returns(snapshot);
            _ = builtins.Setup(value => value.GetAsync(It.IsAny<CancellationToken>())).ReturnsAsync(snapshot);
            return new(this.Projects, this.storage, builtins.Object);
        }

        public IReadOnlyList<ContentBrowserAssetItem> Reduce(IReadOnlyList<AssetRecord> records)
            => new AssetIdentityReducer().Reduce(records, this.Projects.ActiveProject!, new(this.Projects.ActiveProject!.ProjectId, this.directory.FullName, Path.Combine(this.directory.FullName, ".cooked")), AssetBrowserFilter.Default);

        public void Dispose() => this.directory.Delete(recursive: true);

        private string WriteRoot(string relative, byte value)
        {
            var root = Path.GetFullPath(Path.Combine(this.directory.FullName, relative));
            _ = Directory.CreateDirectory(Path.Combine(root, "payloads"));
            byte[] bytes = [value];
            File.WriteAllBytes(Path.Combine(root, "payloads", "shared.bin"), bytes);
            using var stream = File.Create(Path.Combine(root, "container.index.bin"));
            LooseCookedIndex.Write(stream, new Document(
                1,
                IndexFeatures.HasVirtualPaths,
                Guid.CreateVersion7(),
                [new(new AssetKey(1, 2), "payloads/shared.bin", "/Content/Shared.omat", 1, 1, SHA256.HashData(bytes))],
                []));
            return root;
        }
    }
}
