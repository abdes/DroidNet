// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;
using System.Reactive.Subjects;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.Assets.Runtime;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Assets;

/// <summary>
/// The asset feed over the asset catalog, cook status, open documents and the preview's acknowledged state.
/// </summary>
public sealed partial class AssetFeed : IAssetFeed, IDisposable, IAsyncDisposable
{
    private readonly IAssetCatalogReader catalog;
    private readonly IProjectContextService projectContextService;
    private readonly IProjectCookScopeProvider projectCookScopeProvider;
    private readonly AssetIdentityReducer reducer = new();
    private readonly IAssetCookStatusReader cookStatus;
    private readonly ICookDocumentRegistry documents;
    private readonly ICookRunService cooks;
    private readonly IAssetRuntimeStatusSource runtimeStatus;
    private readonly BehaviorSubject<IReadOnlyList<AssetEntry>> items = new([]);
    private readonly IDisposable changesSubscription;
    private readonly IDisposable projectSubscription;
    private readonly ILogger<AssetFeed> logger;
    private readonly CancellationTokenSource lifetime = new();
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="AssetFeed"/> class.</summary>
    /// <param name="catalog">The asset catalog.</param>
    /// <param name="projectContextService">The active project lifetime.</param>
    /// <param name="projectCookScopeProvider">The published mount paths.</param>
    /// <param name="cookStatus">The cook-owned freshness reader.</param>
    /// <param name="documents">Live authoring-state notifications.</param>
    /// <param name="cooks">Cook progress and completion notifications.</param>
    /// <param name="runtimeStatus">The preview's acknowledged content and asset request outcomes.</param>
    /// <param name="logger">Background refresh diagnostics.</param>
    public AssetFeed(
        AssetCatalog catalog,
        IProjectContextService projectContextService,
        IProjectCookScopeProvider projectCookScopeProvider,
        IAssetCookStatusReader cookStatus,
        ICookDocumentRegistry documents,
        ICookRunService cooks,
        IAssetRuntimeStatusSource runtimeStatus,
        ILogger<AssetFeed>? logger = null)
        : this((IAssetCatalogReader)catalog, projectContextService, projectCookScopeProvider, cookStatus, documents, cooks, runtimeStatus, logger)
    {
    }

    /// <summary>Initializes a new instance of the <see cref="AssetFeed"/> class over any reader of catalog records.</summary>
    /// <param name="catalog">The catalog records and their publication.</param>
    /// <param name="projectContextService">The active project lifetime.</param>
    /// <param name="projectCookScopeProvider">The published mount paths.</param>
    /// <param name="cookStatus">The cook-owned freshness reader.</param>
    /// <param name="documents">Live authoring-state notifications.</param>
    /// <param name="cooks">Cook progress and completion notifications.</param>
    /// <param name="runtimeStatus">The preview's acknowledged content and asset request outcomes.</param>
    /// <param name="logger">Background refresh diagnostics.</param>
    internal AssetFeed(
        IAssetCatalogReader catalog,
        IProjectContextService projectContextService,
        IProjectCookScopeProvider projectCookScopeProvider,
        IAssetCookStatusReader cookStatus,
        ICookDocumentRegistry documents,
        ICookRunService cooks,
        IAssetRuntimeStatusSource runtimeStatus,
        ILogger<AssetFeed>? logger = null)
    {
        this.catalog = catalog;
        this.projectContextService = projectContextService;
        this.projectCookScopeProvider = projectCookScopeProvider;
        this.cookStatus = cookStatus;
        this.documents = documents;
        this.cooks = cooks;
        this.runtimeStatus = runtimeStatus;
        this.logger = logger ?? NullLogger<AssetFeed>.Instance;
        this.changesSubscription = this.catalog.Changes
            .Subscribe(_ => this.OnCatalogChanged());
        this.projectSubscription = this.projectContextService.ProjectChanged.Skip(1)
            .Subscribe(_ => this.OnProjectChanged());
        this.documents.StateChanged += this.OnDocumentStateChanged;
        this.cooks.RunChanged += this.OnCookRunChanged;
        this.cookStatus.Changed += this.OnFreshnessChanged;
        this.runtimeStatus.Changed += this.OnRuntimeStatusChanged;
    }

    /// <inheritdoc />
    public IObservable<IReadOnlyList<AssetEntry>> Items => this.items.AsObservable();

    /// <inheritdoc />
    public Task RefreshAsync(CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        return this.RequestRefresh(invalidate: false).WaitAsync(cancellationToken);
    }

