// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Reactive.Linq;
using System.Reactive.Subjects;
using System.Text.Json;
using DroidNet.Storage;
using Oxygen.Editor.ContentPipeline.Discovery;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Assets.Catalog.FileSystem;
using Oxygen.Managed.Assets.Catalog.LooseCooked;

namespace Oxygen.Editor.ContentBrowser.Infrastructure.Assets;

/// <summary>Publishes the project catalog after all initial indexes are ready.</summary>
public sealed partial class ProjectAssetCatalog : IProjectAssetCatalog, IDisposable, IAsyncDisposable
{
    private readonly IProjectContextService projectContextService;
    private readonly IStorageProvider storage;
    private readonly IBuiltinCatalogDiscovery builtins;
    private readonly CookPublicationService publication;
    private Guid? initializedPublicationId;
    private CookPublicationReadLease? installedPublication;
    private string? publicationError;
    private Guid? initializingPublicationId;
    private bool initializingWithSelection;
    private long catalogRevision;
    private long refreshSequence;
    private string? initializingPublicationError;
    private readonly Lock stateLock = new();
    private readonly Lock notificationLock = new();
    private readonly List<Registration> catalogs = [];
    private readonly HashSet<Task> initializationRuns = [];
    private readonly Subject<AssetChange> changes = new();
    private readonly CancellationTokenSource lifetime = new();
    private readonly CancellationToken lifetimeToken;
    private Task? initialization;
    private ProjectContext? initializedProject;
    private volatile bool isDisposed;

    /// <summary>Initializes a new instance of the <see cref="ProjectAssetCatalog"/> class.</summary>
    /// <param name="projectContextService">The active project context.</param>
    /// <param name="storage">The storage provider for indexed folders.</param>
    /// <param name="builtins">The engine-owned discovery catalog.</param>
    public ProjectAssetCatalog(IProjectContextService projectContextService, IStorageProvider storage, IBuiltinCatalogDiscovery builtins, CookPublicationService publication)
    {
        this.projectContextService = projectContextService;
        this.storage = storage;
        this.builtins = builtins;
        this.publication = publication;
        this.lifetimeToken = this.lifetime.Token;
    }

    /// <inheritdoc />
    public IObservable<AssetChange> Changes => this.changes.AsObservable();

    /// <inheritdoc />
    public Task InitializeAsync()
    {
        ProjectContext project;
        TaskCompletionSource completion;
        lock (this.stateLock)
        {
            ObjectDisposedException.ThrowIf(this.isDisposed, this);
            if (this.projectContextService.ActiveProject is not { } active)
            {
                return Task.CompletedTask;
            }

            if (ReferenceEquals(this.initializedProject, active) && this.initialization is { } existing)
            {
                return existing;
            }

            project = active;
            this.initializedProject = active;
            completion = this.StartInitialization(project, supplied: null);
            this.initializingWithSelection = false;
            this.initializingPublicationError = null;
            this.refreshSequence++;
        }

        return completion.Task;
    }

    /// <inheritdoc />
    public async Task<IReadOnlyList<AssetRecord>> QueryAsync(AssetQuery query, CancellationToken cancellationToken = default)
    {
        using var snapshot = await this.ReadSnapshotAsync(query, cancellationToken).ConfigureAwait(false);
        return snapshot.Records;
    }

    /// <inheritdoc />
    public async Task<ProjectAssetSnapshot> ReadSnapshotAsync(AssetQuery query, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(query);
        using var operation = this.CreateOperationCancellation(cancellationToken);
        operation.Token.ThrowIfCancellationRequested();
        var project = this.projectContextService.ActiveProject;
        if (project is null)
        {
            return new([], publication: null);
        }

        await this.InitializeAsync().WaitAsync(operation.Token).ConfigureAwait(false);
        using var snapshot = this.AcquireCatalogs();
        var results = await Task.WhenAll(snapshot.Catalogs.Select(catalog => catalog.QueryAsync(new(AssetQueryScope.All), operation.Token))).ConfigureAwait(false);
        operation.Token.ThrowIfCancellationRequested();
        return this.IsCurrentSnapshot(project, snapshot.Revision)
            ? new(CookedAssetResolution.Resolve(results.SelectMany(static records => records), query), snapshot.Publication, snapshot.PublicationError)
            : throw new OperationCanceledException("The project changed during catalog discovery.");
    }

