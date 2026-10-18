// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Reactive.Linq;
using System.Reactive.Subjects;
using System.Text.Json;
using DroidNet.Storage;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Discovery;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Assets.Catalog.FileSystem;

namespace Oxygen.Editor.Assets;

/// <summary>
/// The editor's asset catalog: every asset the active project can see, from the engine's built-ins, the project and
/// its authoring mounts, library folders that have no cooked content, and the cooked roots of one cook publication.
/// </summary>
/// <remarks>
/// The catalog reads the project's current publication itself, or shows the publication an owner of publication
/// acceptance hands it through <see cref="UsePublicationAsync"/>. Its records change when files change or when a
/// different publication is shown. Records that appear because the catalog starts listing them, when a project opens
/// or a folder or publication is added, are reported as listed, not added: the files did not change. Showing another
/// publication keeps watching the project's authored folders, reports the records that appeared or disappeared, and
/// reports every cooked record that stays as updated, since the publication may have changed it; an authored file that
/// did not change reports nothing.
/// </remarks>
public sealed partial class AssetCatalog : IAssetCatalog, IAssetFolderSource, IAssetPublicationTarget, IAssetCatalogReader, IDisposable, IAsyncDisposable
{
    private readonly IProjectContextService projectContextService;
    private readonly IStorageProvider storage;
    private readonly IBuiltinCatalogDiscovery builtins;
    private readonly ICookPublicationReader publication;
    private readonly ILogger logger;
    private Guid? initializedPublicationId;
    private ICookPublicationReadLease? installedPublication;
    private string? publicationError;
    private Guid? initializingPublicationId;
    private bool initializingWithSelection;
    private long catalogRevision;
    private long refreshSequence;
    private string? initializingPublicationError;
    private readonly Lock stateLock = new();
    private readonly Lock notificationLock = new();
    private readonly List<Registration> catalogs = [];
    private readonly HashSet<Uri> visible = [];
    private Guid? visibleProjectId;
    private readonly HashSet<Task> initializationRuns = [];
    private readonly Subject<AssetChange> changes = new();
    private readonly Subject<Uri> folderChanges = new();
    private readonly CancellationTokenSource lifetime = new();
    private readonly CancellationToken lifetimeToken;
    private Task? initialization;
    private ProjectContext? initializedProject;
    private volatile bool isDisposed;

    /// <summary>Initializes a new instance of the <see cref="AssetCatalog"/> class.</summary>
    /// <param name="projectContextService">The active project context.</param>
    /// <param name="storage">The storage provider for indexed folders.</param>
    /// <param name="builtins">The engine-owned discovery catalog.</param>
    /// <param name="publication">Reads the project's publication and the catalogs of its cooked roots.</param>
    /// <param name="logger">Records failures of the catalog's observers.</param>
    public AssetCatalog(IProjectContextService projectContextService, IStorageProvider storage, IBuiltinCatalogDiscovery builtins, ICookPublicationReader publication,
        ILogger<AssetCatalog>? logger = null)
    {
        this.projectContextService = projectContextService;
        this.storage = storage;
        this.builtins = builtins;
        this.publication = publication;
        this.logger = (ILogger?)logger ?? NullLogger.Instance;
        this.lifetimeToken = this.lifetime.Token;
    }

    /// <inheritdoc />
    public IObservable<AssetChange> Changes => this.changes.AsObservable();

    /// <inheritdoc />
    /// <remarks>Folders of the installed project folder catalogs; the folders of a catalog being replaced are not reported.</remarks>
    public IObservable<Uri> FolderChanges => this.folderChanges.AsObservable();

    /// <inheritdoc />
    public async Task<IReadOnlyList<AssetRecord>> QueryAsync(AssetQuery query, CancellationToken cancellationToken = default)
    {
        using var snapshot = await this.ReadSnapshotAsync(query, cancellationToken).ConfigureAwait(false);
        return snapshot.Records;
    }

    /// <summary>Brings the catalog up to date with the project's current publication and files.</summary>
    /// <param name="cancellationToken">Cancels the refresh.</param>
    /// <returns>Completion after the catalog is current.</returns>
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

        ICookPublicationReadLease selected;
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
    public Task UsePublicationAsync(ICookPublicationReadLease publication, CancellationToken cancellationToken = default)
        => this.RefreshSelectedAsync(publication, cancellationToken, observation: null);

