// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using System.Text.Json;
using AwesomeAssertions;
using DroidNet.Storage;
using DroidNet.Storage.Native;
using Microsoft.Extensions.Logging.Abstractions;
using Moq;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Infrastructure.Assets;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Discovery;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Assets.Persistence.LooseCooked.V3;
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
        await fixture.PublishSelectionAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
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
        await fixture.PublishSelectionAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        using var catalog = fixture.CreateCatalog();
        _ = await catalog.QueryAsync(new(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false);
        fixture.Projects.Activate(fixture.Projects.ActiveProject! with
        {
            CookedContentOrder = [new(CookedContentSourceKind.LocalFolder, "First"), new(CookedContentSourceKind.ProjectOutput), new(CookedContentSourceKind.LocalFolder, "Second")],
        });
        await fixture.PublishSelectionAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var current = await catalog.QueryAsync(new(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = current.Single(static record => string.Equals(record.Uri.AbsolutePath, "/Content/Shared.omat", StringComparison.Ordinal)).Cooked!.RootFolderPath.Should().Be(fixture.Second);
        using var restarted = fixture.CreateCatalog();
        var afterRestart = await restarted.QueryAsync(new(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = afterRestart.Single(static record => string.Equals(record.Uri.AbsolutePath, "/Content/Shared.omat", StringComparison.Ordinal)).Cooked!.RootFolderPath.Should().Be(fixture.Second);
        var second = CookedLibraryProjection.ForFolders(fixture.Reduce(afterRestart), fixture.Projects.ActiveProject!, ["/Second"]).Single();
        _ = second.IsCookedSourceOverridden.Should().BeFalse();
    }

    /// <summary>A library re-cooked since the last publication is named, republished and browsed instead of failing the catalog.</summary>
    /// <returns>The asynchronous stale-library recovery regression.</returns>
    [TestMethod]
    public async Task RecookedLibraryIsRepublishedWhenTheBrowserRefreshes()
    {
        var token = this.TestContext.CancellationToken;
        using var fixture = new Fixture();
        await fixture.PublishSelectionAsync(token).ConfigureAwait(false);
        fixture.RecookFirst();
        var current = await CookedIndexSnapshot.ReadAsync(fixture.First, token).ConfigureAwait(false);
        using (var stale = await fixture.Publication.AcquireReadAsync(fixture.Projects.ActiveProject!, token).ConfigureAwait(false))
        {
            var recorded = stale.Roots.Single(static root => string.Equals(root.Name, "First", StringComparison.Ordinal));
            Func<Task> open = () => stale.CreateCatalogAsync(recorded, token);
            var changed = (await open.Should().ThrowAsync<CookedLibraryChangedException>().ConfigureAwait(false)).Which;
            _ = changed.Message.Should().Contain("'First'").And.Contain(fixture.First)
                .And.Contain(recorded.SourceKey.ToString("D")).And.Contain(recorded.IndexSha256)
                .And.Contain(current.Index.SourceGuid.ToString("D")).And.Contain(current.Fingerprint);
        }

        await using var catalog = fixture.CreateCatalog();
        await catalog.RefreshAsync(token).ConfigureAwait(false);
        using var snapshot = await catalog.ReadSnapshotAsync(new(AssetQueryScope.All), token).ConfigureAwait(false);
        _ = snapshot.PublicationError.Should().BeNull();
        var republished = snapshot.Publication!.Roots.Single(static root => string.Equals(root.Name, "First", StringComparison.Ordinal));
        _ = republished.SourceKey.Should().Be(current.Index.SourceGuid);
        _ = republished.IndexSha256.Should().Be(current.Fingerprint);
        var library = CookedLibraryProjection.ForFolders(fixture.Reduce(snapshot.Records), fixture.Projects.ActiveProject!, ["/First"]).Single();
        _ = library.CookedMetadata!.RootFolderPath.Should().Be(fixture.First);
    }

    private sealed partial class Fixture : IDisposable
    {
        private readonly DirectoryInfo directory = Directory.CreateTempSubdirectory("Oxygen-LibraryBrowser-");
        private readonly NativeStorageProvider storage = new(new RealFileSystem());
        private readonly NativeAtomicFileStore files = new(new RealFileSystem());
        private readonly Guid sourceKey = Guid.CreateVersion7();
        private readonly ContentCookCoordinator coordinator;

        public Fixture()
        {
            _ = Directory.CreateDirectory(Path.Combine(this.directory.FullName, "Content"));
            File.WriteAllText(
                Path.Combine(this.directory.FullName, "Content", "Shared.omat.json"),
                """{"name":"Shared","parameters":{"base_color":[1,0,0,1],"metalness":0,"roughness":0.5}}""");
            this.ProjectOutput = this.WriteRoot(Path.GetRelativePath(this.directory.FullName, CookPublicationPaths.Generation(this.directory.FullName, this.sourceKey)), 1, this.sourceKey);
            File.WriteAllBytes(Path.Combine(this.ProjectOutput, CookedGeneration.MarkerFileName), []);
            this.First = this.WriteRoot("Libraries/First", 2);
            this.Second = this.WriteRoot("Libraries/Second", 3);
            this.Projects.Activate(new()
            {
                ProjectId = Guid.NewGuid(),
                Name = "Libraries",
                Category = Category.Games,
                ProjectRoot = this.directory.FullName,
                AuthoringMounts = [new("Content", "Content")],
                LocalFolderMounts = [new("First", this.First), new("Second", this.Second)],
                Scenes = [],
            });
            var project = this.Projects.ActiveProject!;
            File.WriteAllText(Path.Combine(this.directory.FullName, "Project.oxy"), ProjectInfo.ToJson(new ProjectInfo(project.ProjectId, project.Name, project.Category, project.ProjectRoot)
            {
                AuthoringMounts = [.. project.AuthoringMounts],
                LocalFolderMounts = [.. project.LocalFolderMounts],
            }));
            this.coordinator = new(this.Projects, NullLogger<ContentCookCoordinator>.Instance);
            this.Publication = new(this.coordinator, this.Projects, this.files, new ProjectManagerService(this.storage, atomicFiles: this.files));
        }

        public string ProjectOutput { get; }

        public string First { get; }

        public string Second { get; }

        public ProjectContextService Projects { get; } = new();

        public CookPublicationService Publication { get; }

        public ProjectAssetCatalog CreateCatalog()
        {
            var builtins = new Mock<IBuiltinCatalogDiscovery>();
            var snapshot = new BuiltinCatalogSnapshot(Catalog: null, IsLastKnown: false, Notice: null);
            _ = builtins.SetupGet(value => value.Snapshot).Returns(snapshot);
            _ = builtins.Setup(value => value.GetAsync(It.IsAny<CancellationToken>())).ReturnsAsync(snapshot);
            return new(this.Projects, this.storage, builtins.Object, this.Publication);
        }

        /// <summary>Replaces the first library the way an external cook does: a new source identity and payload.</summary>
        public void RecookFirst() => _ = this.WriteRoot("Libraries/First", 4);

        public IReadOnlyList<ContentBrowserAssetItem> Reduce(IReadOnlyList<AssetRecord> records)
            => new AssetIdentityReducer().Reduce(records, this.Projects.ActiveProject!, new(this.Projects.ActiveProject!.ProjectId, this.directory.FullName, Path.Combine(this.directory.FullName, ".cooked")), AssetBrowserFilter.Default);

        public async Task PublishSelectionAsync(CancellationToken token)
        {
            var project = this.Projects.ActiveProject!;
            using var libraries = await CookedLibraryReadSet.AcquireAsync(project, token).ConfigureAwait(false);
            var index = await CookedIndexSnapshot.ReadAsync(this.ProjectOutput, token).ConfigureAwait(false);
            var roots = libraries.OrderBindings([new(CookPublicationRootOwner.Project, "Content", this.sourceKey, index.Fingerprint, null)]);
            var document = new CookPublicationDocument(CookPublicationDocument.CurrentVersion, project.ProjectId, Guid.NewGuid(),
                DateTimeOffset.UtcNow, CookPublicationDocument.ConfigurationIdentity(project), roots, [], CookInputs: null);
            using var gate = await CookOutputLease.AcquireWriteAsync(project.ProjectRoot, token).ConfigureAwait(false);
            var previous = await this.files.ReadAsync(CookPublicationPaths.Head(project.ProjectRoot), token).ConfigureAwait(false);
            var path = CookPublicationPaths.Document(project.ProjectRoot, document.OperationId);
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            var version = await this.files.WriteAsync(path, JsonSerializer.SerializeToUtf8Bytes(document, CookPublicationDocument.JsonOptions), FileVersion.Missing, token).ConfigureAwait(false);
            var head = new CookPublicationHead(CookPublicationHead.CurrentVersion, document.OperationId, version.Sha256);
            _ = await this.files.WriteAsync(CookPublicationPaths.Head(project.ProjectRoot), JsonSerializer.SerializeToUtf8Bytes(head, CookPublicationDocument.JsonOptions), previous.Version, token).ConfigureAwait(false);
        }

        public void Dispose()
        {
            this.coordinator.Dispose();
            this.directory.Delete(recursive: true);
        }

        private string WriteRoot(string relative, byte value, Guid? sourceKey = null)
        {
            var root = Path.GetFullPath(Path.Combine(this.directory.FullName, relative));
            _ = Directory.CreateDirectory(Path.Combine(root, "payloads"));
            byte[] bytes = [value];
            File.WriteAllBytes(Path.Combine(root, "payloads", "shared.bin"), bytes);
            using var stream = File.Create(Path.Combine(root, "container.index.bin"));
            Oxygen.Testing.LooseCookedIndexFixture.Write(stream, new Document(
                1,
                IndexFeatures.HasVirtualPaths,
                sourceKey ?? Guid.CreateVersion7(),
                [new(new AssetKey(1, 2), "payloads/shared.bin", "/Content/Shared.omat", 1, 1, SHA256.HashData(bytes))],
                []));
            return root;
        }
    }
}