    /// <inheritdoc />
    public async Task RefreshAsync(CancellationToken cancellationToken = default)
    {
        var project = this.projectContextService.ActiveProject;
        if (project is null)
        {
            return;
        }

        long observation;
        lock (this.stateLock)
        {
            ObjectDisposedException.ThrowIf(this.isDisposed, this);
            observation = this.refreshSequence;
        }

        CookPublicationReadLease selected;
        try
        {
            selected = await this.publication.AcquireReadAsync(project, cancellationToken).ConfigureAwait(false);
        }
        catch (Exception failure) when (IsPublicationUnavailable(failure))
        {
            TaskCompletionSource? completion = null;
            Task fallback;
            lock (this.stateLock)
            {
                ObjectDisposedException.ThrowIf(this.isDisposed, this);
                if (this.refreshSequence != observation || !ReferenceEquals(project, this.projectContextService.ActiveProject))
                {
                    return;
                }

                if (ReferenceEquals(this.initializedProject, project) && this.initialization is { IsCanceled: false, IsFaulted: false } existing
                    && string.Equals(existing.IsCompletedSuccessfully ? this.publicationError : this.initializingPublicationError, failure.Message, StringComparison.Ordinal))
                {
                    fallback = existing;
                }
                else
                {
                    completion = this.StartInitialization(project, supplied: null, failure.Message);
                    fallback = completion.Task;
                    this.initializedProject = project;
                    this.initializingWithSelection = false;
                    this.initializingPublicationError = failure.Message;
                    this.refreshSequence++;
                }
            }

            await fallback.WaitAsync(cancellationToken).ConfigureAwait(false);
            return;
        }

        using (selected)
        {
            await this.RefreshSelectedAsync(selected, cancellationToken, observation).ConfigureAwait(false);
        }
    }

    /// <inheritdoc />
    public Task RefreshAsync(CookPublicationReadLease selected, CancellationToken cancellationToken = default)
        => this.RefreshSelectedAsync(selected, cancellationToken, observation: null);

