// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using DroidNet.Storage;
using DroidNet.TimeMachine;
using DroidNet.TimeMachine.Changes;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Relocation;

/// <summary>Runs reference-aware relocation, copy and delete as the project's content writer.</summary>
/// <param name="coordinator">Serializes project content changes with cooking and import.</param>
/// <param name="projects">The active project.</param>
/// <param name="documents">The open documents and their unsaved state.</param>
/// <param name="files">The atomic file store.</param>
/// <param name="catalog">The asset catalog; its changes make the cached references stale.</param>
/// <param name="logger">Records every move, rewrite, copy, delete, rejection and restore.</param>
/// <param name="redirects">The session's relocation record for undo, redo and paste; optional.</param>
public sealed partial class AssetRelocationService(IContentCookCoordinator coordinator, IProjectContextService projects, ICookDocumentRegistry documents, IAtomicFileStore files,
    IAssetCatalog catalog, ILogger<AssetRelocationService>? logger = null, AssetRedirects? redirects = null)
    : IAssetRelocationService, IDisposable
{
    private readonly ILogger logger = (ILogger?)logger ?? NullLogger.Instance;
    private readonly Lock sync = new();
    private readonly List<IAssetRelocationParticipant> participants = [];
    private Task<AuthoredReferenceIndex>? cached;
    private ProjectContext? cachedProject;
    private IDisposable? catalogChanges;

    /// <inheritdoc/>
    public event EventHandler<AssetFilesChangedEventArgs>? FilesChanged;

    /// <summary>Gets or sets a test hook that fails a relocation after this many applied steps.</summary>
    internal int? FailAfterStep { get; set; }

    /// <inheritdoc/>
    public void Dispose()
    {
        lock (this.sync)
        {
            this.catalogChanges?.Dispose();
            this.catalogChanges = null;
            this.cached = null;
        }
    }

    /// <inheritdoc/>
    public IDisposable AddParticipant(IAssetRelocationParticipant participant)
    {
        ArgumentNullException.ThrowIfNull(participant);
        lock (this.sync)
        {
            this.participants.Add(participant);
        }

        return new Registration(this, participant);
    }

    /// <inheritdoc/>
    public async Task<AssetRelocationPlan> RelocateAsync(AssetRelocationRequest request, string label, HistoryKeeper history, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(request);
        ArgumentException.ThrowIfNullOrWhiteSpace(label);
        ArgumentNullException.ThrowIfNull(history);
        var caller = SynchronizationContext.Current;

        // Resumes on the caller's context: the history belongs to it, and an undo running this relocation is still
        // undoing when the reverse is recorded.
        var plan = await this.RelocateCoreAsync(request, cancellationToken).ConfigureAwait(true);
        if (plan.IsEmpty)
        {
            return plan;
        }

        var reverse = plan.Reverse;
        history.AddChange(new AsyncSimpleAction(token => new ValueTask(this.RelocateAsync(reverse, label, history, token))) { Key = label });
        this.LogRecorded(label);
        this.RaiseFilesChanged(caller, new(plan.Moves, plan.RewrittenFiles, [], plan.Moves.Any(static move => move.IsDirectory)));
        return plan;
    }

    /// <inheritdoc/>
    public async Task<IReadOnlyList<AssetPath>> FindReferrersAsync(AssetPath virtualPath, CancellationToken cancellationToken)
    {
        var project = projects.ActiveProject;
        if (project is null)
        {
            return [];
        }

        Task<AuthoredReferenceIndex> index;
        lock (this.sync)
        {
            // Content changed outside a relocation (a save, an import, an external edit) makes the index stale.
            this.catalogChanges ??= catalog.Changes.Subscribe(_ => this.Invalidate());
            if (this.cached?.IsFaulted != false || this.cached.IsCanceled || !ReferenceEquals(this.cachedProject, project))
            {
                this.cachedProject = project;
                this.cached = Task.Run(() => AuthoredReferenceIndex.BuildAsync(project, CancellationToken.None));
            }

            index = this.cached;
        }

        return (await index.WaitAsync(cancellationToken).ConfigureAwait(false)).FindReferrers(virtualPath);
    }

    /// <inheritdoc/>
    public async Task<IReadOnlyList<AssetPath>> FindReferrersOutsideAsync(IReadOnlyList<AssetPath> sources, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(sources);
        this.Invalidate();
        var referrers = new SortedSet<AssetPath>();
        foreach (var source in sources)
        {
            referrers.UnionWith((await this.FindReferrersAsync(source, cancellationToken).ConfigureAwait(false))
                .Where(referrer => !sources.Any(referrer.IsSameOrInside)));
        }

        return [.. referrers];
    }

    /// <inheritdoc/>
    public IReadOnlyList<(string Name, bool IsDirty)> FindOpenDocuments(IReadOnlyList<AssetPath> sources)
    {
        ArgumentNullException.ThrowIfNull(sources);
        if (projects.ActiveProject is not { } project)
        {
            return [];
        }

        var deleted = sources.Select(source => RelocationPaths.ToPhysical(project, source)).OfType<FileSystemPath>().ToArray();
        return [.. documents.GetState().Documents
            .Where(document => deleted.Any(document.SourcePath.IsSameOrInside))
            .Select(static document => (document.DisplayName, document.IsDirty))
            .DistinctBy(static document => document.DisplayName, StringComparer.OrdinalIgnoreCase)
            .OrderBy(static document => document.DisplayName, StringComparer.OrdinalIgnoreCase)];
    }

    /// <inheritdoc/>
    public async Task<IReadOnlyList<AssetPath>> CopyAsync(IReadOnlyList<AssetPath> sources, AssetPath targetFolder, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(sources);
        ArgumentNullException.ThrowIfNull(targetFolder);
        var caller = SynchronizationContext.Current;
        RelocationFileMove[] created = [];
        var copies = await coordinator.RunAsync<IReadOnlyList<AssetPath>>(
            (operation, token) => this.LoggedAsync<IReadOnlyList<AssetPath>>("Copy", async () =>
            {
                var project = operation.Project;
                var index = await this.BuildIndexAsync(project, token).ConfigureAwait(false);
                if (RelocationPaths.ToPhysical(project, targetFolder) is not { } folder || !Directory.Exists(folder.FullPath))
                {
                    throw new AssetRelocationException("Choose a destination under Content or another authoring folder.");
                }

                var (map, copies) = await PlanCopiesAsync(project, index, sources, targetFolder, folder, token).ConfigureAwait(false);
                foreach (var move in map.Moves)
                {
                    var from = RelocationPaths.ToVirtual(project, move.Source)!;
                    var to = RelocationPaths.ToVirtual(project, move.Target)!;
                    map.AddIdentity(move.IsDirectory ? from : AssetFileTypes.ToIdentity(from), move.IsDirectory ? to : AssetFileTypes.ToIdentity(to), move.IsDirectory);
                }

                coordinator.VerifyWriter(operation);
                await this.WriteCopiesAsync(map, index.Mounts).ConfigureAwait(false);
                this.Invalidate();
                created = [.. map.Moves];
                return copies;
            }),
            cancellationToken).ConfigureAwait(false);
        this.RaiseFilesChanged(caller, AssetFilesChangedEventArgs.ForCreated([.. created.Select(static copy => copy.Target)], created.Any(static copy => copy.IsDirectory)));
        return copies;
    }

    /// <inheritdoc/>
    public async Task DeleteAsync(IReadOnlyList<AssetPath> sources, Func<IReadOnlyList<string>, Task> recycle, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(sources);
        ArgumentNullException.ThrowIfNull(recycle);
        var caller = SynchronizationContext.Current;
        List<FileSystemPath> deleted = [];
        var foldersDeleted = false;
        _ = await coordinator.RunAsync(
            (operation, token) => this.LoggedAsync("Delete", async () =>
            {
                var project = operation.Project;
                var index = await this.BuildIndexAsync(project, token).ConfigureAwait(false);
                var paths = new List<FileSystemPath>();
                foreach (var source in sources)
                {
                    var path = ResolveExisting(project, source);
                    var isDirectory = Directory.Exists(path.FullPath);
                    if (isDirectory && source.Parent!.IsMount && RelocationPaths.ImportTypeFolders.Contains(source.Name, StringComparer.Ordinal))
                    {
                        throw new AssetRelocationException($"'{source.Name}' is one of the importer's fixed folders and cannot be deleted.");
                    }

                    RejectStartSceneDelete(project, path, isDirectory);
                    paths.AddRange(GetDeletedFiles(project, index, path, isDirectory));
                }

                var distinct = paths.Distinct().ToList();
                _ = distinct.RemoveAll(path => distinct.Exists(other => other != path && path.IsSameOrInside(other)));
                coordinator.VerifyWriter(operation);
                foreach (var path in distinct)
                {
                    this.LogRecycling(path.FullPath);
                }

                foldersDeleted = distinct.Exists(static path => Directory.Exists(path.FullPath));
                try
                {
                    await recycle([.. distinct.Select(static path => path.FullPath)]).ConfigureAwait(false);
                    deleted = distinct;
                }
                finally
                {
                    this.Invalidate();
                }

                this.LogRecycled(distinct.Count);
                redirects?.RecordDeleted(distinct.Select(path => RelocationPaths.ToVirtual(project, path)).OfType<AssetPath>().Select(AssetFileTypes.ToIdentity));
                return true;
            }),
            cancellationToken).ConfigureAwait(false);
        if (deleted.Count != 0)
        {
            this.RaiseFilesChanged(caller, new([], [], deleted, foldersDeleted));
        }
    }

    private static async Task<(RelocationMap map, List<AssetPath> copies)> PlanCopiesAsync(
        ProjectContext project,
        AuthoredReferenceIndex index,
        IReadOnlyList<AssetPath> sources,
        AssetPath targetFolder,
        FileSystemPath folder,
        CancellationToken cancellationToken)
    {
        var map = new RelocationMap();
        var copies = new List<AssetPath>();
        var reserved = new HashSet<FileSystemPath>();
        foreach (var source in sources)
        {
            var path = ResolveExisting(project, source);
            var isDirectory = Directory.Exists(path.FullPath);
            RejectImportContent(project, index, path, isDirectory, "Import the model again instead of copying it.");
            if (isDirectory && targetFolder.IsSameOrInside(source))
            {
                throw new AssetRelocationException("A folder cannot be copied into itself.");
            }

            var target = GetUniquePath(folder, path.Name, isDirectory, reserved);
            map.AddMove(new(path, target, isDirectory));
            copies.Add(RelocationPaths.ToVirtual(project, target)!);
            if (!isDirectory && path.Name.EndsWith(AssetFileTypes.TextureDescriptor, StringComparison.Ordinal))
            {
                await AddTextureImageCopiesAsync(map, path, target, folder, reserved, cancellationToken).ConfigureAwait(false);
            }
        }

        return (map, copies);
    }

    private static async Task AddTextureImageCopiesAsync(RelocationMap map, FileSystemPath path, FileSystemPath target, FileSystemPath folder, HashSet<FileSystemPath> reserved, CancellationToken cancellationToken)
    {
        var stem = RelocationPaths.GetDisplayName(path.Name);
        var newStem = RelocationPaths.GetDisplayName(target.Name);
        foreach (var image in AuthoredReferenceFormat.ReadTextureSources(path, await File.ReadAllBytesAsync(path.FullPath, cancellationToken).ConfigureAwait(false))
            .Where(image => image.Parent == path.Parent && File.Exists(image.FullPath)))
        {
            var name = image.Name;
            var renamed = string.Equals(RelocationPaths.GetDisplayName(name), stem, FileSystemPath.NameComparison) ? newStem + AssetFileTypes.GetSuffix(name) : name;
            map.AddMove(new(image, GetUniquePath(folder, renamed, isDirectory: false, reserved), IsDirectory: false));
        }
    }

    // The project opens its start scene; deleting it, or a folder that holds it, would leave the project without one.
    private static void RejectStartSceneDelete(ProjectContext project, FileSystemPath path, bool isDirectory)
    {
        if (project.DefaultSceneId is not { } start)
        {
            return;
        }

        var scenes = isDirectory
            ? Directory.EnumerateFiles(path.FullPath, "*" + AssetFileTypes.SceneDescriptor, SearchOption.AllDirectories)
            : path.Name.EndsWith(AssetFileTypes.SceneDescriptor, StringComparison.Ordinal) ? [path.FullPath] : [];
        if (scenes.FirstOrDefault(scene => AuthoredReferenceFormat.ReadSceneId(File.ReadAllBytes(scene)) == start) is { } found)
        {
            throw new AssetRelocationException($"'{RelocationPaths.GetDisplayName(Path.GetFileName(found))}' is the project's start scene and cannot be deleted. Choose another start scene first.");
        }
    }

    private static void RejectImportContent(ProjectContext project, AuthoredReferenceIndex index, FileSystemPath path, bool isDirectory, string advice)
    {
        var root = project.ProjectRoot;
        foreach (var import in index.Imports)
        {
            var bundle = root.Combine(import.Settings.BundleRoot);
            var owned = isDirectory
                ? bundle.IsSameOrInside(path) || path.IsSameOrInside(bundle)
                : path == import.SidecarPath
                    || import.Settings.Files.Any(file => FileSystemPath.From(import.Settings.ResolveFile(project.ProjectRoot.FullPath, file)) == path);
            if (owned)
            {
                throw new AssetRelocationException($"'{path.Name}' belongs to the imported model '{import.PrimaryVirtualPath.Name}'. {advice}");
            }
        }
    }

    private static IEnumerable<FileSystemPath> GetDeletedFiles(ProjectContext project, AuthoredReferenceIndex index, FileSystemPath path, bool isDirectory)
    {
        var root = project.ProjectRoot;
        foreach (var import in index.Imports)
        {
            var bundle = root.Combine(import.Settings.BundleRoot);
            var model = import.PrimaryVirtualPath.Name;
            if (isDirectory && path != bundle && path.IsSameOrInside(bundle))
            {
                throw new AssetRelocationException($"'{path.Name}' is part of the imported model '{model}'. Delete the model instead.");
            }

            if (isDirectory)
            {
                continue;
            }

            if (path == import.SidecarPath)
            {
                throw new AssetRelocationException($"'{path.Name}' holds the import settings of '{model}'. Delete the model instead.");
            }

            var bundleFiles = import.Settings.Files.Select(file => FileSystemPath.From(import.Settings.ResolveFile(project.ProjectRoot.FullPath, file))).ToArray();
            if (path == import.PrimaryPath)
            {
                var owned = bundleFiles.Append(import.SidecarPath).ToHashSet();
                var onlyThisModel = Directory.Exists(bundle.FullPath)
                    && Directory.EnumerateFiles(bundle.FullPath, "*", SearchOption.AllDirectories).All(file => owned.Contains(FileSystemPath.From(file)));
                return onlyThisModel && bundle != root
                    ? [bundle]
                    : owned.Where(static file => File.Exists(file.FullPath));
            }

            if (bundleFiles.Contains(path))
            {
                throw new AssetRelocationException($"'{path.Name}' is part of the imported model '{model}'. Delete the model instead.");
            }
        }

        if (isDirectory || !path.Name.EndsWith(AssetFileTypes.TextureDescriptor, StringComparison.Ordinal))
        {
            var descriptors = isDirectory ? [] : index.Descriptors.Where(descriptor => descriptor.Location.Name.EndsWith(AssetFileTypes.TextureDescriptor, StringComparison.Ordinal)
                && descriptor.Location.Parent == path.Parent
                && AuthoredReferenceFormat.ReadTextureSources(descriptor.Location, File.ReadAllBytes(descriptor.Location.FullPath)).Contains(path))
                .Select(static descriptor => descriptor.Location);
            return [path, .. descriptors];
        }

        // An image another texture still uses stays; only this texture's own images go with it.
        var folder = path.Parent;
        var shared = index.Descriptors
            .Where(descriptor => descriptor.Location.Name.EndsWith(AssetFileTypes.TextureDescriptor, StringComparison.Ordinal)
                && descriptor.Location != path
                && descriptor.Location.Parent == folder)
            .SelectMany(static descriptor => AuthoredReferenceFormat.ReadTextureSources(descriptor.Location, File.ReadAllBytes(descriptor.Location.FullPath)))
            .ToHashSet();
        var images = AuthoredReferenceFormat.ReadTextureSources(path, File.ReadAllBytes(path.FullPath))
            .Where(image => image.Parent == folder && File.Exists(image.FullPath) && !shared.Contains(image));
        return [path, .. images];
    }

    private static FileSystemPath GetUniquePath(FileSystemPath folder, string name, bool isDirectory, HashSet<FileSystemPath> reserved)
    {
        var extension = isDirectory ? string.Empty : AssetFileTypes.GetSuffix(name);
        var stem = name[..^extension.Length];
        var candidate = folder.Combine(name);
        for (var index = 2; File.Exists(candidate.FullPath) || Directory.Exists(candidate.FullPath) || reserved.Contains(candidate) || ScriptSourceNames.FindCollision(candidate) is not null; index++)
        {
            candidate = folder.Combine(string.Create(CultureInfo.InvariantCulture, $"{stem} ({index}){extension}"));
        }

        _ = reserved.Add(candidate);
        return candidate;
    }

    private static FileSystemPath ResolveExisting(ProjectContext project, AssetPath source)
    {
        if (RelocationPaths.ToPhysical(project, source) is not { } path)
        {
            throw new AssetRelocationException($"'{source}' is read-only. Only project content can be changed.");
        }

        if (source.IsMount)
        {
            throw new AssetRelocationException("A content mount is changed in Mounts, not here.");
        }

        return File.Exists(path.FullPath) || Directory.Exists(path.FullPath)
            ? path
            : throw new AssetRelocationException($"'{source}' does not exist as an authored file. Imported outputs belong to their model.");
    }

    private async Task WriteCopiesAsync(RelocationMap map, IReadOnlyList<string> mounts)
    {
        var created = new List<string>();
        try
        {
            foreach (var move in map.Moves)
            {
                var pairs = move.IsDirectory
                    ? Directory.EnumerateFiles(move.Source.FullPath, "*", SearchOption.AllDirectories)
                        .Select(FileSystemPath.From)
                        .Select(file => (Source: file, Below: file.GetPathBelow(move.Source)))
                        .Where(static file => !file.Below.Split('/').Any(static segment => segment.StartsWith('.')))
                        .Select(file => (file.Source, Target: move.Target.Combine(file.Below)))
                        .ToArray()
                    : [(move.Source, move.Target)];
                if (move.IsDirectory)
                {
                    _ = Directory.CreateDirectory(move.Target.FullPath);
                    created.Add(move.Target.FullPath);
                }

                foreach (var (source, target) in pairs)
                {
                    var content = await File.ReadAllBytesAsync(source.FullPath, CancellationToken.None).ConfigureAwait(false);
                    var bytes = AuthoredReferenceFormat.IsReferencingSource(source.FullPath) ? AuthoredReferenceFormat.RewriteCopy(source, target, content, map, mounts) : content;
                    _ = Directory.CreateDirectory(target.Parent!.FullPath);
                    _ = await files.WriteAsync(target.FullPath, bytes, FileVersion.Missing, CancellationToken.None).ConfigureAwait(false);
                    created.Add(target.FullPath);
                    this.LogCopied(source.FullPath, target.FullPath);
                }
            }
        }
        catch (Exception failure) when (failure is IOException or UnauthorizedAccessException or StorageException or System.Text.Json.JsonException)
        {
            this.LogCopyFailed(failure, created.Count);
            foreach (var path in created.AsEnumerable().Reverse())
            {
                try
                {
                    if (File.Exists(path))
                    {
                        File.Delete(path);
                    }
                    else if (Directory.Exists(path))
                    {
                        Directory.Delete(path, recursive: true);
                    }
                }
                catch (Exception error) when (error is IOException or UnauthorizedAccessException)
                {
                    // A partial copy left behind is new content and harms no existing asset.
                    this.LogCopyCleanupFailed(error, path);
                }
            }

            throw new AssetRelocationException("The copy failed and was removed: " + failure.Message, failure);
        }
    }

    private void Invalidate()
    {
        lock (this.sync)
        {
            this.cached = null;
        }
    }

    private Task<AssetRelocationPlan> RelocateCoreAsync(AssetRelocationRequest request, CancellationToken cancellationToken)
        => coordinator.RunAsync(
            (operation, token) => this.LoggedAsync("Relocation", async () =>
            {
                foreach (var move in request.Moves)
                {
                    this.LogRelocationRequested(move.Source.VirtualPath, move.Target.VirtualPath);
                }

                foreach (var move in request.GroupMoves)
                {
                    this.LogGroupRelocationRequested(move.Source.VirtualPath, move.Group);
                }

                var index = await this.BuildIndexAsync(operation.Project, token).ConfigureAwait(false);
                var plan = await AssetRelocationPlanner.PlanAsync(operation.Project, request, index, files, token).ConfigureAwait(false);
                if (plan.IsEmpty)
                {
                    this.LogNothingToRelocate();
                    return plan;
                }

                this.LogRelocationPlanned(plan.Moves.Count, plan.Edits.Count, plan.Referrers.Count, plan.GroupFolders.Count);
                this.RejectUnsavedDocuments();

                coordinator.VerifyWriter(operation);
                IReadOnlyList<RelocatedFile> written;
                try
                {
                    written = await AssetRelocationTransaction.ApplyAsync(operation.Project.ProjectRoot.FullPath, plan, files, this.logger, this.FailAfterStep, token).ConfigureAwait(false);
                }
                finally
                {
                    this.Invalidate();
                }

                this.LogRelocationCommitted(plan.Moves.Count, plan.Edits.Count);
                redirects?.Record(plan.Map);
                await this.NotifyParticipantsAsync(new AssetRelocationChange(plan.Map, written)).ConfigureAwait(false);
                return plan;
            }),
            cancellationToken);

    // Open documents follow the committed files before the writer is released, so the next cook sees them consistent.
    // A participant failure leaves its document to report the file conflict on its next save.
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "The files are committed; one document's failure must not undo the relocation or skip the other documents.")]
    private async Task NotifyParticipantsAsync(AssetRelocationChange change)
    {
        IAssetRelocationParticipant[] current;
        lock (this.sync)
        {
            current = [.. this.participants];
        }

        this.LogNotifyingParticipants(current.Length, change.RewrittenFiles.Count);
        foreach (var participant in current)
        {
            try
            {
                await participant.FollowAsync(change).ConfigureAwait(false);
            }
            catch (Exception failure)
            {
                this.LogParticipantFailed(failure, participant.GetType().Name);
            }
        }
    }

    // Raised where the caller started the operation, like Progress<T>, so UI listeners need no dispatcher.
    private void RaiseFilesChanged(SynchronizationContext? caller, AssetFilesChangedEventArgs args)
    {
        if (this.FilesChanged is not { } handlers)
        {
            return;
        }

        if (caller is null)
        {
            handlers(this, args);
        }
        else
        {
            caller.Post(_ => handlers(this, args), state: null);
        }
    }

    private async Task<T> LoggedAsync<T>(string operation, Func<Task<T>> work)
    {
        try
        {
            return await work().ConfigureAwait(false);
        }
        catch (AssetRelocationException rejected)
        {
            // A failed step logged its own error with the cause; this records the outcome once.
            this.LogNotDone(operation, rejected.Message);
            throw;
        }
        catch (Exception failure) when (failure is not OperationCanceledException)
        {
            this.LogFailed(failure, operation);
            throw;
        }
    }

    // A change runs on a fresh index; files it could not read keep their references as they are, so say which.
    private async Task<AuthoredReferenceIndex> BuildIndexAsync(ProjectContext project, CancellationToken cancellationToken)
    {
        var index = await AuthoredReferenceIndex.BuildAsync(project, cancellationToken).ConfigureAwait(false);
        foreach (var path in index.Unreadable)
        {
            this.LogUnreadableFile(path.FullPath);
        }

        return index;
    }

    private void RejectUnsavedDocuments()
    {
        var unsaved = documents.GetState().Documents.Where(static document => document.IsDirty).Select(static document => document.DisplayName)
            .Distinct(StringComparer.OrdinalIgnoreCase).Order(StringComparer.OrdinalIgnoreCase).ToArray();
        if (unsaved.Length != 0)
        {
            throw new AssetRelocationException($"Save or discard the changes in {string.Join(", ", unsaved)} first. Moving content rewrites the files that reference it.");
        }
    }

    private sealed partial class Registration(AssetRelocationService owner, IAssetRelocationParticipant participant) : IDisposable
    {
        public void Dispose()
        {
            lock (owner.sync)
            {
                _ = owner.participants.Remove(participant);
            }
        }
    }
}
