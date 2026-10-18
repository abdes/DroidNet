// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets.Authoring.Materials;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core.Diagnostics;
using Oxygen.Editor.Assets.TestSupport;

namespace Oxygen.Editor.Assets.Unit.Tests;

[TestClass]
public sealed partial class AssetFeedTests
{
    [TestMethod]
    public async Task RefreshAsync_WhenProjectIsActive_ShouldPublishReducedRows()
    {
        using var workspace = new AssetWorkspace();
        WriteMaterial(workspace.SourcePath("Content/Materials/Red.omat.json"));
        var catalog = new TestProjectAssetCatalog(
            [new AssetRecord(new Uri("asset:///Content/Materials/Red.omat.json"))]);
        var projectContext = workspace.ActivateProject();
        var unavailableRuntime = AssetStatusFixture.CreateUnavailableRuntimeStatus();
        using var provider = new AssetFeed(catalog, projectContext, new WorkspaceCookScopeProvider(workspace), new EmptyCookStatusReader(), new CookDocumentRegistry(), EmptyCookRuns(), unavailableRuntime, NoRelocations(), NoAuthoring());

        IReadOnlyList<AssetEntry> rows = [];
        provider.Items.Subscribe(new Observer<IReadOnlyList<AssetEntry>>(value => rows = value), this.TestContext.CancellationToken);
        await provider.RefreshAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = rows.Should().ContainSingle();
        _ = rows[0].IdentityUri.Should().Be(new Uri("asset:///Content/Materials/Red.omat.json"));
        _ = rows[0].Kind.Should().Be(AssetKind.Material);
        _ = rows[0].PrimaryState.Should().Be(AssetState.Descriptor);
    }

    [TestMethod]
    public async Task RefreshAsync_WhenCatalogSnapshotLags_ShouldRefreshCatalogBeforeReducingRows()
    {
        using var workspace = new AssetWorkspace();
        WriteMaterial(workspace.SourcePath("Content/Materials/Red.omat.json"));
        var catalog = new TestProjectAssetCatalog([]);
        catalog.SetRecordsOnRefresh([new AssetRecord(new Uri("asset:///Content/Materials/Red.omat.json"))]);
        var projectContext = workspace.ActivateProject();
        var unavailableRuntime = AssetStatusFixture.CreateUnavailableRuntimeStatus();
        using var provider = new AssetFeed(catalog, projectContext, new WorkspaceCookScopeProvider(workspace), new EmptyCookStatusReader(), new CookDocumentRegistry(), EmptyCookRuns(), unavailableRuntime, NoRelocations(), NoAuthoring());

        IReadOnlyList<AssetEntry> rows = [];
        provider.Items.Subscribe(new Observer<IReadOnlyList<AssetEntry>>(value => rows = value), this.TestContext.CancellationToken);

        await provider.RefreshAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = catalog.RefreshCount.Should().Be(1);
        _ = rows.Should().ContainSingle();
        _ = rows[0].IdentityUri.Should().Be(new Uri("asset:///Content/Materials/Red.omat.json"));
    }

    [TestMethod]
    public async Task RefreshAsync_WhenCatalogContainsCookedIndexRecord_ShouldPublishCookedRow()
    {
        using var workspace = new AssetWorkspace();
        var cookedPath = workspace.SourcePath(".cooked/Content/Materials/Red.omat");
        Directory.CreateDirectory(Path.GetDirectoryName(cookedPath)!);
        await File.WriteAllBytesAsync(cookedPath, [1], this.TestContext.CancellationToken).ConfigureAwait(false);
        var catalog = new TestProjectAssetCatalog(
            [IndexedOutput(new Uri("asset:///Content/Materials/Red.omat"), cookedPath)]);
        var projectContext = workspace.ActivateProject();
        var unavailableRuntime = AssetStatusFixture.CreateUnavailableRuntimeStatus();
        using var provider = new AssetFeed(catalog, projectContext, new WorkspaceCookScopeProvider(workspace), new EmptyCookStatusReader(), new CookDocumentRegistry(), EmptyCookRuns(), unavailableRuntime, NoRelocations(), NoAuthoring());

        IReadOnlyList<AssetEntry> rows = [];
        provider.Items.Subscribe(new Observer<IReadOnlyList<AssetEntry>>(value => rows = value), this.TestContext.CancellationToken);
        await provider.RefreshAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = rows.Should().ContainSingle();
        _ = rows[0].IdentityUri.Should().Be(new Uri("asset:///Content/Materials/Red.omat"));
        _ = rows[0].PrimaryState.Should().Be(AssetState.Cooked);
        _ = rows[0].CookedPath.Should().Be(cookedPath);
    }