    private async Task RefreshSelectedAsync(CookPublicationReadLease selected, CancellationToken cancellationToken, long? observation)
    {
        using var operation = this.CreateOperationCancellation(cancellationToken);
        var project = this.projectContextService.ActiveProject
            ?? throw new OperationCanceledException("The project closed before catalog refresh.");
        if (selected.ProjectId != project.ProjectId
            || !string.Equals(selected.ProjectRoot, project.ProjectRoot, StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidOperationException("The catalog publication belongs to another project.");
        }

        Task initialization;
        TaskCompletionSource? completion = null;
        CookPublicationReadLease? retained = null;
        var refreshExisting = false;
        lock (this.stateLock)
        {
            ObjectDisposedException.ThrowIf(this.isDisposed, this);
            if (observation is { } observed && this.refreshSequence != observed)
            {
                return;
            }

            this.refreshSequence++;
            if (ReferenceEquals(this.initializedProject, project) && this.initialization is { IsFaulted: false, IsCanceled: false } existing
                && (existing.IsCompletedSuccessfully ? this.installedPublication is not null && this.initializedPublicationId == selected.PublicationId
                    : this.initializingWithSelection && this.initializingPublicationId == selected.PublicationId))
            {
                initialization = existing;
                refreshExisting = existing.IsCompletedSuccessfully;
            }
            else
            {
                retained = selected.Retain();
                completion = this.StartInitialization(project, retained);
                initialization = completion.Task;
                this.initializedProject = project;
                this.initializingWithSelection = true;
                this.initializingPublicationId = selected.PublicationId;
                this.initializingPublicationError = null;
            }
        }

        await initialization.WaitAsync(operation.Token).ConfigureAwait(false);
        lock (this.stateLock)
        {
            if (!ReferenceEquals(this.initialization, initialization) || !ReferenceEquals(project, this.projectContextService.ActiveProject))
            {
                throw new OperationCanceledException("A newer publication superseded catalog initialization.");
            }
        }

        if (!refreshExisting)
        {
            return;
        }

        using var snapshot = this.AcquireCatalogs();
        foreach (var catalog in snapshot.Catalogs.OfType<IRefreshableAssetCatalog>())
        {
            await catalog.RefreshAsync(operation.Token).ConfigureAwait(false);
        }

        if (!this.IsCurrentSnapshot(project, snapshot.Revision))
        {
            throw new OperationCanceledException("A newer catalog replaced the refreshed snapshot.");
        }
    }

    /// <inheritdoc />
    public void Dispose()
    {
        Registration[] snapshot;
        CookPublicationReadLease? publication;
        Task? pending;
        lock (this.stateLock)
        {
            if (this.isDisposed)
            {
                return;
            }

            this.isDisposed = true;
            snapshot = this.catalogs.ToArray();
            this.catalogs.Clear();
            publication = this.installedPublication;
            this.installedPublication = null;
            pending = Task.WhenAll(this.initializationRuns);
        }

        this.lifetime.Cancel();
        foreach (var catalog in snapshot)
        {
            catalog.Dispose();
        }
        publication?.Dispose();

        lock (this.notificationLock)
        {
            this.changes.OnCompleted();
            this.changes.Dispose();
        }

        if (pending?.IsCompleted != false)
        {
            this.lifetime.Dispose();
        }
        else
        {
            _ = pending.ContinueWith(
                task =>
                {
                    _ = task.Exception;
                    this.lifetime.Dispose();
                },
                CancellationToken.None,
                TaskContinuationOptions.ExecuteSynchronously,
                TaskScheduler.Default);
        }
    }

    /// <summary>Cancels catalog work and drains every initialization, including superseded snapshots.</summary>
    /// <returns>Completion after initialization has released retained publications and child catalogs.</returns>
    public async ValueTask DisposeAsync()
    {
        this.Dispose();
        Task[] pending;
        lock (this.stateLock)
        {
            pending = this.initializationRuns.ToArray();
        }

        await Task.WhenAll(pending).ConfigureAwait(false);
        GC.SuppressFinalize(this);
    }

    // Called under stateLock so shutdown observes the run before it can finish or publish.
    private TaskCompletionSource StartInitialization(ProjectContext project, CookPublicationReadLease? supplied, string? publicationError = null)
    {
        var completion = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        this.initialization = completion.Task;
        var run = Task.Run(() => this.InitializeCoreAsync(project, completion, supplied, publicationError));
        this.initializationRuns.Add(run);
        _ = run.ContinueWith(task =>
        {
            _ = task.Exception;
            lock (this.stateLock)
            {
                this.initializationRuns.Remove(task);
            }
        }, CancellationToken.None, TaskContinuationOptions.ExecuteSynchronously, TaskScheduler.Default);
        return completion;
    }

    private static bool IsPublicationUnavailable(Exception failure)
        => failure is IOException or InvalidDataException or UnauthorizedAccessException or JsonException;

    private static bool IsDerivedRootMount(ProjectMountPoint mount)
    {
        var path = mount.RelativePath.Trim().Replace('\\', '/').Trim('/');
        return path.Equals(".cooked", StringComparison.OrdinalIgnoreCase)
            || path.Equals(".imported", StringComparison.OrdinalIgnoreCase)
            || path.Equals(".build", StringComparison.OrdinalIgnoreCase);
    }

    [SuppressMessage("Reliability", "CA2000:Dispose objects before losing scope", Justification = "Prepared registrations transfer to the project catalog or are disposed in the initialization finally block.")]
    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Every initialization failure must complete the shared promise so callers can observe it and retry; observer failures after publication do not roll back an installed catalog.")]
    private async Task InitializeCoreAsync(ProjectContext project, TaskCompletionSource completion, CookPublicationReadLease? supplied, string? publicationError = null)
    {
        var candidates = new List<Registration>();
        CookPublicationReadLease? selected = supplied;
        try
        {
            this.lifetimeToken.ThrowIfCancellationRequested();
            candidates.Add(await this.PrepareCatalogAsync(new EngineBuiltinAssetCatalog(this.builtins)).ConfigureAwait(false));
            if (!string.IsNullOrEmpty(project.ProjectRoot))
            {
                candidates.Add(await this.PrepareCatalogAsync(new FileSystemAssetCatalog(
                    this.storage,
                    new FileSystemAssetCatalogOptions { RootFolderPath = project.ProjectRoot, MountPoint = "project" })).ConfigureAwait(false));
            }

            if (selected is null && publicationError is null)
            {
                try
                {
                    selected = await this.publication.AcquireReadAsync(project, this.lifetimeToken).ConfigureAwait(false);
                }
                catch (Exception failure) when (IsPublicationUnavailable(failure))
                {
                    // Project activation reports recovery failures. Keep authored assets accessible
                    // while the publication owner blocks admission of unresolved cooked output.
                    publicationError = failure.Message;
                }
            }

            await this.PrepareMountsAsync(project, selected, candidates).ConfigureAwait(false);
            this.InstallCatalogs(candidates, completion, project, selected, publicationError);
        }
        catch (OperationCanceledException) when (this.lifetimeToken.IsCancellationRequested)
        {
            _ = completion.TrySetCanceled(this.lifetimeToken);
            return;
        }
        catch (Exception exception)
        {
            if (completion.Task.IsCompletedSuccessfully)
            {
                Debug.WriteLine($"[ProjectAssetCatalog] A catalog observer failed: {exception}");
                return;
            }

            lock (this.stateLock)
            {
                if (ReferenceEquals(this.initialization, completion.Task))
                {
                    this.initialization = null;
                }
            }

            _ = completion.TrySetException(exception);
            return;
        }
        finally
        {
            foreach (var candidate in candidates)
            {
                candidate.Dispose();
            }

            selected?.Dispose();
        }
    }

    [SuppressMessage("Reliability", "CA2000:Dispose objects before losing scope", Justification = "PrepareCatalogAsync owns each child catalog through Registration and disposes it on preparation failure.")]
    private async Task PrepareMountsAsync(ProjectContext project, CookPublicationReadLease? selected, List<Registration> candidates)
    {
        foreach (var mount in project.AuthoringMounts.Where(static mount => !IsDerivedRootMount(mount)))
        {
            this.lifetimeToken.ThrowIfCancellationRequested();
            var root = this.storage.NormalizeRelativeTo(project.ProjectRoot, mount.RelativePath);
            candidates.Add(await this.PrepareCatalogAsync(new FileSystemAssetCatalog(
                this.storage,
                new FileSystemAssetCatalogOptions { RootFolderPath = root, MountPoint = mount.Name })).ConfigureAwait(false));
        }

        // Preserve authored-folder browsing for external folders without cooked content.
        foreach (var mount in project.LocalFolderMounts.Where(mount => selected?.Roots.Any(root =>
            root.Owner == CookPublicationRootOwner.Library && string.Equals(root.Name, mount.Name, StringComparison.OrdinalIgnoreCase)) != true))
        {
            candidates.Add(await this.PrepareCatalogAsync(new FileSystemAssetCatalog(this.storage,
                new FileSystemAssetCatalogOptions { RootFolderPath = mount.AbsolutePath, MountPoint = mount.Name })).ConfigureAwait(false));
        }

        if (selected is null)
        {
            return;
        }

        for (var index = 0; index < selected.Roots.Length; index++)
        {
            if (selected.UnavailableRoots.Contains(selected.Roots[index]))
            {
                continue;
            }

            var candidate = await this.PrepareCatalogAsync(await selected.CreateCatalogAsync(selected.Roots[index], this.lifetimeToken).ConfigureAwait(false)).ConfigureAwait(false);
            try
            {
                candidates.Add(candidate);
            }
            catch
            {
                candidate.Dispose();
                throw;
            }
        }
    }

    private async Task<Registration> PrepareCatalogAsync(IAssetCatalog catalog)
    {
        var registration = new Registration(catalog);
        try
        {
            registration.Subscription = catalog.Changes.Subscribe(change => this.OnCatalogChange(registration, change));
            registration.Initial = await catalog.QueryAsync(new AssetQuery(AssetQueryScope.All), this.lifetimeToken).ConfigureAwait(false);
            this.lifetimeToken.ThrowIfCancellationRequested();
            return registration;
        }
        catch
        {
            registration.Dispose();
            throw;
        }
    }

    private void InstallCatalogs(List<Registration> candidates, TaskCompletionSource completion, ProjectContext project, CookPublicationReadLease? publication, string? publicationError)
    {
        lock (this.notificationLock)
        {
            var notifications = this.CommitCatalogs(candidates, completion, project, publication, publicationError, out var retired, out var retiredPublication);
            candidates.Clear();
            foreach (var registration in retired)
            {
                registration.Dispose();
            }
            retiredPublication?.Dispose();

            this.PublishChanges(notifications);
        }
    }

    private List<AssetChange> CommitCatalogs(IReadOnlyList<Registration> candidates, TaskCompletionSource completion, ProjectContext project,
        CookPublicationReadLease? publication, string? publicationError, out Registration[] retired, out CookPublicationReadLease? retiredPublication)
    {
        var notifications = new List<AssetChange>();
        lock (this.stateLock)
        {
            this.lifetimeToken.ThrowIfCancellationRequested();
            if (!ReferenceEquals(project, this.projectContextService.ActiveProject) || !ReferenceEquals(this.initialization, completion.Task))
            {
                throw new OperationCanceledException("A newer project configuration superseded catalog initialization.");
            }

            foreach (var candidate in candidates)
            {
                notifications.AddRange(candidate.Initial.Select(static record => new AssetChange(AssetChangeKind.Added, record.Uri)));
                notifications.AddRange(candidate.Pending);
            }

            retired = this.catalogs.ToArray();
            this.catalogs.EnsureCapacity(candidates.Count);
            var accepted = publication?.Retain();
            retiredPublication = this.installedPublication;
            this.installedPublication = accepted;
            this.publicationError = publicationError;
            this.initializedPublicationId = publication?.PublicationId;
            this.catalogs.Clear();
            foreach (var candidate in candidates)
            {
                this.catalogs.Add(candidate);
                candidate.Published = true;
                candidate.Initial = [];
                candidate.Pending.Clear();
            }

            this.catalogRevision++;
            _ = completion.TrySetResult();
        }

        return notifications;
    }

    private void OnCatalogChange(Registration registration, AssetChange change)
    {
        lock (this.stateLock)
        {
            if (this.isDisposed)
            {
                return;
            }

            if (!registration.Published)
            {
                registration.Pending.Add(change);
                return;
            }

            if (!this.catalogs.Contains(registration))
            {
                return;
            }
        }

        this.PublishChanges([change]);
    }

    private void PublishChanges(IReadOnlyList<AssetChange> notifications)
    {
        lock (this.notificationLock)
        {
            foreach (var notification in notifications)
            {
                if (this.isDisposed)
                {
                    return;
                }

                this.changes.OnNext(notification);
            }
        }
    }

    private CatalogOpening AcquireCatalogs()
    {
        lock (this.stateLock)
        {
            ObjectDisposedException.ThrowIf(this.isDisposed, this);
            var catalogs = this.catalogs.Select(static entry => entry.Catalog).ToArray();
            return new(this.catalogRevision, catalogs, this.installedPublication?.Retain(), this.publicationError);
        }
    }

    private bool IsCurrentSnapshot(ProjectContext project, long revision)
    {
        lock (this.stateLock)
        {
            return !this.isDisposed && this.catalogRevision == revision && ReferenceEquals(project, this.projectContextService.ActiveProject);
        }
    }

    private sealed class CatalogOpening(long revision, IAssetCatalog[] catalogs, CookPublicationReadLease? publication, string? publicationError) : IDisposable
    {
        internal string? PublicationError { get; } = publicationError;

        internal long Revision { get; } = revision;

        internal IAssetCatalog[] Catalogs { get; } = catalogs;

        internal CookPublicationReadLease? Publication { get; } = publication;

        public void Dispose() => this.Publication?.Dispose();
    }

    private CancellationTokenSource CreateOperationCancellation(CancellationToken cancellationToken)
    {
        lock (this.stateLock)
        {
            ObjectDisposedException.ThrowIf(this.isDisposed, this);
            return CancellationTokenSource.CreateLinkedTokenSource(cancellationToken, this.lifetimeToken);
        }
    }

    private sealed partial class Registration(IAssetCatalog catalog) : IDisposable
    {
        public IAssetCatalog Catalog { get; } = catalog;

        public IDisposable? Subscription { get; set; }


        public IReadOnlyList<AssetRecord> Initial { get; set; } = [];

        public List<AssetChange> Pending { get; } = [];

        public bool Published { get; set; }

        public void Dispose()
        {
            this.Subscription?.Dispose();
            (this.Catalog as IDisposable)?.Dispose();
        }
    }
}
