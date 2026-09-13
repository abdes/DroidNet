// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Reactive.Linq;
using System.Reactive.Subjects;
using DroidNet.Storage;
using Oxygen.Editor.ContentPipeline.Discovery;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Assets.Catalog.FileSystem;
using Oxygen.Managed.Assets.Catalog.LooseCooked;

namespace Oxygen.Editor.ContentBrowser.Infrastructure.Assets;

/// <summary>Publishes the project catalog after all initial indexes are ready.</summary>
public sealed partial class ProjectAssetCatalog : IProjectAssetCatalog, IDisposable
{
    private readonly IProjectContextService projectContextService;
    private readonly IStorageProvider storage;
    private readonly IBuiltinCatalogDiscovery builtins;
    private readonly Lock stateLock = new();
    private readonly Lock notificationLock = new();
    private readonly List<Registration> catalogs = [];
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
    public ProjectAssetCatalog(IProjectContextService projectContextService, IStorageProvider storage, IBuiltinCatalogDiscovery builtins)
    {
        this.projectContextService = projectContextService;
        this.storage = storage;
        this.builtins = builtins;
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
            completion = new(TaskCreationOptions.RunContinuationsAsynchronously);
            this.initialization = completion.Task;
        }

        _ = this.InitializeCoreAsync(project, completion);
        return completion.Task;
    }

    /// <inheritdoc />
    public async Task<IReadOnlyList<AssetRecord>> QueryAsync(AssetQuery query, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(query);
        using var operation = this.CreateOperationCancellation(cancellationToken);
        operation.Token.ThrowIfCancellationRequested();
        var project = this.projectContextService.ActiveProject;
        if (project is null)
        {
            return [];
        }

        await this.InitializeAsync().WaitAsync(operation.Token).ConfigureAwait(false);
        var results = await Task.WhenAll(this.SnapshotCatalogs().Select(catalog => catalog.QueryAsync(new(AssetQueryScope.All), operation.Token))).ConfigureAwait(false);
        operation.Token.ThrowIfCancellationRequested();
        return ReferenceEquals(project, this.projectContextService.ActiveProject)
            ? CookedAssetResolution.Resolve(OrderCookedRecords(project, results.SelectMany(static records => records)), query)
            : throw new OperationCanceledException("The project changed during catalog discovery.");
    }

    /// <inheritdoc />
    public async Task RefreshAsync(CancellationToken cancellationToken = default)
    {
        using var operation = this.CreateOperationCancellation(cancellationToken);
        operation.Token.ThrowIfCancellationRequested();
        await this.InitializeAsync().WaitAsync(operation.Token).ConfigureAwait(false);
        foreach (var catalog in this.SnapshotCatalogs().OfType<IRefreshableAssetCatalog>())
        {
            await catalog.RefreshAsync(operation.Token).ConfigureAwait(false);
        }

        operation.Token.ThrowIfCancellationRequested();
    }

    /// <inheritdoc />
    [SuppressMessage("Reliability", "CA2000:Dispose objects before losing scope", Justification = "PrepareCatalogAsync transfers catalog ownership to Registration; failed preparation or installation disposes it, and installed registrations are disposed with the project.")]
    public async Task AddFolderAsync(IFolder folder, string mountPoint)
    {
        ArgumentNullException.ThrowIfNull(folder);
        using var operation = this.CreateOperationCancellation(CancellationToken.None);
        await this.InitializeAsync().WaitAsync(operation.Token).ConfigureAwait(false);
        var candidate = await this.PrepareFolderAsync(folder.Location, mountPoint).ConfigureAwait(false);
        try
        {
            this.InstallCatalogs([candidate]);
        }
        catch when (!candidate.Published)
        {
            candidate.Dispose();
            throw;
        }
    }

    /// <inheritdoc />
    public void Dispose()
    {
        Registration[] snapshot;
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
            pending = this.initialization;
        }

        this.lifetime.Cancel();
        foreach (var catalog in snapshot)
        {
            catalog.Dispose();
        }

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

    private static IEnumerable<AssetRecord> OrderCookedRecords(ProjectContext project, IEnumerable<AssetRecord> records)
    {
        var order = CookedContentOrdering.Resolve(project.LocalFolderMounts, project.CookedContentOrder);
        var projectPriority = order.Select((source, index) => (source, index)).Single(static item => item.source.Kind == CookedContentSourceKind.ProjectOutput).index;
        var libraryPriority = order.Select((source, index) => (source, index)).Where(static item => item.source.Kind == CookedContentSourceKind.LocalFolder)
            .GroupBy(item => Path.GetFullPath(project.LocalFolderMounts.Single(mount => string.Equals(mount.Name, item.source.Name, StringComparison.OrdinalIgnoreCase)).AbsolutePath), StringComparer.OrdinalIgnoreCase)
            .ToDictionary(static group => group.Key, static group => group.Max(static item => item.index), StringComparer.OrdinalIgnoreCase);
        return records.OrderBy(record => record.Cooked is { } cooked ? libraryPriority.GetValueOrDefault(Path.GetFullPath(cooked.RootFolderPath), projectPriority) : -1)
            .ThenBy(static record => record.Cooked?.RootFolderPath, StringComparer.Ordinal);
    }

    private static bool IsDerivedRootMount(ProjectMountPoint mount)
    {
        var path = mount.RelativePath.Trim().Replace('\\', '/').Trim('/');
        return path.Equals(".cooked", StringComparison.OrdinalIgnoreCase)
            || path.Equals(".imported", StringComparison.OrdinalIgnoreCase)
            || path.Equals(".build", StringComparison.OrdinalIgnoreCase);
    }

    [SuppressMessage("Reliability", "CA2000:Dispose objects before losing scope", Justification = "Prepared registrations transfer to the project catalog or are disposed in the initialization finally block.")]
    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Every initialization failure must complete the shared promise so callers can observe it and retry; observer failures after publication do not roll back an installed catalog.")]
    private async Task InitializeCoreAsync(ProjectContext project, TaskCompletionSource completion)
    {
        var candidates = new List<Registration>();
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

            await this.PrepareMountsAsync(project, candidates).ConfigureAwait(false);
            this.InstallCatalogs(candidates, completion, project);
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
        }
    }

    [SuppressMessage("Reliability", "CA2000:Dispose objects before losing scope", Justification = "PrepareCatalogAsync owns each child catalog through Registration and disposes it on preparation failure.")]
    private async Task PrepareMountsAsync(ProjectContext project, List<Registration> candidates)
    {
        var sharedCookedRoot = this.storage.NormalizeRelativeTo(project.ProjectRoot, ".cooked");
        var hasSharedIndex = await this.storage.DocumentExistsAsync(Path.Combine(sharedCookedRoot, "container.index.bin")).ConfigureAwait(false);
        foreach (var mount in project.AuthoringMounts.Where(static mount => !IsDerivedRootMount(mount)))
        {
            this.lifetimeToken.ThrowIfCancellationRequested();
            try
            {
                var root = this.storage.NormalizeRelativeTo(project.ProjectRoot, mount.RelativePath);
                candidates.Add(await this.PrepareCatalogAsync(new FileSystemAssetCatalog(
                    this.storage,
                    new FileSystemAssetCatalogOptions { RootFolderPath = root, MountPoint = mount.Name })).ConfigureAwait(false));
                if (!hasSharedIndex)
                {
                    var cookedRoot = this.storage.NormalizeRelativeTo(project.ProjectRoot, $".cooked/{mount.Name}");
                    candidates.Add(await this.PrepareCatalogAsync(new LooseCookedIndexAssetCatalog(
                        this.storage,
                        new LooseCookedIndexAssetCatalogOptions { CookedRootFolderPath = cookedRoot })).ConfigureAwait(false));
                }
            }
            catch (Exception exception) when (exception is not OperationCanceledException)
            {
                Debug.WriteLine($"[ProjectAssetCatalog] Could not index mount '{mount.Name}': {exception}");
            }
        }

        if (hasSharedIndex)
        {
            candidates.Add(await this.PrepareCatalogAsync(new LooseCookedIndexAssetCatalog(
                this.storage,
                new LooseCookedIndexAssetCatalogOptions { CookedRootFolderPath = sharedCookedRoot })).ConfigureAwait(false));
        }

        foreach (var mount in project.LocalFolderMounts)
        {
            candidates.Add(await this.PrepareFolderAsync(mount.AbsolutePath, mount.Name).ConfigureAwait(false));
        }
    }

    [SuppressMessage("Reliability", "CA2000:Dispose objects before losing scope", Justification = "PrepareCatalogAsync transfers child catalog ownership to Registration and disposes it on preparation failure.")]
    private async Task<Registration> PrepareFolderAsync(string root, string mountPoint)
        => await this.storage.DocumentExistsAsync(Path.Combine(root, "container.index.bin")).ConfigureAwait(false)
            ? await this.PrepareCatalogAsync(new LooseCookedIndexAssetCatalog(
                this.storage,
                new LooseCookedIndexAssetCatalogOptions { CookedRootFolderPath = root })).ConfigureAwait(false)
            : await this.PrepareCatalogAsync(new FileSystemAssetCatalog(
                this.storage,
                new FileSystemAssetCatalogOptions { RootFolderPath = root, MountPoint = mountPoint })).ConfigureAwait(false);

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

    private void InstallCatalogs(List<Registration> candidates, TaskCompletionSource? completion = null, ProjectContext? project = null)
    {
        lock (this.notificationLock)
        {
            var notifications = this.CommitCatalogs(candidates, completion, project, out var retired);
            candidates.Clear();
            foreach (var registration in retired)
            {
                registration.Dispose();
            }

            this.PublishChanges(notifications);
        }
    }

    private List<AssetChange> CommitCatalogs(IReadOnlyList<Registration> candidates, TaskCompletionSource? completion, ProjectContext? project, out Registration[] retired)
    {
        var notifications = new List<AssetChange>();
        lock (this.stateLock)
        {
            this.lifetimeToken.ThrowIfCancellationRequested();
            if (completion is not null && (!ReferenceEquals(project, this.projectContextService.ActiveProject) || !ReferenceEquals(this.initialization, completion.Task)))
            {
                throw new OperationCanceledException("A newer project configuration superseded catalog initialization.");
            }

            retired = completion is null ? [] : this.catalogs.ToArray();
            if (completion is not null)
            {
                this.catalogs.Clear();
            }

            foreach (var candidate in candidates)
            {
                this.catalogs.Add(candidate);
                candidate.Published = true;
                notifications.AddRange(candidate.Initial.Select(static record => new AssetChange(AssetChangeKind.Added, record.Uri)));
                candidate.Initial = [];
                notifications.AddRange(candidate.Pending);
                candidate.Pending.Clear();
            }

            _ = completion?.TrySetResult();
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

    private IAssetCatalog[] SnapshotCatalogs()
    {
        lock (this.stateLock)
        {
            ObjectDisposedException.ThrowIf(this.isDisposed, this);
            return this.catalogs.Select(static entry => entry.Catalog).ToArray();
        }
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