    /// <inheritdoc />
    Task<AssetCatalogSnapshot> IAssetCatalogReader.ReadSnapshotAsync(AssetQuery query, CancellationToken cancellationToken)
        => this.ReadSnapshotAsync(query, cancellationToken);

    /// <summary>Reads the active project once; later calls share that reading until the project changes.</summary>
    /// <returns>Completion after the catalog lists the project's assets.</returns>
    internal Task InitializeAsync()
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

    /// <summary>Reads the catalog's records together with the publication they were read from.</summary>
    /// <param name="query">The records to read.</param>
    /// <param name="cancellationToken">Cancels the read.</param>
    /// <returns>A snapshot, disposed after the reads that depend on its publication finish.</returns>
    internal async Task<AssetCatalogSnapshot> ReadSnapshotAsync(AssetQuery query, CancellationToken cancellationToken = default)
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

    private async Task RefreshSelectedAsync(ICookPublicationReadLease selected, CancellationToken cancellationToken, long? observation)
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
        ICookPublicationReadLease? retained = null;
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
                // A publication handed over by its owner is shown exactly: the owner may hold the project writer, so
                // it must not be published again here. A publication the catalog observed itself is read again
                // through the reader, which publishes the project again when a library changed after it.
                retained = observation is null ? selected.Retain() : null;
                completion = this.StartInitialization(project, retained);
                initialization = completion.Task;
                this.initializedProject = project;
                this.initializingWithSelection = retained is not null;
                this.initializingPublicationId = retained?.PublicationId;
                this.initializingPublicationError = null;
            }
        }

        await initialization.WaitAsync(operation.Token).ConfigureAwait(false);
        lock (this.stateLock)
        {
            if (!ReferenceEquals(this.initialization, initialization) || !ReferenceEquals(project, this.projectContextService.ActiveProject))
            {
                // An observation only asks for the current publication; a newer installed one satisfies it.
                if (observation is not null && ReferenceEquals(project, this.projectContextService.ActiveProject))
                {
                    return;
                }

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
        ICookPublicationReadLease? publication;
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
            this.folderChanges.OnCompleted();
            this.folderChanges.Dispose();
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
    private TaskCompletionSource StartInitialization(ProjectContext project, ICookPublicationReadLease? supplied, string? publicationError = null)
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

    private static void Follow(TaskCompletionSource completion, Task successor)
        => _ = successor.ContinueWith(
            task =>
            {
                if (task.IsCompletedSuccessfully)
                {
                    _ = completion.TrySetResult();
                }
                else if (task.IsCanceled)
                {
                    _ = completion.TrySetCanceled();
                }
                else
                {
                    _ = completion.TrySetException(task.Exception!.InnerExceptions);
                }
            },
            CancellationToken.None,
            TaskContinuationOptions.ExecuteSynchronously,
            TaskScheduler.Default);

    private static bool IsPublicationUnavailable(Exception failure)
        => failure is IOException or InvalidDataException or UnauthorizedAccessException or JsonException;

    [SuppressMessage("Reliability", "CA2000:Dispose objects before losing scope", Justification = "Prepared registrations transfer to the catalog or are disposed in the initialization finally block.")]
    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Every initialization failure must complete the shared promise so callers can observe it and retry; observer failures after publication do not roll back an installed catalog.")]
    private async Task InitializeCoreAsync(ProjectContext project, TaskCompletionSource completion, ICookPublicationReadLease? supplied, string? publicationError = null)
    {
        var candidates = new List<Registration>();
        var kept = new Dictionary<Registration, IReadOnlyList<AssetRecord>>();
        var selected = supplied;
        IReadOnlyList<IAssetCatalog> cooked = [];
        var cookedPrepared = 0;
        try
        {
            this.lifetimeToken.ThrowIfCancellationRequested();
            if (selected is not null)
            {
                cooked = await selected.CreateCatalogsAsync(this.lifetimeToken).ConfigureAwait(false);
            }
            else if (publicationError is null)
            {
                try
                {
                    var opened = await this.publication.AcquireCatalogsAsync(project, this.lifetimeToken).ConfigureAwait(false);
                    (selected, cooked) = (opened.Publication, opened.Catalogs);
                }
                catch (Exception failure) when (IsPublicationUnavailable(failure))
                {
                    // Project activation reports recovery failures. Keep authored assets accessible
                    // while the publication owner blocks admission of unresolved cooked output.
                    publicationError = failure.Message;
                }
            }

            await this.PrepareAuthoredCatalogsAsync(project, selected, candidates, kept).ConfigureAwait(false);
            for (; cookedPrepared < cooked.Count; cookedPrepared++)
            {
                candidates.Add(await this.PrepareCatalogAsync(cooked[cookedPrepared], isCooked: true).ConfigureAwait(false));
            }

            this.InstallCatalogs(candidates, kept, completion, project, selected, publicationError);
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
                this.LogObserverFailed(exception);
                return;
            }

            Task? successor = null;
            lock (this.stateLock)
            {
                if (ReferenceEquals(this.initialization, completion.Task))
                {
                    this.initialization = null;
                }
                else if (ReferenceEquals(this.initializedProject, project))
                {
                    successor = this.initialization;
                }
            }

            // A newer selection for the same project replaced this run; its callers observe that catalog instead.
            if (exception is OperationCanceledException && successor is not null)
            {
                Follow(completion, successor);
                return;
            }

            _ = completion.TrySetException(exception);
            return;
        }
        finally
        {
            // A kept registration still belongs to the installed catalogs.
            foreach (var candidate in candidates.Where(candidate => !kept.ContainsKey(candidate)))
            {
                candidate.Dispose();
            }

            // The cooked catalog whose preparation failed was disposed by its registration; later ones were never prepared.
            for (var index = cookedPrepared + 1; index < cooked.Count; index++)
            {
                (cooked[index] as IDisposable)?.Dispose();
            }

            selected?.Dispose();
        }
    }

    [SuppressMessage("Reliability", "CA2000:Dispose objects before losing scope", Justification = "PrepareCatalogAsync owns each child catalog through Registration and disposes it on preparation failure.")]
    private async Task PrepareAuthoredCatalogsAsync(ProjectContext project, ICookPublicationReadLease? selected, List<Registration> candidates, Dictionary<Registration, IReadOnlyList<AssetRecord>> kept)
    {
        candidates.Add(await this.PrepareCatalogAsync(new BuiltinAssetCatalog(this.builtins)).ConfigureAwait(false));
        if (!string.IsNullOrEmpty(project.ProjectRoot))
        {
            candidates.Add(await this.PrepareFolderAsync(project, "project", project.ProjectRoot, kept, GetFoldersOfOtherCatalogs(project)).ConfigureAwait(false));
        }

        foreach (var mount in project.AuthoringMounts.Where(static mount => !ProjectOutputFolders.Contains(mount)))
        {
            this.lifetimeToken.ThrowIfCancellationRequested();
            var root = this.storage.NormalizeRelativeTo(project.ProjectRoot, mount.RelativePath);
            candidates.Add(await this.PrepareFolderAsync(project, mount.Name, root, kept).ConfigureAwait(false));
        }

        // Preserve authored-folder browsing for external folders without cooked content.
        foreach (var mount in project.LocalFolderMounts.Where(mount => selected?.Roots.Any(root =>
            root.Owner == CookPublicationRootOwner.Library && string.Equals(root.Name, mount.Name, StringComparison.OrdinalIgnoreCase)) != true))
        {
            candidates.Add(await this.PrepareFolderAsync(project, mount.Name, mount.AbsolutePath, kept).ConfigureAwait(false));
        }
    }

    // Each file belongs to one catalog. The project folder's catalog lists the files outside the mounts, so it leaves
    // out every mount folder inside the project: the authoring mounts and the local folders mounted from inside it.
    private static string[] GetFoldersOfOtherCatalogs(ProjectContext project)
        => [.. project.AuthoringMounts.Select(static mount => mount.RelativePath)
            .Concat(project.LocalFolderMounts.Select(mount => Path.GetRelativePath(project.ProjectRoot, mount.AbsolutePath)))
            .Select(static path => path.Replace('/', Path.DirectorySeparatorChar).Trim(Path.DirectorySeparatorChar))
            .Where(static path => path.Length != 0 && path != "." && path != ".." && !path.StartsWith(".." + Path.DirectorySeparatorChar, StringComparison.Ordinal) && !Path.IsPathRooted(path))
            .Distinct(StringComparer.OrdinalIgnoreCase)];

    // A folder the installed catalogs already watch for this project keeps its catalog and watcher, so no change made
    // while another publication is installed goes unseen.
    private async Task<Registration> PrepareFolderAsync(ProjectContext project, string mountPoint, string root, Dictionary<Registration, IReadOnlyList<AssetRecord>> kept, IReadOnlyList<string>? excluded = null)
    {
        var folder = new WatchedFolder(project.ProjectId, mountPoint, root, string.Join('|', excluded ?? []));
        Registration? installed;
        lock (this.stateLock)
        {
            installed = this.catalogs.FirstOrDefault(registration => folder.Equals(registration.Folder));
        }

        if (installed is not null)
        {
            kept[installed] = await installed.Catalog.QueryAsync(new AssetQuery(AssetQueryScope.All), this.lifetimeToken).ConfigureAwait(false);
            return installed;
        }

        var registration = await this.PrepareCatalogAsync(new FileSystemAssetCatalog(
            this.storage,
            new FileSystemAssetCatalogOptions { RootFolderPath = root, MountPoint = mountPoint, ExcludedFolders = excluded ?? [] },
            this.logger)).ConfigureAwait(false);
        registration.Folder = folder;
        return registration;
    }

    private async Task<Registration> PrepareCatalogAsync(IAssetCatalog catalog, bool isCooked = false)
    {
        var registration = new Registration(catalog, isCooked);
        try
        {
            registration.Subscription = catalog.Changes.Subscribe(change => this.OnCatalogChange(registration, change));
            registration.FolderSubscription = (catalog as IAssetFolderSource)?.FolderChanges.Subscribe(folder => this.OnFolderChange(registration, folder));
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

    private void InstallCatalogs(List<Registration> candidates, Dictionary<Registration, IReadOnlyList<AssetRecord>> kept, TaskCompletionSource completion, ProjectContext project, ICookPublicationReadLease? publication, string? publicationError)
    {
        lock (this.notificationLock)
        {
            var notifications = this.CommitCatalogs(candidates, kept, completion, project, publication, publicationError, out var retired, out var retiredPublication);
            candidates.Clear();
            foreach (var registration in retired)
            {
                registration.Dispose();
            }

            retiredPublication?.Dispose();

            this.PublishChanges(notifications);
        }
    }

    private List<AssetChange> CommitCatalogs(IReadOnlyList<Registration> candidates, IReadOnlyDictionary<Registration, IReadOnlyList<AssetRecord>> kept, TaskCompletionSource completion, ProjectContext project,
        ICookPublicationReadLease? publication, string? publicationError, out Registration[] retired, out ICookPublicationReadLease? retiredPublication)
    {
        var notifications = new List<AssetChange>();
        lock (this.stateLock)
        {
            this.lifetimeToken.ThrowIfCancellationRequested();
            if (!ReferenceEquals(project, this.projectContextService.ActiveProject) || !ReferenceEquals(this.initialization, completion.Task))
            {
                throw new OperationCanceledException("A newer project configuration superseded catalog initialization.");
            }

            this.CollectInstallChanges(project, candidates, kept, notifications);

            retired = [.. this.catalogs.Where(registration => !kept.ContainsKey(registration))];
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
                this.LogChangeHeld(change.Kind, change.Uri);
                return;
            }

            if (!this.catalogs.Contains(registration))
            {
                this.LogChangeIgnored(change.Kind, change.Uri);
                return;
            }

            this.Track(change);
        }

        this.PublishChanges([change]);
    }

    // Called under stateLock while the candidates replace the installed catalogs.
    private void CollectInstallChanges(ProjectContext project, IReadOnlyList<Registration> candidates, IReadOnlyDictionary<Registration, IReadOnlyList<AssetRecord>> kept, List<AssetChange> notifications)
    {
        if (this.visibleProjectId != project.ProjectId)
        {
            // Another project's records are all gone, even where a path matches.
            notifications.AddRange(this.visible.Select(static uri => new AssetChange(AssetChangeKind.Removed, uri)));
            this.visible.Clear();
            this.visibleProjectId = project.ProjectId;
        }

        var installed = new HashSet<Uri>();
        foreach (var candidate in candidates)
        {
            foreach (var record in kept.TryGetValue(candidate, out var records) ? records : candidate.Initial)
            {
                if (!installed.Add(record.Uri))
                {
                    continue;
                }

                if (!this.visible.Contains(record.Uri))
                {
                    notifications.Add(new(AssetChangeKind.Listed, record.Uri));
                }
                else if (candidate.IsCooked)
                {
                    notifications.Add(new(AssetChangeKind.Updated, record.Uri));
                }
            }
        }

        notifications.AddRange(this.visible.Where(uri => !installed.Contains(uri)).Select(static uri => new AssetChange(AssetChangeKind.Removed, uri)));
        this.visible.Clear();
        this.visible.UnionWith(installed);
        var held = 0;
        foreach (var change in candidates.SelectMany(static candidate => candidate.Pending))
        {
            notifications.Add(change);
            this.Track(change);
            held++;
        }

        if (held != 0)
        {
            this.LogHeldChangesReported(held);
        }
    }

    // Called under stateLock.
    private void Track(AssetChange change)
    {
        switch (change.Kind)
        {
            case AssetChangeKind.Added or AssetChangeKind.Listed:
                _ = this.visible.Add(change.Uri);
                break;
            case AssetChangeKind.Removed:
                _ = this.visible.Remove(change.Uri);
                break;
            case AssetChangeKind.Relocated:
                if (change.PreviousUri is { } previous)
                {
                    _ = this.visible.Remove(previous);
                }

                _ = this.visible.Add(change.Uri);
                break;
            case AssetChangeKind.Updated:
                break;
        }
    }

    private void OnFolderChange(Registration registration, Uri folder)
    {
        lock (this.stateLock)
        {
            if (this.isDisposed)
            {
                return;
            }

            if (!registration.Published || !this.catalogs.Contains(registration))
            {
                this.LogFolderChangeDropped(folder);
                return;
            }
        }

        lock (this.notificationLock)
        {
            if (!this.isDisposed)
            {
                this.folderChanges.OnNext(folder);
            }
        }
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

    private sealed class CatalogOpening(long revision, IAssetCatalog[] catalogs, ICookPublicationReadLease? publication, string? publicationError) : IDisposable
    {
        internal string? PublicationError { get; } = publicationError;

        internal long Revision { get; } = revision;

        internal IAssetCatalog[] Catalogs { get; } = catalogs;

        internal ICookPublicationReadLease? Publication { get; } = publication;

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

    private sealed partial class Registration(IAssetCatalog catalog, bool isCooked) : IDisposable
    {
        public IAssetCatalog Catalog { get; } = catalog;

        public bool IsCooked { get; } = isCooked;

        public WatchedFolder? Folder { get; set; }

        public IDisposable? Subscription { get; set; }

        public IDisposable? FolderSubscription { get; set; }

        public IReadOnlyList<AssetRecord> Initial { get; set; } = [];

        public List<AssetChange> Pending { get; } = [];

        public bool Published { get; set; }

        public void Dispose()
        {
            this.Subscription?.Dispose();
            this.FolderSubscription?.Dispose();
            (this.Catalog as IDisposable)?.Dispose();
        }
    }

    // Excluded lists the folders the catalog leaves out, so a catalog whose exclusions changed is replaced.
    private sealed record WatchedFolder(Guid ProjectId, string MountPoint, string Root, string Excluded)
    {
        public bool Equals(WatchedFolder? other)
            => other is not null && this.ProjectId == other.ProjectId
                && string.Equals(this.MountPoint, other.MountPoint, StringComparison.OrdinalIgnoreCase)
                && string.Equals(this.Root, other.Root, StringComparison.OrdinalIgnoreCase)
                && string.Equals(this.Excluded, other.Excluded, StringComparison.OrdinalIgnoreCase);

        public override int GetHashCode()
            => HashCode.Combine(this.ProjectId, StringComparer.OrdinalIgnoreCase.GetHashCode(this.MountPoint), StringComparer.OrdinalIgnoreCase.GetHashCode(this.Root), StringComparer.OrdinalIgnoreCase.GetHashCode(this.Excluded));
    }
}
