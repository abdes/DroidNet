// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;
using System.Reactive.Subjects;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.Assets.Runtime;
using Oxygen.Editor.ContentPipeline.Authoring;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Assets;

/// <summary>
/// The asset feed over the asset catalog, cook status, open documents and the preview's acknowledged state.
/// </summary>
/// <remarks>
/// Files the editor itself moves, copies, deletes or creates are read again as soon as their service reports them,
/// whoever asked for the change; other file changes arrive through the catalog's file watching.
/// </remarks>
public sealed partial class AssetFeed : IAssetFeed, IDisposable, IAsyncDisposable
{
    private readonly IAssetCatalogReader catalog;
    private readonly IProjectContextService projectContextService;
    private readonly IProjectCookScopeProvider projectCookScopeProvider;
    private readonly AssetIdentityReducer reducer = new();
    private readonly IAssetCookStatusReader cookStatus;
    private readonly IUnsavedDocuments documents;
    private readonly ICookRunService cooks;
    private readonly IAssetRelocationService relocation;
    private readonly IAssetAuthoring authoring;
    private readonly IAssetRuntimeStatusSource runtimeStatus;
    private readonly BehaviorSubject<IReadOnlyList<AssetEntry>> items = new([]);
    private readonly ILogger<AssetFeed> logger;
    private readonly CancellationTokenSource lifetime = new();
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="AssetFeed"/> class.</summary>
    /// <param name="catalog">The asset catalog.</param>
    /// <param name="projectContextService">The active project lifetime.</param>
    /// <param name="projectCookScopeProvider">The published mount paths.</param>
    /// <param name="cookStatus">The cook-owned freshness reader.</param>
    /// <param name="documents">The open documents with unsaved changes.</param>
    /// <param name="cooks">Cook progress and completion notifications.</param>
    /// <param name="runtimeStatus">The preview's acknowledged content and asset request outcomes.</param>
    /// <param name="relocation">Reports the files that relocations, copies and deletes changed.</param>
    /// <param name="authoring">Reports the scripts and input files it created.</param>
    /// <param name="logger">Background refresh diagnostics.</param>
    public AssetFeed(
        AssetCatalog catalog,
        IProjectContextService projectContextService,
        IProjectCookScopeProvider projectCookScopeProvider,
        IAssetCookStatusReader cookStatus,
        IUnsavedDocuments documents,
        ICookRunService cooks,
        IAssetRuntimeStatusSource runtimeStatus,
        IAssetRelocationService relocation,
        IAssetAuthoring authoring,
        ILogger<AssetFeed>? logger = null)
        : this((IAssetCatalogReader)catalog, projectContextService, projectCookScopeProvider, cookStatus, documents, cooks, runtimeStatus, relocation, authoring, logger)
    {
    }

    /// <summary>Initializes a new instance of the <see cref="AssetFeed"/> class over any reader of catalog records.</summary>
    /// <param name="catalog">The catalog records and their publication.</param>
    /// <param name="projectContextService">The active project lifetime.</param>
    /// <param name="projectCookScopeProvider">The published mount paths.</param>
    /// <param name="cookStatus">The cook-owned freshness reader.</param>
    /// <param name="documents">The open documents with unsaved changes.</param>
    /// <param name="cooks">Cook progress and completion notifications.</param>
    /// <param name="runtimeStatus">The preview's acknowledged content and asset request outcomes.</param>
    /// <param name="relocation">Reports the files that relocations, copies and deletes changed.</param>
    /// <param name="authoring">Reports the scripts and input files it created.</param>
    /// <param name="logger">Background refresh diagnostics.</param>
    internal AssetFeed(
        IAssetCatalogReader catalog,
        IProjectContextService projectContextService,
        IProjectCookScopeProvider projectCookScopeProvider,
        IAssetCookStatusReader cookStatus,
        IUnsavedDocuments documents,
        ICookRunService cooks,
        IAssetRuntimeStatusSource runtimeStatus,
        IAssetRelocationService relocation,
        IAssetAuthoring authoring,
        ILogger<AssetFeed>? logger = null)
    {
        this.catalog = catalog;
        this.projectContextService = projectContextService;
        this.projectCookScopeProvider = projectCookScopeProvider;
        this.cookStatus = cookStatus;
        this.documents = documents;
        this.cooks = cooks;
        this.runtimeStatus = runtimeStatus;
        this.relocation = relocation;
        this.authoring = authoring;
        this.logger = logger ?? NullLogger<AssetFeed>.Instance;
        this.catalog.Changes.Subscribe(_ => this.OnCatalogChanged(), this.lifetime.Token);
        this.projectContextService.ProjectChanged.Skip(1).Subscribe(_ => this.OnProjectChanged(), this.lifetime.Token);
        this.documents.Changed += this.OnUnsavedDocumentsChanged;
        this.cooks.RunChanged += this.OnCookRunChanged;
        this.cookStatus.Changed += this.OnFreshnessChanged;
        this.runtimeStatus.Changed += this.OnRuntimeStatusChanged;
        this.relocation.FilesChanged += this.OnEditorFilesChanged;
        this.authoring.FilesChanged += this.OnEditorFilesChanged;
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
    public async Task<AssetEntry?> ResolveAsync(AssetPath identity, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(identity);
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
                return AssetEntry.Missing(identity);
            }

            project = active;

            // The engine publishes its content changes asynchronously: the row takes the preview's state as it is now.
            if (this.items.Value.Find(identity) is { } match)
            {
                return this.ApplyLiveState([match])[0];
            }
        }