    /// <inheritdoc />
    public async Task<AssetEntry?> ResolveAsync(Uri uri, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(uri);
        CancellationTokenSource cancellation;
        lock (this.refreshSync)
        {
            ObjectDisposedException.ThrowIf(this.disposed, this);
            cancellation = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken, this.lifetime.Token);
        }

        using var cancellationLifetime = cancellation;
        await this.RefreshAsync(cancellation.Token).ConfigureAwait(false);
        ProjectContext project;
        lock (this.refreshSync)
        {
            ObjectDisposedException.ThrowIf(this.disposed, this);
            if (this.projectContextService.ActiveProject is not { } active)
            {
                return AssetEntry.Missing(uri);
            }

            project = active;
            var logicalKey = GetLogicalKey(uri);
            var match = this.items.Value.FirstOrDefault(item => item.IdentityUri == uri)
                ?? this.items.Value.FirstOrDefault(item => item.CookedUri == uri)
                ?? this.items.Value.FirstOrDefault(item => string.Equals(GetLogicalKey(item.IdentityUri), logicalKey, StringComparison.OrdinalIgnoreCase));
            if (match is not null)
            {
                return match;
            }
        }

        if (TryResolveSourcePath(project, uri) is not { } sourcePath || !File.Exists(sourcePath))
        {
            return AssetEntry.Missing(uri);
        }