    [TestMethod]
    public async Task ResolveAsync_WhenRecordIsMissing_ShouldReturnUnresolvedRow()
    {
        using var workspace = new AssetWorkspace();
        var catalog = new TestProjectAssetCatalog([]);
        var projectContext = workspace.ActivateProject();
        var unavailableRuntime = AssetStatusFixture.CreateUnavailableRuntimeStatus();
        using var provider = new AssetFeed(catalog, projectContext, new WorkspaceCookScopeProvider(workspace), new EmptyCookStatusReader(), new CookDocumentRegistry(), EmptyCookRuns(), unavailableRuntime, NoRelocations(), NoAuthoring());
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
        using var workspace = new AssetWorkspace();
        var descriptorPath = workspace.SourcePath("Content/Materials/Lagged.omat.json");
        Directory.CreateDirectory(Path.GetDirectoryName(descriptorPath)!);
        await File.WriteAllTextAsync(descriptorPath, "{ invalid json", this.TestContext.CancellationToken).ConfigureAwait(false);
        var catalog = new TestProjectAssetCatalog([]);
        var projectContext = workspace.ActivateProject();
        var unavailableRuntime = AssetStatusFixture.CreateUnavailableRuntimeStatus();
        using var provider = new AssetFeed(catalog, projectContext, new WorkspaceCookScopeProvider(workspace), new EmptyCookStatusReader(), new CookDocumentRegistry(), EmptyCookRuns(), unavailableRuntime, NoRelocations(), NoAuthoring());
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
        using var workspace = new AssetWorkspace();
        WriteMaterial(workspace.SourcePath("Content/Materials/Red.omat.json"));
        WriteMaterial(workspace.SourcePath("Content/Materials/Blue.omat.json"));
        var catalog = new TestProjectAssetCatalog(
            [new AssetRecord(new Uri("asset:///Content/Materials/Red.omat.json"))]);
        var projectContext = workspace.ActivateProject();
        var unavailableRuntime = AssetStatusFixture.CreateUnavailableRuntimeStatus();
        using var provider = new AssetFeed(catalog, projectContext, new WorkspaceCookScopeProvider(workspace), new EmptyCookStatusReader(), new CookDocumentRegistry(), EmptyCookRuns(), unavailableRuntime, NoRelocations(), NoAuthoring());

        IReadOnlyList<AssetEntry> rows = [];
        provider.Items.Subscribe(new Observer<IReadOnlyList<AssetEntry>>(value => rows = value), this.TestContext.CancellationToken);
        await provider.RefreshAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

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
    public async Task RefreshAsync_ShouldPublishTheFullWorkspaceSnapshot()
    {
        using var workspace = new AssetWorkspace();
        WriteMaterial(workspace.SourcePath("Content/Materials/Red.omat.json"));
        var catalog = new TestProjectAssetCatalog(
            [
                new AssetRecord(new Uri("asset:///Content/Materials/Red.omat.json")),
                new AssetRecord(new Uri("asset:///Content/Images/Preview.png")),
            ]);
        var projectContext = workspace.ActivateProject();
        var unavailableRuntime = AssetStatusFixture.CreateUnavailableRuntimeStatus();
        using var provider = new AssetFeed(catalog, projectContext, new WorkspaceCookScopeProvider(workspace), new EmptyCookStatusReader(), new CookDocumentRegistry(), EmptyCookRuns(), unavailableRuntime, NoRelocations(), NoAuthoring());

        IReadOnlyList<AssetEntry> rows = [];
        provider.Items.Subscribe(new Observer<IReadOnlyList<AssetEntry>>(value => rows = value), this.TestContext.CancellationToken);

        await provider.RefreshAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = rows.Should().HaveCount(2);
        _ = rows.Should().Contain(row => row.Kind == AssetKind.Material);
        _ = rows.Should().Contain(row => row.Kind == AssetKind.Image);
    }

    private static AssetRecord IndexedOutput(Uri uri, string path, byte type = 1)
    {
        var descriptor = uri.AbsolutePath["/Content/".Length..];
        var root = path[..^(descriptor.Length + 1)];
        return new(uri) { Cooked = new(root, descriptor, Guid.NewGuid(), new(1, 2), type, 6, new string('0', 64)) { VirtualPath = uri.AbsolutePath } };
    }

    private static void WriteMaterial(string path)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        using var stream = File.Create(path);
        MaterialSourceWriter.Write(
            stream,
            new MaterialSource(
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

    private sealed class TestProjectAssetCatalog(IReadOnlyList<AssetRecord> records) : IAssetCatalogReader
    {
        private readonly ChangeObservable changes = new();
        private IReadOnlyList<AssetRecord> records = records;
        private IReadOnlyList<AssetRecord>? recordsOnRefresh;

        public IObservable<AssetChange> Changes => this.changes;

        public Oxygen.Editor.ContentPipeline.Publication.ICookPublicationReadLease? Publication { get; init; }

        public string? PublicationError { get; init; }

        public int RefreshCount { get; private set; }

        public Task<IReadOnlyList<AssetRecord>> QueryAsync(AssetQuery query, CancellationToken cancellationToken = default)
        {
            _ = query;
            cancellationToken.ThrowIfCancellationRequested();
            return Task.FromResult(this.records);
        }

        public Task<AssetCatalogSnapshot> ReadSnapshotAsync(AssetQuery query, CancellationToken cancellationToken = default)
            => Task.FromResult(new AssetCatalogSnapshot(this.records, this.Publication, this.PublicationError));

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

        public void SetRecords(IReadOnlyList<AssetRecord> nextRecords) => this.records = nextRecords;

        public void SetRecordsOnRefresh(IReadOnlyList<AssetRecord> nextRecords) => this.recordsOnRefresh = nextRecords;

        public void Publish(AssetChange change) => this.changes.Publish(change);
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

    private sealed class EmptyCookStatusReader : IAssetCookStatusReader
    {
        public event EventHandler? Changed { add { } remove { } }

        public Task<IReadOnlyList<AssetCookStatus>> ReadAsync(ProjectContext project, Oxygen.Editor.ContentPipeline.Publication.ICookPublicationReadLease publication, IReadOnlyList<Uri> assetUris, CancellationToken cancellationToken = default)
            => this.ReadAsync(project, assetUris, cancellationToken);

        public Task<IReadOnlyList<AssetCookStatus>> ReadAsync(ProjectContext project, IReadOnlyList<Uri> assetUris, CancellationToken cancellationToken = default)
            => Task.FromResult<IReadOnlyList<AssetCookStatus>>([]);
    }
}
