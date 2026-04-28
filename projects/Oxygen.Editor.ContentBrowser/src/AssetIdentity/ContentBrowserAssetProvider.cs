// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;
using System.Reactive.Subjects;
using Oxygen.Assets.Catalog;
using Oxygen.Editor.ContentBrowser.Infrastructure.Assets;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentBrowser.AssetIdentity;

/// <summary>
/// Shared ED-M06 browser asset provider over the composed project catalog.
/// </summary>
public sealed class ContentBrowserAssetProvider : IContentBrowserAssetProvider, IDisposable
{
    private readonly IProjectAssetCatalog projectAssetCatalog;
    private readonly IProjectContextService projectContextService;
    private readonly IProjectCookScopeProvider projectCookScopeProvider;
    private readonly IAssetIdentityReducer reducer;
    private readonly BehaviorSubject<IReadOnlyList<ContentBrowserAssetItem>> items = new([]);
    private readonly IDisposable changesSubscription;
    private bool disposed;

    public ContentBrowserAssetProvider(
        IProjectAssetCatalog projectAssetCatalog,
        IProjectContextService projectContextService,
        IProjectCookScopeProvider projectCookScopeProvider,
        IAssetIdentityReducer reducer)
    {
        this.projectAssetCatalog = projectAssetCatalog;
        this.projectContextService = projectContextService;
        this.projectCookScopeProvider = projectCookScopeProvider;
        this.reducer = reducer;
        this.changesSubscription = this.projectAssetCatalog.Changes
            .Subscribe(change => { _ = this.RefreshAsync(AssetBrowserFilter.Default); });
    }

    /// <inheritdoc />
    public IObservable<IReadOnlyList<ContentBrowserAssetItem>> Items => this.items.AsObservable();

    /// <inheritdoc />
    public async Task RefreshAsync(AssetBrowserFilter filter, CancellationToken cancellationToken = default)
    {
        _ = filter;
        cancellationToken.ThrowIfCancellationRequested();
        if (this.projectContextService.ActiveProject is not { } project)
        {
            this.items.OnNext([]);
            return;
        }

        var query = new AssetQuery(AssetQueryScope.All);
        await this.projectAssetCatalog.RefreshAsync(cancellationToken).ConfigureAwait(false);
        var records = await this.projectAssetCatalog.QueryAsync(query, cancellationToken).ConfigureAwait(false);
        var cookScope = this.projectCookScopeProvider.CreateScope(project);
        var reduced = await Task.Run(
                () => this.reducer.Reduce(records, project, cookScope, AssetBrowserFilter.Default),
                cancellationToken)
            .ConfigureAwait(false);
        this.items.OnNext(reduced);
    }

    /// <inheritdoc />
    public async Task<ContentBrowserAssetItem?> ResolveAsync(Uri uri, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(uri);
        cancellationToken.ThrowIfCancellationRequested();
        if (this.projectContextService.ActiveProject is not { } project)
        {
            return this.reducer.CreateMissing(uri);
        }

        await this.projectAssetCatalog.RefreshAsync(cancellationToken).ConfigureAwait(false);
        var records = await this.projectAssetCatalog
            .QueryAsync(new AssetQuery(AssetQueryScope.All), cancellationToken)
            .ConfigureAwait(false);
        var logicalKey = GetLogicalKey(uri);
        var matches = records.Where(record => string.Equals(GetLogicalKey(record.Uri), logicalKey, StringComparison.OrdinalIgnoreCase)).ToList();
        if (matches.Count == 0)
        {
            if (TryResolveSourcePath(project, uri) is { } sourcePath && File.Exists(sourcePath))
            {
                matches.Add(new AssetRecord(uri));
            }
            else
            {
                return this.reducer.CreateMissing(uri);
            }
        }

        var cookScope = this.projectCookScopeProvider.CreateScope(project);
        return this.reducer
            .Reduce(matches, project, cookScope, AssetBrowserFilter.Default with { IncludeMissing = true, IncludeBroken = true })
            .FirstOrDefault();
    }

    /// <inheritdoc />
    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        this.disposed = true;
        this.changesSubscription.Dispose();
        this.items.Dispose();
    }

    private static string GetLogicalKey(Uri uri)
    {
        var path = AssetUriHelper.GetVirtualPath(uri);
        if (string.IsNullOrWhiteSpace(path))
        {
            path = uri.AbsolutePath;
        }

        return path.EndsWith(".json", StringComparison.OrdinalIgnoreCase)
            ? path[..^".json".Length]
            : path;
    }

    private static string? TryResolveSourcePath(ProjectContext project, Uri uri)
    {
        var relative = Uri.UnescapeDataString(uri.AbsolutePath).TrimStart('/');
        var slash = relative.IndexOf('/', StringComparison.Ordinal);
        if (slash <= 0)
        {
            return null;
        }

        var mountName = relative[..slash];
        var mountRelativePath = relative[(slash + 1)..];
        var mount = project.AuthoringMounts.FirstOrDefault(m => string.Equals(m.Name, mountName, StringComparison.OrdinalIgnoreCase));
        if (mount is null)
        {
            return null;
        }

        return Path.GetFullPath(Path.Combine(project.ProjectRoot, mount.RelativePath, mountRelativePath));
    }
}
