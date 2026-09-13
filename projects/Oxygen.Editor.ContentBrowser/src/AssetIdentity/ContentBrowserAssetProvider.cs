// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;
using System.Reactive.Subjects;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentBrowser.Infrastructure.Assets;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentBrowser.AssetIdentity;

/// <summary>
/// Shared ED-M06 browser asset provider over the composed project catalog.
/// </summary>
public sealed partial class ContentBrowserAssetProvider : IContentBrowserAssetProvider, IDisposable
{
    private readonly IProjectAssetCatalog projectAssetCatalog;
    private readonly IProjectContextService projectContextService;
    private readonly IProjectCookScopeProvider projectCookScopeProvider;
    private readonly IAssetIdentityReducer reducer;
    private readonly IAssetCookStatusReader cookStatus;
    private readonly ICookDocumentRegistry documents;
    private readonly ICookRunService cooks;
    private readonly BehaviorSubject<IReadOnlyList<ContentBrowserAssetItem>> items = new([]);
    private readonly IDisposable changesSubscription;
    private readonly IDisposable projectSubscription;
    private readonly ILogger<ContentBrowserAssetProvider> logger;
    private readonly CancellationTokenSource lifetime = new();
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="ContentBrowserAssetProvider"/> class.</summary>
    /// <param name="projectAssetCatalog">The composed asset identities.</param>
    /// <param name="projectContextService">The active project lifetime.</param>
    /// <param name="projectCookScopeProvider">The published mount paths.</param>
    /// <param name="reducer">The source and output identity reducer.</param>
    /// <param name="cookStatus">The cook-owned freshness reader.</param>
    /// <param name="documents">Live authoring-state notifications.</param>
    /// <param name="cooks">Cook progress and completion notifications.</param>
    /// <param name="logger">Background refresh diagnostics.</param>
    public ContentBrowserAssetProvider(
        IProjectAssetCatalog projectAssetCatalog,
        IProjectContextService projectContextService,
        IProjectCookScopeProvider projectCookScopeProvider,
        IAssetIdentityReducer reducer,
        IAssetCookStatusReader cookStatus,
        ICookDocumentRegistry documents,
        ICookRunService cooks,
        ILogger<ContentBrowserAssetProvider>? logger = null)
    {
        this.projectAssetCatalog = projectAssetCatalog;
        this.projectContextService = projectContextService;
        this.projectCookScopeProvider = projectCookScopeProvider;
        this.reducer = reducer;
        this.cookStatus = cookStatus;
        this.documents = documents;
        this.cooks = cooks;
        this.logger = logger ?? NullLogger<ContentBrowserAssetProvider>.Instance;
        this.changesSubscription = this.projectAssetCatalog.Changes
            .Subscribe(_ => this.OnCatalogChanged());
        this.projectSubscription = this.projectContextService.ProjectChanged.Skip(1)
            .Subscribe(_ => this.OnProjectChanged());
        this.documents.StateChanged += this.OnDocumentStateChanged;
        this.cooks.RunChanged += this.OnCookRunChanged;
    }

    /// <inheritdoc />
    public IObservable<IReadOnlyList<ContentBrowserAssetItem>> Items => this.items.AsObservable();

    /// <inheritdoc />
    public Task RefreshAsync(AssetBrowserFilter filter, CancellationToken cancellationToken = default)
    {
        _ = filter;
        cancellationToken.ThrowIfCancellationRequested();
        return this.RequestRefresh(invalidate: false).WaitAsync(cancellationToken);
    }

    /// <inheritdoc />
    public async Task<ContentBrowserAssetItem?> ResolveAsync(Uri uri, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(uri);
        CancellationTokenSource cancellation;
        lock (this.refreshSync)
        {
            ObjectDisposedException.ThrowIf(this.disposed, this);
            cancellation = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken, this.lifetime.Token);
        }