        AssetRecord[] matches = [new(uri)];
        var cookScope = this.projectCookScopeProvider.CreateScope(project);
        var reduced = this.reducer.Reduce(matches, project, cookScope, includeMissing: true);
        var enriched = await this.ApplyCookStatusAsync(project, reduced, cancellation.Token).ConfigureAwait(false);
        lock (this.refreshSync)
        {
            return !this.disposed && ReferenceEquals(project, this.projectContextService.ActiveProject) && enriched.Count != 0 ? this.ApplyLiveState(enriched)[0] : null;
        }
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
        this.cookStatus.Changed -= this.OnFreshnessChanged;
        this.runtimeStatus.Changed -= this.OnRuntimeStatusChanged;
    }

    /// <summary>Cancels discovery and waits until its retained publication readers have drained.</summary>
    /// <returns>Completion after background refresh releases its resources.</returns>
    public async ValueTask DisposeAsync()
    {
        this.Dispose();
        Task? pending;
        lock (this.refreshSync)
        {
            pending = this.refreshTask;
        }

        if (pending is not null)
        {
            await pending.ConfigureAwait(false);
        }

        GC.SuppressFinalize(this);
    }

    private static AssetEntry ApplyCookStatus(AssetEntry item, AssetCookStatus state)
        => item with
        {
            CookStatus = state,
            CookedPath = item.CookedMetadata is not null ? item.CookedPath : ResolvePublishedOutputPath(item, state),
            DiagnosticCodes = item.DiagnosticCodes.Where(code => !string.Equals(code, AssetIdentityDiagnosticCodes.CookedMissing, StringComparison.Ordinal)
                    || state.OutputAvailability == CookedOutputAvailability.Missing && state.HasPublishedOutput)
                .Concat(state.Diagnostics.Select(static diagnostic => diagnostic.Code)).Distinct(StringComparer.Ordinal).ToArray(),
            PrimaryState = state.Freshness == AssetCookFreshness.Unknown ? item.PrimaryState : state.Freshness switch
            {
                AssetCookFreshness.MissingSource => AssetState.Missing,
                AssetCookFreshness.InvalidSource => AssetState.Broken,
                _ => item.Kind == AssetKind.ForeignSource ? AssetState.Source
                    : item.ImportSourceUri is not null && item.DescriptorPath is null ? AssetState.Cooked : AssetState.Descriptor,
            },
            DerivedState = !state.HasPublishedOutput ? null
                : state.OutputAvailability == CookedOutputAvailability.Missing ? AssetState.Missing
                : state.OutputAvailability == CookedOutputAvailability.Unknown ? null
                : state.Freshness == AssetCookFreshness.Current && !state.HasUnsavedChanges ? AssetState.Cooked : AssetState.Stale,
            IsSelectable = state.Freshness == AssetCookFreshness.Unknown ? item.IsSelectable : state.Freshness is not (AssetCookFreshness.InvalidSource or AssetCookFreshness.MissingSource),
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

    private async Task<IReadOnlyList<AssetEntry>> ApplyCookStatusAsync(ProjectContext project, IReadOnlyList<AssetEntry> source, CancellationToken cancellationToken,
        Oxygen.Editor.ContentPipeline.Publication.ICookPublicationReadLease? publication = null, string? publicationError = null)
    {
        source = source.Select(item => item.Kind == AssetKind.ForeignSource && item.SourcePath is { } path
            && File.Exists(path + Oxygen.Editor.ContentPipeline.Import.NativeSceneImportSettings.SidecarSuffix)
                ? item with { ImportSourceUri = item.IdentityUri, ImportSourcePath = path } : item).ToArray();
        var candidates = source.Where(item => item.Generated is not null || item.ImportSourceUri is not null
            || (item.DescriptorPath is not null && item.Kind is AssetKind.Material or AssetKind.Geometry or AssetKind.Scene or AssetKind.Texture)
            || IsProjectCookedOutput(publication?.ProjectRoots.Values ?? [], item)).ToArray();
        if (candidates.Length == 0)
        {
            return source;
        }

        var identities = candidates.Select(static item => item.IdentityUri).ToArray();
        if (publicationError is not null)
        {
            var diagnostic = new DiagnosticRecord
            {
                OperationId = Guid.Empty,
                Domain = FailureDomain.AssetCook,
                Severity = DiagnosticSeverity.Error,
                Code = "Cook.PublicationUnavailable",
                Message = publicationError,
            };
            var affected = candidates.Select(static item => item.IdentityUri).ToHashSet();
            return source.Select(item => affected.Contains(item.IdentityUri)
                ? ApplyCookStatus(item, new(item.IdentityUri, AssetCookFreshness.Unknown, false, CookedOutputAvailability.Unknown, [], [], [diagnostic]))
                : item).ToArray();
        }

        var read = publication is null ? this.cookStatus.ReadAsync(project, identities, cancellationToken)
            : this.cookStatus.ReadAsync(project, publication, identities, cancellationToken);
        var states = (await read.ConfigureAwait(false)).ToDictionary(static state => state.AssetUri);
        var origins = source.Where(item => item.Generated is not null && states.TryGetValue(item.IdentityUri, out var state) && state.HasAvailableOutput)
            .SelectMany(item => states[item.IdentityUri].Outputs.Where(output => states[item.IdentityUri].OutputRoots.ContainsKey(output.MountName))
                .Select(output => new VerifiedBuiltinSource(states[item.IdentityUri].OutputRoots[output.MountName], output.CookedAssetUri, item)))
            .GroupBy(static entry => entry.CookedUri)
            .Where(static group => group.Select(entry => entry.Origin.IdentityUri).Distinct().Take(2).Count() == 1)
            .ToDictionary(static group => group.Key, static group => group.ToArray());
        var projectedItems = source.Select(item =>
        {
            if (item.IsBuiltin)
            {
                return item;
            }

            var proofs = origins.Values.SelectMany(static entries => entries).Where(proof =>
                item.CookedUri == proof.CookedUri || string.Equals(item.CookedMetadata?.VirtualPath, proof.CookedUri.AbsolutePath, StringComparison.Ordinal)
                    || item.OverriddenCookedSources.Any(metadata => string.Equals(metadata.VirtualPath, proof.CookedUri.AbsolutePath, StringComparison.Ordinal))).ToArray();
            var projected = item with { VerifiedBuiltinSources = proofs };
            var origin = proofs.FirstOrDefault(proof => proof.Origin.Kind == item.Kind
                && (item.CookedMetadata is { } metadata ? string.Equals(Path.GetFullPath(metadata.RootFolderPath), proof.Root, StringComparison.OrdinalIgnoreCase)
                    && (metadata.VirtualPath is null || string.Equals(metadata.VirtualPath, proof.CookedUri.AbsolutePath, StringComparison.Ordinal))
                    : item.CookedPath is { } path && Path.GetFullPath(path).StartsWith(proof.Root + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase)))?.Origin;
            return item.SourcePath is null && item.DescriptorPath is null && origin is not null
                ? projected with { Generated = origin.Generated, BuiltinOriginUri = origin.IdentityUri, DisplayName = origin.DisplayName, PrimaryState = AssetState.Generated, DerivedState = null }
                : states.TryGetValue(item.IdentityUri, out var state) ? ApplyOwnedCookStatus(project, projected, state) : projected;
        }).ToArray();
        return AddKnownImportedOutputs(project, projectedItems, states.Values);
    }
}