        if (project.FindSource(identity) is not { MountKind: ProjectMountKind.Authoring, IsMountRoot: false } source)
        {
            return AssetEntry.Missing(identity);
        }

        AssetRecord[] matches = [new(source.Path)];
        var cookScope = this.projectCookScopeProvider.CreateScope(project);
        var reduced = this.reducer.Reduce(matches, project, cookScope, includeMissing: true);
        var enriched = await this.ApplyCookStatusAsync(project, reduced, cancellation.Token).ConfigureAwait(false);
        lock (this.refreshSync)
        {
            return !this.disposed && ReferenceEquals(project, this.projectContextService.ActiveProject) && enriched.Count != 0 ? this.ApplyLiveState(enriched)[0] : null;
        }
    }

    /// <inheritdoc />
    public void Dispose() => this.Close();

    /// <summary>Cancels discovery and waits until its retained publication readers have drained.</summary>
    /// <returns>Completion after background refresh releases its resources.</returns>
    public async ValueTask DisposeAsync()
    {
        this.Close();
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
            DiagnosticCodes = [.. item.DiagnosticCodes.Where(code => !string.Equals(code, AssetIdentityDiagnosticCodes.CookedMissing, StringComparison.Ordinal)
                    || (state.OutputAvailability == CookedOutputAvailability.Missing && state.HasPublishedOutput))
                .Concat(state.Diagnostics.Select(static diagnostic => diagnostic.Code)).Distinct(StringComparer.Ordinal)],
            PrimaryState = state.Freshness == AssetCookFreshness.Unknown ? item.PrimaryState : state.Freshness switch
            {
                AssetCookFreshness.MissingSource => AssetState.Missing,
                AssetCookFreshness.InvalidSource => AssetState.Broken,
                _ => item.Kind == AssetKind.ForeignSource ? AssetState.Source
                    : item.ImportSource is not null && item.DescriptorPath is null ? AssetState.Cooked : AssetState.Descriptor,
            },
            DerivedState = !state.HasPublishedOutput ? null
                : state.OutputAvailability == CookedOutputAvailability.Missing ? AssetState.Missing
                : state.OutputAvailability == CookedOutputAvailability.Unknown ? null
                : state.Freshness == AssetCookFreshness.Current && !state.HasUnsavedChanges ? AssetState.Cooked : AssetState.Stale,
            IsSelectable = state.Freshness == AssetCookFreshness.Unknown ? item.IsSelectable : state.Freshness is not (AssetCookFreshness.InvalidSource or AssetCookFreshness.MissingSource),
        };

    // Only the files of authoring mounts are project sources.
    private static FileSystemPath? TryResolveSourcePath(ProjectContext project, AssetPath path)
        => project.Find(path) is { MountKind: ProjectMountKind.Authoring, IsMountRoot: false } location ? location.File : null;

    // Cancelling the lifetime also ends the catalog and project subscriptions.
    private void Close()
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

        this.documents.Changed -= this.OnUnsavedDocumentsChanged;
        this.cooks.RunChanged -= this.OnCookRunChanged;
        this.cookStatus.Changed -= this.OnFreshnessChanged;
        this.runtimeStatus.Changed -= this.OnRuntimeStatusChanged;
        this.relocation.FilesChanged -= this.OnEditorFilesChanged;
        this.authoring.FilesChanged -= this.OnEditorFilesChanged;
    }

    private async Task<IReadOnlyList<AssetEntry>> ApplyCookStatusAsync(
        ProjectContext project,
        IReadOnlyList<AssetEntry> source,
        CancellationToken cancellationToken,
        Oxygen.Editor.ContentPipeline.Publication.ICookPublicationReadLease? publication = null,
        string? publicationError = null)
    {
        source = [.. source.Select(item => item.Kind == AssetKind.ForeignSource && item.SourcePath is { } path
            && project.FileSystem.File.Exists(path.FullPath + Oxygen.Editor.ContentPipeline.Import.NativeSceneImportSettings.SidecarSuffix)
                ? item with { ImportSource = item.Identity, ImportSourcePath = path } : item)];
        var candidates = source.Where(item => item.ImportSource is not null
            || (item.DescriptorPath is not null && item.Kind is AssetKind.Material or AssetKind.Geometry or AssetKind.Scene or AssetKind.Texture)
            || IsProjectCookedOutput(publication?.ProjectRoots.Values ?? [], item)).ToArray();
        if (candidates.Length == 0)
        {
            return source;
        }

        var identities = candidates.Select(static item => item.Identity).ToArray();
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
            var affected = candidates.Select(static item => item.Identity).ToHashSet();
            return [.. source.Select(item => affected.Contains(item.Identity)
                ? ApplyCookStatus(item, new(item.Identity, AssetCookFreshness.Unknown, HasPublishedOutput: false, CookedOutputAvailability.Unknown, [], [diagnostic]))
                : item)];
        }

        var read = publication is null ? this.cookStatus.ReadAsync(project, identities, cancellationToken)
            : this.cookStatus.ReadAsync(project, publication, identities, cancellationToken);
        var states = (await read.ConfigureAwait(false)).ToDictionary(static state => state.AssetUri);
        var projectedItems = source.Select(item => states.TryGetValue(item.Identity, out var state) ? ApplyOwnedCookStatus(project, item, state) : item).ToArray();
        return AddKnownImportedOutputs(project, projectedItems, states.Values);
    }
}