        using var cancellationLifetime = cancellation;
        await this.RefreshAsync(AssetBrowserFilter.Default, cancellation.Token).ConfigureAwait(false);
        ProjectContext project;
        lock (this.refreshSync)
        {
            ObjectDisposedException.ThrowIf(this.disposed, this);
            if (this.projectContextService.ActiveProject is not { } active)
            {
                return this.reducer.CreateMissing(uri);
            }

            project = active;
            var logicalKey = GetLogicalKey(uri);
            var match = this.items.Value.FirstOrDefault(item => string.Equals(GetLogicalKey(item.IdentityUri), logicalKey, StringComparison.OrdinalIgnoreCase));
            if (match is not null)
            {
                return match;
            }
        }

        if (TryResolveSourcePath(project, uri) is not { } sourcePath || !File.Exists(sourcePath))
        {
            return this.reducer.CreateMissing(uri);
        }

        AssetRecord[] matches = [new(uri)];
        var cookScope = this.projectCookScopeProvider.CreateScope(project);
        var reduced = this.reducer.Reduce(matches, project, cookScope, AssetBrowserFilter.Default with { IncludeMissing = true, IncludeBroken = true });
        var enriched = await this.ApplyCookStatusAsync(project, reduced, cancellation.Token).ConfigureAwait(false);
        return !this.disposed && ReferenceEquals(project, this.projectContextService.ActiveProject) && enriched.Count != 0 ? enriched[0] : null;
    }

    /// <inheritdoc />
    public void Dispose()
    {
        lock (this.refreshSync)
        {
            if (this.disposed)
            {
                return;
            }

            this.disposed = true;
            this.scanCancellation?.Cancel();
            this.lifetime.Cancel();
            this.lifetime.Dispose();
            _ = this.refreshCompletion?.TrySetCanceled(new CancellationToken(canceled: true));
            this.items.Dispose();
        }

        this.changesSubscription.Dispose();
        this.projectSubscription.Dispose();
        this.documents.StateChanged -= this.OnDocumentStateChanged;
        this.cooks.RunChanged -= this.OnCookRunChanged;
    }

    private static ContentBrowserAssetItem ApplyCookStatus(ContentBrowserAssetItem item, AssetCookStatus state)
        => item with
        {
            CookStatus = state,
            DiagnosticCodes = item.DiagnosticCodes.Where(code => state.HasPublishedOutput || !string.Equals(code, AssetIdentityDiagnosticCodes.CookedMissing, StringComparison.Ordinal))
                .Concat(state.Diagnostics.Select(static diagnostic => diagnostic.Code)).Distinct(StringComparer.Ordinal).ToArray(),
            PrimaryState = state.Freshness switch
            {
                AssetCookFreshness.MissingSource => AssetState.Missing,
                AssetCookFreshness.InvalidSource => AssetState.Broken,
                _ => AssetState.Descriptor,
            },
            DerivedState = !state.HasPublishedOutput ? null
                : !state.HasVerifiedOutput ? AssetState.Broken
                : state.Freshness == AssetCookFreshness.Current && !state.HasUnsavedChanges ? AssetState.Cooked : AssetState.Stale,
            IsSelectable = state.Freshness is not (AssetCookFreshness.InvalidSource or AssetCookFreshness.MissingSource),
        };

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
        return mount is null ? null : Path.GetFullPath(Path.Combine(project.ProjectRoot, mount.RelativePath, mountRelativePath));
    }

    private async Task<IReadOnlyList<ContentBrowserAssetItem>> ApplyCookStatusAsync(ProjectContext project, IReadOnlyList<ContentBrowserAssetItem> source, CancellationToken cancellationToken)
    {
        var candidates = source.Where(static item => item.DescriptorPath is not null && item.Kind is AssetKind.Material or AssetKind.Geometry or AssetKind.Scene).ToArray();
        if (candidates.Length == 0)
        {
            return source;
        }

        var states = (await this.cookStatus.ReadAsync(project, candidates.Select(static item => item.IdentityUri).ToArray(), cancellationToken).ConfigureAwait(false))
            .ToDictionary(static state => state.AssetUri);
        return source.Select(item => states.TryGetValue(item.IdentityUri, out var state) ? ApplyCookStatus(item, state) : item).ToArray();
    }
}
