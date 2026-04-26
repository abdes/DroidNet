// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Reactive.Disposables;
using System.Reactive.Linq;
using System.Reactive.Subjects;
using Oxygen.Assets.Catalog;
using Oxygen.Assets.Catalog.FileSystem;
using Oxygen.Assets.Catalog.LooseCooked;
using Oxygen.Editor.Projects;
using Oxygen.Storage;

namespace Oxygen.Editor.ContentBrowser.Infrastructure.Assets;

public sealed class ProjectAssetCatalog : IProjectAssetCatalog, IDisposable
{
    private readonly IProjectContextService projectContextService;
    private readonly IStorageProvider storage;
    private readonly List<IAssetCatalog> catalogs = new();
    private readonly Subject<AssetChange> changes = new();
    private readonly CompositeDisposable catalogSubscriptions = new();
    private readonly SemaphoreSlim lockObj = new(1, 1);
    private bool isDisposed;
    private bool isInitialized;

    public ProjectAssetCatalog(IProjectContextService projectContextService, IStorageProvider storage)
    {
        this.projectContextService = projectContextService;
        this.storage = storage;
    }

    public IObservable<AssetChange> Changes => this.changes.AsObservable();

    public async Task InitializeAsync()
    {
        if (this.isInitialized)
        {
            return;
        }

        if (this.projectContextService.ActiveProject is not { } project)
        {
            Debug.WriteLine("[ProjectAssetCatalog] InitializeAsync skipped: no active project context");
            return;
        }

        await this.lockObj.WaitAsync().ConfigureAwait(false);
        try
        {
            if (this.isInitialized)
            {
                return;
            }

            this.isInitialized = true;
        }
        finally
        {
            this.lockObj.Release();
        }

        Debug.WriteLine($"[ProjectAssetCatalog] Initializing for project at '{project.ProjectRoot}'");

        // Always provide engine-generated assets (e.g. /Engine/Generated/BasicShapes/*) to pickers.
        await this.AddCatalogAsync(new GeneratedAssetCatalog()).ConfigureAwait(false);

        // Index project root
        if (!string.IsNullOrEmpty(project.ProjectRoot))
        {
            var rootOptions = new FileSystemAssetCatalogOptions
            {
                RootFolderPath = project.ProjectRoot,
                MountPoint = "project",
            };
            var rootCatalog = new FileSystemAssetCatalog(this.storage, rootOptions);
            await this.AddCatalogAsync(rootCatalog).ConfigureAwait(false);
        }

        // Index mount points
        foreach (var mount in project.AuthoringMounts)
        {
            try
            {
                var mountRootLocation = this.storage.NormalizeRelativeTo(project.ProjectRoot, mount.RelativePath);

                var mountOptions = new FileSystemAssetCatalogOptions
                {
                    RootFolderPath = mountRootLocation,
                    MountPoint = mount.Name,
                };
                var mountCatalog = new FileSystemAssetCatalog(this.storage, mountOptions);
                await this.AddCatalogAsync(mountCatalog).ConfigureAwait(false);

                // Index cooked outputs for this mount point via the authoritative loose cooked index:
                //   .cooked/<MountPoint>/container.index.bin
                // This is what exposes runtime-consumable assets like .ogeo/.omat/.otex/.oscene as canonical asset:/// URIs.
                var cookedRootLocation = this.storage.NormalizeRelativeTo(project.ProjectRoot, $".cooked/{mount.Name}");
                var cookedOptions = new LooseCookedIndexAssetCatalogOptions
                {
                    CookedRootFolderPath = cookedRootLocation,
                };

                var cookedCatalog = new LooseCookedIndexAssetCatalog(this.storage, cookedOptions);
                await this.AddCatalogAsync(cookedCatalog).ConfigureAwait(false);
            }
            catch (Exception)
            {
                // Log error?
            }
        }
    }

    public async Task<IReadOnlyList<AssetRecord>> QueryAsync(AssetQuery query, CancellationToken cancellationToken = default)
    {
        // Ensure this catalog is initialized when queried.
        // This is important because some consumers (property editors) are created before the Content Browser triggers initialization.
        await this.InitializeAsync().ConfigureAwait(false);

        List<IAssetCatalog> currentCatalogs;
        await this.lockObj.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            currentCatalogs = this.catalogs.ToList();
        }
        finally
        {
            this.lockObj.Release();
        }

        var tasks = currentCatalogs.Select(c => c.QueryAsync(query, cancellationToken));
        var results = await Task.WhenAll(tasks).ConfigureAwait(false);

        return results.SelectMany(r => r)
            .DistinctBy(r => r.Uri)
            .OrderBy(r => r.Uri.ToString(), StringComparer.Ordinal)
            .ToList();
    }

    public async Task AddFolderAsync(Oxygen.Storage.IFolder folder, string mountPoint)
    {
        var options = new FileSystemAssetCatalogOptions
        {
            RootFolderPath = folder.Location,
            MountPoint = mountPoint,
        };

        var catalog = new FileSystemAssetCatalog(this.storage, options);
        await this.AddCatalogAsync(catalog).ConfigureAwait(false);
    }

    public void Dispose()
    {
        if (this.isDisposed)
        {
            return;
        }

        this.isDisposed = true;
        this.changes.Dispose();
        this.catalogSubscriptions.Dispose();
        this.lockObj.Dispose();
    }

    private async Task AddCatalogAsync(IAssetCatalog catalog)
    {
        await this.lockObj.WaitAsync().ConfigureAwait(false);
        try
        {
            this.catalogs.Add(catalog);
            var sub = catalog.Changes.Subscribe(this.changes);
            this.catalogSubscriptions.Add(sub);
            if (catalog is IDisposable d)
            {
                this.catalogSubscriptions.Add(d);
            }
        }
        finally
        {
            this.lockObj.Release();
        }

        // Emit Added events for existing items
        var items = await catalog.QueryAsync(new AssetQuery(AssetQueryScope.All)).ConfigureAwait(false);
        foreach (var item in items)
        {
            this.changes.OnNext(new AssetChange(AssetChangeKind.Added, item.Uri));
        }
    }
}
