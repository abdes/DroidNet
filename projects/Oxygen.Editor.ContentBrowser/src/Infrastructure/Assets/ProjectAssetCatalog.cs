// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Disposables;
using System.Reactive.Linq;
using System.Reactive.Subjects;
using Oxygen.Assets.Catalog;
using Oxygen.Assets.Catalog.FileSystem;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentBrowser.Infrastructure.Assets;

public sealed class ProjectAssetCatalog : IProjectAssetCatalog, IDisposable
{
    private readonly IProjectManagerService projectManager;
    private readonly List<IAssetCatalog> catalogs = new();
    private readonly Subject<AssetChange> changes = new();
    private readonly CompositeDisposable catalogSubscriptions = new();
    private readonly SemaphoreSlim lockObj = new(1, 1);
    private bool isDisposed;
    private bool isInitialized;

    public ProjectAssetCatalog(IProjectManagerService projectManager)
    {
        this.projectManager = projectManager;
    }

    public IObservable<AssetChange> Changes => this.changes.AsObservable();

    public async Task InitializeAsync()
    {
        if (this.isInitialized)
        {
            return;
        }

        this.isInitialized = true;

        if (this.projectManager.CurrentProject is not { } project)
        {
            return;
        }

        var storage = this.projectManager.GetCurrentProjectStorageProvider();

        // Index project root
        if (!string.IsNullOrEmpty(project.ProjectInfo.Location))
        {
            var rootOptions = new FileSystemAssetCatalogOptions
            {
                RootFolderPath = project.ProjectInfo.Location,
                MountPoint = "project",
            };
            var rootCatalog = new FileSystemAssetCatalog(storage, rootOptions);
            await this.AddCatalogAsync(rootCatalog).ConfigureAwait(false);
        }

        // Index mount points
        foreach (var mount in project.ProjectInfo.AuthoringMounts)
        {
            try
            {
                var mountRootLocation = storage.NormalizeRelativeTo(project.ProjectInfo.Location!, mount.RelativePath);

                var mountOptions = new FileSystemAssetCatalogOptions
                {
                    RootFolderPath = mountRootLocation,
                    MountPoint = mount.Name,
                };
                var mountCatalog = new FileSystemAssetCatalog(storage, mountOptions);
                await this.AddCatalogAsync(mountCatalog).ConfigureAwait(false);
            }
            catch (Exception)
            {
                // Log error?
            }
        }
    }

    public async Task<IReadOnlyList<AssetRecord>> QueryAsync(AssetQuery query, CancellationToken cancellationToken = default)
    {
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
        var storage = this.projectManager.GetCurrentProjectStorageProvider();

        var options = new FileSystemAssetCatalogOptions
        {
            RootFolderPath = folder.Location,
            MountPoint = mountPoint,
        };

        var catalog = new FileSystemAssetCatalog(storage, options);
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
