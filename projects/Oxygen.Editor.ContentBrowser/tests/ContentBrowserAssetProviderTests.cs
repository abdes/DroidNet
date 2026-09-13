// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Storage;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Infrastructure.Assets;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Assets.Import.Materials;
using Oxygen.Managed.Assets.Model;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentBrowser.Tests;

[TestClass]
public sealed partial class ContentBrowserAssetProviderTests
{
    [TestMethod]
    public async Task RefreshAsync_WhenProjectIsActive_ShouldPublishReducedRows()
    {
        using var workspace = new TempWorkspace();
        WriteMaterial(workspace.SourcePath("Content/Materials/Red.omat.json"));
        var catalog = new TestProjectAssetCatalog(
            [new AssetRecord(new Uri("asset:///Content/Materials/Red.omat.json"))]);
        var projectContext = CreateProjectContextService(workspace);
        using var provider = new ContentBrowserAssetProvider(catalog, projectContext, new TestProjectCookScopeProvider(workspace), new AssetIdentityReducer(), new EmptyCookStatusReader());

        IReadOnlyList<ContentBrowserAssetItem> rows = [];
        provider.Items.Subscribe(new Observer<IReadOnlyList<ContentBrowserAssetItem>>(value => rows = value), this.TestContext.CancellationToken);
        await provider.RefreshAsync(AssetBrowserFilter.Default, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = rows.Should().ContainSingle();
        _ = rows[0].IdentityUri.Should().Be(new Uri("asset:///Content/Materials/Red.omat.json"));
        _ = rows[0].Kind.Should().Be(AssetKind.Material);
        _ = rows[0].PrimaryState.Should().Be(AssetState.Descriptor);
    }

    [TestMethod]
    public async Task RefreshAsync_WhenCatalogSnapshotLags_ShouldRefreshCatalogBeforeReducingRows()
    {
        using var workspace = new TempWorkspace();
        WriteMaterial(workspace.SourcePath("Content/Materials/Red.omat.json"));
        var catalog = new TestProjectAssetCatalog([]);
        catalog.SetRecordsOnRefresh([new AssetRecord(new Uri("asset:///Content/Materials/Red.omat.json"))]);
        var projectContext = CreateProjectContextService(workspace);
        using var provider = new ContentBrowserAssetProvider(catalog, projectContext, new TestProjectCookScopeProvider(workspace), new AssetIdentityReducer(), new EmptyCookStatusReader());

        IReadOnlyList<ContentBrowserAssetItem> rows = [];
        provider.Items.Subscribe(new Observer<IReadOnlyList<ContentBrowserAssetItem>>(value => rows = value), this.TestContext.CancellationToken);

        await provider.RefreshAsync(AssetBrowserFilter.Default, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = catalog.RefreshCount.Should().Be(1);
        _ = rows.Should().ContainSingle();
        _ = rows[0].IdentityUri.Should().Be(new Uri("asset:///Content/Materials/Red.omat.json"));
    }

    [TestMethod]
    public async Task RefreshAsync_WhenCatalogContainsCookedIndexRecord_ShouldPublishCookedRow()
    {
        using var workspace = new TempWorkspace();
        var cookedPath = workspace.SourcePath(".cooked/Content/Materials/Red.omat");
        Directory.CreateDirectory(Path.GetDirectoryName(cookedPath)!);
        await File.WriteAllBytesAsync(cookedPath, [1], this.TestContext.CancellationToken).ConfigureAwait(false);
        var catalog = new TestProjectAssetCatalog(
            [new AssetRecord(new Uri("asset:///Content/Materials/Red.omat"))]);
        var projectContext = CreateProjectContextService(workspace);
        using var provider = new ContentBrowserAssetProvider(catalog, projectContext, new TestProjectCookScopeProvider(workspace), new AssetIdentityReducer(), new EmptyCookStatusReader());

        IReadOnlyList<ContentBrowserAssetItem> rows = [];
        provider.Items.Subscribe(new Observer<IReadOnlyList<ContentBrowserAssetItem>>(value => rows = value), this.TestContext.CancellationToken);
        await provider.RefreshAsync(AssetBrowserFilter.Default, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = rows.Should().ContainSingle();
        _ = rows[0].IdentityUri.Should().Be(new Uri("asset:///Content/Materials/Red.omat"));
        _ = rows[0].PrimaryState.Should().Be(AssetState.Cooked);
        _ = rows[0].CookedPath.Should().Be(cookedPath);
    }

    [TestMethod]
    public async Task ResolveAsync_WhenRecordIsMissing_ShouldReturnUnresolvedRow()
    {
        using var workspace = new TempWorkspace();
        var catalog = new TestProjectAssetCatalog([]);
        var projectContext = CreateProjectContextService(workspace);
        using var provider = new ContentBrowserAssetProvider(catalog, projectContext, new TestProjectCookScopeProvider(workspace), new AssetIdentityReducer(), new EmptyCookStatusReader());
        var uri = new Uri("asset:///Content/Materials/Missing.omat.json");

        var row = await provider.ResolveAsync(uri, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = row.Should().NotBeNull();
        _ = row!.IdentityUri.Should().Be(uri);
        _ = row!.PrimaryState.Should().Be(AssetState.Missing);
        _ = row!.DiagnosticCodes.Should().Contain(AssetIdentityDiagnosticCodes.ResolveMissing);
    }

    [TestMethod]
    public async Task ResolveAsync_WhenCatalogLagsButDescriptorExists_ShouldProbeSourcePath()
    {
        using var workspace = new TempWorkspace();
        var descriptorPath = workspace.SourcePath("Content/Materials/Lagged.omat.json");
        Directory.CreateDirectory(Path.GetDirectoryName(descriptorPath)!);
        await File.WriteAllTextAsync(descriptorPath, "{ invalid json", this.TestContext.CancellationToken).ConfigureAwait(false);
        var catalog = new TestProjectAssetCatalog([]);
        var projectContext = CreateProjectContextService(workspace);
        using var provider = new ContentBrowserAssetProvider(catalog, projectContext, new TestProjectCookScopeProvider(workspace), new AssetIdentityReducer(), new EmptyCookStatusReader());
        var uri = new Uri("asset:///Content/Materials/Lagged.omat.json");

        var row = await provider.ResolveAsync(uri, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = row.Should().NotBeNull();
        _ = row!.IdentityUri.Should().Be(uri);
        _ = row!.PrimaryState.Should().Be(AssetState.Broken);
        _ = row!.DescriptorPath.Should().Be(descriptorPath);
        _ = row!.DiagnosticCodes.Should().Contain(AssetIdentityDiagnosticCodes.DescriptorBroken);
    }

    [TestMethod]
    public async Task CatalogChange_ShouldRefreshPublishedRows()
    {
        using var workspace = new TempWorkspace();
        WriteMaterial(workspace.SourcePath("Content/Materials/Red.omat.json"));
        WriteMaterial(workspace.SourcePath("Content/Materials/Blue.omat.json"));
        var catalog = new TestProjectAssetCatalog(
            [new AssetRecord(new Uri("asset:///Content/Materials/Red.omat.json"))]);
        var projectContext = CreateProjectContextService(workspace);
        using var provider = new ContentBrowserAssetProvider(catalog, projectContext, new TestProjectCookScopeProvider(workspace), new AssetIdentityReducer(), new EmptyCookStatusReader());

        IReadOnlyList<ContentBrowserAssetItem> rows = [];
        provider.Items.Subscribe(new Observer<IReadOnlyList<ContentBrowserAssetItem>>(value => rows = value), this.TestContext.CancellationToken);
        await provider.RefreshAsync(AssetBrowserFilter.Default, this.TestContext.CancellationToken).ConfigureAwait(false);

        catalog.SetRecords(
            [
                new AssetRecord(new Uri("asset:///Content/Materials/Red.omat.json")),
                new AssetRecord(new Uri("asset:///Content/Materials/Blue.omat.json")),
            ]);
        catalog.Publish(new AssetChange(AssetChangeKind.Added, new Uri("asset:///Content/Materials/Blue.omat.json")));

        for (var i = 0; i < 20 && rows.Count < 2; i++)
        {
            await Task.Delay(25, this.TestContext.CancellationToken).ConfigureAwait(false);
        }

        _ = rows.Should().HaveCount(2);
        _ = rows.Should().Contain(row => row!.IdentityUri == new Uri("asset:///Content/Materials/Blue.omat.json"));
    }

    [TestMethod]
    public async Task RefreshAsync_WhenConsumersPassDifferentFilters_ShouldPublishFullWorkspaceSnapshot()
    {
        using var workspace = new TempWorkspace();
        WriteMaterial(workspace.SourcePath("Content/Materials/Red.omat.json"));
        var catalog = new TestProjectAssetCatalog(
            [
                new AssetRecord(new Uri("asset:///Content/Materials/Red.omat.json")),
                new AssetRecord(new Uri("asset:///Content/Images/Preview.png")),
            ]);
        var projectContext = CreateProjectContextService(workspace);
        using var provider = new ContentBrowserAssetProvider(catalog, projectContext, new TestProjectCookScopeProvider(workspace), new AssetIdentityReducer(), new EmptyCookStatusReader());

        IReadOnlyList<ContentBrowserAssetItem> rows = [];
        provider.Items.Subscribe(new Observer<IReadOnlyList<ContentBrowserAssetItem>>(value => rows = value), this.TestContext.CancellationToken);

        await provider.RefreshAsync(AssetBrowserFilter.Default with { Kinds = new HashSet<AssetKind> { AssetKind.Material } }, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = rows.Should().HaveCount(2);

        await provider.RefreshAsync(AssetBrowserFilter.Default with { Kinds = new HashSet<AssetKind> { AssetKind.Image } }, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = rows.Should().HaveCount(2);
        _ = rows.Should().Contain(row => row.Kind == AssetKind.Material);
        _ = rows.Should().Contain(row => row.Kind == AssetKind.Image);
    }

    private static ProjectContextService CreateProjectContextService(TempWorkspace workspace)
    {
        var service = new ProjectContextService();
        service.Activate(
            new ProjectContext
            {
                ProjectId = Guid.NewGuid(),
                Name = "Test",
                Category = Category.Games,
                ProjectRoot = workspace.Root,
                AuthoringMounts = [new ProjectMountPoint("Content", "Content")],
                LocalFolderMounts = [],
                Scenes = [],
            });

        return service;
    }

    private static void WriteMaterial(string path)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        using var stream = File.Create(path);
        MaterialSourceWriter.Write(
            stream,
            new MaterialSource(
                "oxygen.material.v1",
                "PBR",
                "Material",
                new MaterialPbrMetallicRoughness(
                    1.0f,
                    0.0f,
                    0.0f,
                    1.0f,
                    metallicFactor: 0.0f,
                    roughnessFactor: 0.5f,
                    baseColorTexture: null,
                    metallicRoughnessTexture: null),
                normalTexture: null,
                occlusionTexture: null,
                alphaMode: MaterialAlphaMode.Opaque,
                alphaCutoff: 0.5f,
                doubleSided: false));
    }

    private sealed class TestProjectAssetCatalog(IReadOnlyList<AssetRecord> records) : IProjectAssetCatalog
    {
        private readonly ChangeObservable changes = new();
        private IReadOnlyList<AssetRecord> records = records;
        private IReadOnlyList<AssetRecord>? recordsOnRefresh;

        public IObservable<AssetChange> Changes => this.changes;

        public int RefreshCount { get; private set; }

        public Task<IReadOnlyList<AssetRecord>> QueryAsync(AssetQuery query, CancellationToken cancellationToken = default)
        {
            _ = query;
            cancellationToken.ThrowIfCancellationRequested();
            return Task.FromResult(this.records);
        }

        public Task InitializeAsync() => Task.CompletedTask;

        public Task RefreshAsync(CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();
            this.RefreshCount++;
            if (this.recordsOnRefresh is not null)
            {
                this.records = this.recordsOnRefresh;
                this.recordsOnRefresh = null;
            }

            return Task.CompletedTask;
        }

        public Task AddFolderAsync(IFolder folder, string mountPoint)
        {
            _ = folder;
            _ = mountPoint;
            return Task.CompletedTask;
        }

        public void SetRecords(IReadOnlyList<AssetRecord> nextRecords) => this.records = nextRecords;

        public void SetRecordsOnRefresh(IReadOnlyList<AssetRecord> nextRecords) => this.recordsOnRefresh = nextRecords;

        public void Publish(AssetChange change) => this.changes.Publish(change);
    }

    private sealed class TestProjectCookScopeProvider(TempWorkspace workspace) : IProjectCookScopeProvider
    {
        public ProjectCookScope CreateScope(ProjectContext context)
            => new(context.ProjectId, context.ProjectRoot, workspace.SourcePath(".cooked"));
    }

    private sealed partial class ChangeObservable : IObservable<AssetChange>
    {
        private readonly List<IObserver<AssetChange>> observers = [];

        public IDisposable Subscribe(IObserver<AssetChange> observer)
        {
            this.observers.Add(observer);
            return new Subscription(this.observers, observer);
        }

        public void Publish(AssetChange change)
        {
            foreach (var observer in this.observers.ToArray())
            {
                observer.OnNext(change);
            }
        }

        private sealed partial class Subscription(List<IObserver<AssetChange>> observers, IObserver<AssetChange> observer) : IDisposable
        {
            public void Dispose() => observers.Remove(observer);
        }
    }

    private sealed class Observer<T>(Action<T> onNext) : IObserver<T>
    {
        public void OnCompleted()
        {
        }

        public void OnError(Exception error) => throw error;

        public void OnNext(T value) => onNext(value);
    }

    private sealed class EmptyCookStatusReader : IAssetCookStatusReader
    {
        public Task<IReadOnlyList<AssetCookStatus>> ReadAsync(ProjectContext project, IReadOnlyList<Uri> assetUris, CancellationToken cancellationToken = default)
            => Task.FromResult<IReadOnlyList<AssetCookStatus>>([]);
    }

    private sealed partial class TempWorkspace : IDisposable
    {
        public TempWorkspace()
        {
            this.Root = Path.Combine(Path.GetTempPath(), "oxygen-content-browser-provider-tests", Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(this.Root);
        }

        public string Root { get; }

        public string SourcePath(string relative)
            => Path.Combine(this.Root, relative.Replace('/', Path.DirectorySeparatorChar));

        public void Dispose()
        {
            if (Directory.Exists(this.Root))
            {
                Directory.Delete(this.Root, recursive: true);
            }
        }
    }
}
