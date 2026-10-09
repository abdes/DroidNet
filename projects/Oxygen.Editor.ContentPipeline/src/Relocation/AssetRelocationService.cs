// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using DroidNet.Storage;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentPipeline.Relocation;

/// <summary>Runs reference-aware relocation, copy and delete as the project's content writer.</summary>
/// <param name="coordinator">Serializes project content changes with cooking and import.</param>
/// <param name="projects">The active project.</param>
/// <param name="documents">The open documents and their unsaved state.</param>
/// <param name="files">The atomic file store.</param>
/// <param name="logger">Records every move, rewrite, copy, delete, rejection and restore.</param>
/// <param name="pipeline">Republishes changed content after each change; without it nothing is recooked.</param>
/// <param name="redirects">The session's relocation record for undo, redo and paste; optional.</param>
public sealed partial class AssetRelocationService(IContentCookCoordinator coordinator, IProjectContextService projects, ICookDocumentRegistry documents, IAtomicFileStore files,
    ILogger<AssetRelocationService>? logger = null, IContentPipelineService? pipeline = null, AssetRedirects? redirects = null)
    : IAssetRelocationService
{
    private readonly ILogger logger = (ILogger?)logger ?? NullLogger.Instance;
    private readonly Lock sync = new();
    private readonly List<IAssetRelocationParticipant> participants = [];
    private Task<AuthoredReferenceIndex>? cached;
    private ProjectContext? cachedProject;

    /// <summary>Gets or sets a test hook that fails a relocation after this many applied steps.</summary>
    internal int? FailAfterStep { get; set; }

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
    public async Task<AssetRelocationPlan> RelocateAsync(AssetRelocationRequest request, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(request);
        var published = new TaskCompletionSource<string?>(TaskCreationOptions.RunContinuationsAsynchronously);
        AssetRelocationPlan plan;
        try
        {
            plan = await this.RelocateCoreAsync(request, published.Task, cancellationToken).ConfigureAwait(false);
        }
        catch
        {
            _ = published.TrySetResult(null);
            throw;
        }

        plan.ContentPublished = published.Task;
        if (plan.IsEmpty)
        {
            _ = published.TrySetResult(null);
            return plan;
        }

        // The writer is released: cook now, so open documents that followed the change are consistent with their files.
        _ = this.CookAfterChangeAsync("relocation", published);
        return plan;
    }

    /// <inheritdoc/>
    public async Task<IReadOnlyList<string>> FindReferrersAsync(string virtualPath, CancellationToken cancellationToken)
    {
        var project = projects.ActiveProject;
        if (project is null)
        {
            return [];
        }

        Task<AuthoredReferenceIndex> index;
        lock (this.sync)
        {
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
    public Task<IReadOnlyList<string>> CopyAsync(IReadOnlyList<string> sources, string targetFolder, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(sources);
        ArgumentNullException.ThrowIfNull(targetFolder);
        return this.ThenCookAsync("copy", coordinator.RunAsync<IReadOnlyList<string>>(
            (operation, token) => this.LoggedAsync<IReadOnlyList<string>>("Copy", async () =>
            {
                var project = operation.Project;
                var index = await this.BuildIndexAsync(project, token).ConfigureAwait(false);
                var folder = RelocationPaths.FindWritableMount(project, targetFolder) is not null ? RelocationPaths.ToPhysical(project, targetFolder) : null;
                if (folder is null || !Directory.Exists(folder))
                {
                    throw new AssetRelocationException("Choose a destination under Content or another authoring folder.");
                }

                var (map, copies) = await PlanCopiesAsync(project, index, sources, targetFolder, folder, token).ConfigureAwait(false);
                foreach (var move in map.Moves)
                {
                    var from = RelocationPaths.ToVirtual(project, move.Source)!;
                    var to = RelocationPaths.ToVirtual(project, move.Target)!;
                    map.AddIdentity(move.IsDirectory ? from : RelocationPaths.ToIdentity(from), move.IsDirectory ? to : RelocationPaths.ToIdentity(to), move.IsDirectory);
                }

                coordinator.VerifyWriter(operation);
                await this.WriteCopiesAsync(map, index.Mounts).ConfigureAwait(false);
                this.Invalidate();
                return copies;
            }),
            cancellationToken));
    }

    /// <inheritdoc/>
    public Task DeleteAsync(IReadOnlyList<string> sources, Func<IReadOnlyList<string>, Task> recycle, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(sources);
        ArgumentNullException.ThrowIfNull(recycle);
        return this.ThenCookAsync("delete", coordinator.RunAsync(
            (operation, token) => this.LoggedAsync("Delete", async () =>
            {
                var project = operation.Project;
                var index = await this.BuildIndexAsync(project, token).ConfigureAwait(false);
                var paths = new List<string>();
                foreach (var source in sources)
                {
                    var path = ResolveExisting(project, source);
                    var isDirectory = Directory.Exists(path);
                    var segments = source.Trim('/').Split('/');
                    if (isDirectory && segments.Length == 2 && RelocationPaths.ImportTypeFolders.Contains(segments[1], StringComparer.OrdinalIgnoreCase))
                    {
                        throw new AssetRelocationException($"'{segments[1]}' is one of the importer's fixed folders and cannot be deleted.");
                    }

                    if (!isDirectory && RelocationPaths.IsEditorScene(project, path))
                    {
                        throw new AssetRelocationException("Delete scenes with Delete on the scene.");
                    }

                    paths.AddRange(GetDeletedFiles(project, index, path, isDirectory));
                }

                var distinct = paths.Distinct(StringComparer.OrdinalIgnoreCase).ToList();
                _ = distinct.RemoveAll(path => distinct.Exists(other => !string.Equals(other, path, StringComparison.OrdinalIgnoreCase)
                    && path.StartsWith(other + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase)));
                coordinator.VerifyWriter(operation);
                foreach (var path in distinct)
                {
                    this.LogRecycling(path);
                }

                try
                {
                    await recycle(distinct).ConfigureAwait(false);
                }
                finally
                {
                    this.Invalidate();
                }

                this.LogRecycled(distinct.Count);
                redirects?.RecordDeleted(distinct.Select(path => RelocationPaths.ToVirtual(project, path)).OfType<string>().Select(RelocationPaths.ToIdentity));
                return true;
            }),
            cancellationToken));
    }

    /// <inheritdoc/>
    public void Invalidate()
    {
        lock (this.sync)
        {
            this.cached = null;
        }
    }

    private static async Task<(RelocationMap map, List<string> copies)> PlanCopiesAsync(
        ProjectContext project,
        AuthoredReferenceIndex index,
        IReadOnlyList<string> sources,
        string targetFolder,
        string folder,
        CancellationToken cancellationToken)
    {
        var map = new RelocationMap();
        var copies = new List<string>();
        var reserved = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var source in sources)
        {
            var path = ResolveExisting(project, source);
            var isDirectory = Directory.Exists(path);
            RejectImportContent(project, index, path, isDirectory, "Import the model again instead of copying it.");
            if (!isDirectory && RelocationPaths.IsEditorScene(project, path))
            {
                throw new AssetRelocationException("Duplicate scenes with Duplicate on the scene.");
            }

            if (isDirectory && RelocationPaths.IsSameOrInside(targetFolder, source))
            {
                throw new AssetRelocationException("A folder cannot be copied into itself.");
            }

            var target = GetUniquePath(folder, Path.GetFileName(path), isDirectory, reserved);
            map.AddMove(new(path, target, isDirectory));
            copies.Add(RelocationPaths.ToVirtual(project, target)!);
            if (!isDirectory && path.EndsWith(".otex.json", StringComparison.OrdinalIgnoreCase))
            {
                await AddTextureImageCopiesAsync(map, path, target, folder, reserved, cancellationToken).ConfigureAwait(false);
            }
        }

        return (map, copies);
    }

    private static async Task AddTextureImageCopiesAsync(RelocationMap map, string path, string target, string folder, HashSet<string> reserved, CancellationToken cancellationToken)
    {
        var stem = RelocationPaths.GetDisplayName(Path.GetFileName(path));
        var newStem = RelocationPaths.GetDisplayName(Path.GetFileName(target));
        foreach (var image in AuthoredReferenceFormat.ReadTextureSources(path, await File.ReadAllBytesAsync(path, cancellationToken).ConfigureAwait(false))
            .Where(image => string.Equals(Path.GetDirectoryName(image), Path.GetDirectoryName(path), StringComparison.OrdinalIgnoreCase) && File.Exists(image)))
        {
            var name = Path.GetFileName(image);
            var renamed = string.Equals(RelocationPaths.GetDisplayName(name), stem, StringComparison.OrdinalIgnoreCase) ? newStem + RelocationPaths.GetExtension(name) : name;
            map.AddMove(new(image, GetUniquePath(folder, renamed, isDirectory: false, reserved), IsDirectory: false));
        }
    }

    private static void RejectImportContent(ProjectContext project, AuthoredReferenceIndex index, string path, bool isDirectory, string advice)
    {
        foreach (var import in index.Imports)
        {
            var bundle = Path.GetFullPath(Path.Combine(project.ProjectRoot, import.Settings.BundleRoot));
            var owned = isDirectory
                ? bundle.StartsWith(path + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase) || string.Equals(bundle, path, StringComparison.OrdinalIgnoreCase)
                    || path.StartsWith(bundle + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase)
                : string.Equals(path, import.SidecarPath, StringComparison.OrdinalIgnoreCase)
                    || import.Settings.Files.Any(file => string.Equals(import.Settings.ResolveFile(project.ProjectRoot, file), path, StringComparison.OrdinalIgnoreCase));
            if (owned)
            {
                throw new AssetRelocationException($"'{Path.GetFileName(path)}' belongs to the imported model '{RelocationPaths.GetName(import.PrimaryVirtualPath)}'. {advice}");
            }
        }
    }

    private static IEnumerable<string> GetDeletedFiles(ProjectContext project, AuthoredReferenceIndex index, string path, bool isDirectory)
    {
        foreach (var import in index.Imports)
        {
            var bundle = Path.GetFullPath(Path.Combine(project.ProjectRoot, import.Settings.BundleRoot));
            var model = RelocationPaths.GetName(import.PrimaryVirtualPath);
            if (isDirectory && path.StartsWith(bundle + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
            {
                throw new AssetRelocationException($"'{Path.GetFileName(path)}' is part of the imported model '{model}'. Delete the model instead.");
            }

            if (isDirectory)
            {
                continue;
            }

            if (string.Equals(path, import.SidecarPath, StringComparison.OrdinalIgnoreCase))
            {
                throw new AssetRelocationException($"'{Path.GetFileName(path)}' holds the import settings of '{model}'. Delete the model instead.");
            }

            var bundleFiles = import.Settings.Files.Select(file => import.Settings.ResolveFile(project.ProjectRoot, file)).ToArray();
            if (string.Equals(path, import.PrimaryPath, StringComparison.OrdinalIgnoreCase))
            {
                var owned = bundleFiles.Append(import.SidecarPath).ToHashSet(StringComparer.OrdinalIgnoreCase);
                var onlyThisModel = Directory.Exists(bundle) && Directory.EnumerateFiles(bundle, "*", SearchOption.AllDirectories).All(owned.Contains);
                return onlyThisModel && !string.Equals(bundle, Path.GetFullPath(project.ProjectRoot), StringComparison.OrdinalIgnoreCase)
                    ? [bundle]
                    : owned.Where(File.Exists);
            }

            if (bundleFiles.Contains(path, StringComparer.OrdinalIgnoreCase))
            {
                throw new AssetRelocationException($"'{Path.GetFileName(path)}' is part of the imported model '{model}'. Delete the model instead.");
            }
        }

        if (isDirectory || !path.EndsWith(".otex.json", StringComparison.OrdinalIgnoreCase))
        {
            var descriptors = isDirectory ? [] : index.Descriptors.Where(descriptor => descriptor.Path.EndsWith(".otex.json", StringComparison.OrdinalIgnoreCase)
                && string.Equals(Path.GetDirectoryName(descriptor.Path), Path.GetDirectoryName(path), StringComparison.OrdinalIgnoreCase)
                && AuthoredReferenceFormat.ReadTextureSources(descriptor.Path, File.ReadAllBytes(descriptor.Path)).Contains(path, StringComparer.OrdinalIgnoreCase))
                .Select(static descriptor => descriptor.Path);
            return [path, .. descriptors];
        }

        // An image another texture still uses stays; only this texture's own images go with it.
        var folder = Path.GetDirectoryName(path);
        var shared = index.Descriptors
            .Where(descriptor => descriptor.Path.EndsWith(".otex.json", StringComparison.OrdinalIgnoreCase)
                && !string.Equals(descriptor.Path, path, StringComparison.OrdinalIgnoreCase)
                && string.Equals(Path.GetDirectoryName(descriptor.Path), folder, StringComparison.OrdinalIgnoreCase))
            .SelectMany(static descriptor => AuthoredReferenceFormat.ReadTextureSources(descriptor.Path, File.ReadAllBytes(descriptor.Path)))
            .ToHashSet(StringComparer.OrdinalIgnoreCase);
        var images = AuthoredReferenceFormat.ReadTextureSources(path, File.ReadAllBytes(path))
            .Where(image => string.Equals(Path.GetDirectoryName(image), folder, StringComparison.OrdinalIgnoreCase) && File.Exists(image) && !shared.Contains(image));
        return [path, .. images];
    }

    private static string GetUniquePath(string folder, string name, bool isDirectory, HashSet<string> reserved)
    {
        var extension = isDirectory ? string.Empty : RelocationPaths.GetExtension(name);
        var stem = name[..^extension.Length];
        var candidate = Path.Combine(folder, name);
        for (var index = 2; File.Exists(candidate) || Directory.Exists(candidate) || reserved.Contains(candidate); index++)
        {
            candidate = Path.Combine(folder, string.Create(CultureInfo.InvariantCulture, $"{stem} ({index}){extension}"));
        }

        _ = reserved.Add(candidate);
        return candidate;
    }

    private static string ResolveExisting(ProjectContext project, string source)
    {
        if (RelocationPaths.FindWritableMount(project, source) is null || RelocationPaths.ToPhysical(project, source) is not { } path)
        {
            throw new AssetRelocationException($"'{source}' is read-only. Only project content can be changed.");
        }

        if (source.Trim('/').Split('/').Length == 1)
        {
            throw new AssetRelocationException("A content mount is changed in Mounts, not here.");
        }

        return File.Exists(path) || Directory.Exists(path)
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
                    ? Directory.EnumerateFiles(move.Source, "*", SearchOption.AllDirectories)
                        .Where(file => !Path.GetRelativePath(move.Source, file).Split(Path.DirectorySeparatorChar).Any(static segment => segment.StartsWith('.')))
                        .Select(file => (Source: file, Target: move.Target + file[move.Source.Length..]))
                        .ToArray()
                    : [(move.Source, move.Target)];
                if (move.IsDirectory)
                {
                    _ = Directory.CreateDirectory(move.Target);
                    created.Add(move.Target);
                }

                foreach (var (source, target) in pairs)
                {
                    var content = await File.ReadAllBytesAsync(source, CancellationToken.None).ConfigureAwait(false);
                    var bytes = AuthoredReferenceFormat.IsDescriptor(source) ? AuthoredReferenceFormat.RewriteCopy(source, target, content, map, mounts) : content;
                    _ = Directory.CreateDirectory(Path.GetDirectoryName(target)!);
                    _ = await files.WriteAsync(target, bytes, FileVersion.Missing, CancellationToken.None).ConfigureAwait(false);
                    created.Add(target);
                    this.LogCopied(source, target);
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

    private Task<AssetRelocationPlan> RelocateCoreAsync(AssetRelocationRequest request, Task<string?> contentPublished, CancellationToken cancellationToken)
        => coordinator.RunAsync(
            (operation, token) => this.LoggedAsync("Relocation", async () =>
            {
                foreach (var move in request.Moves)
                {
                    this.LogRelocationRequested(move.SourcePath, move.TargetPath);
                }

                foreach (var move in request.GroupMoves)
                {
                    this.LogGroupRelocationRequested(move.SourcePath, move.Group);
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
                    written = await AssetRelocationTransaction.ApplyAsync(operation.Project.ProjectRoot, plan, files, this.logger, this.FailAfterStep, token).ConfigureAwait(false);
                }
                finally
                {
                    this.Invalidate();
                }

                this.LogRelocationCommitted(plan.Moves.Count, plan.Edits.Count);
                redirects?.Record(plan.Map);
                await this.NotifyParticipantsAsync(new AssetRelocationChange(plan.Map, written, contentPublished)).ConfigureAwait(false);
                return plan;
            }),
            cancellationToken);

    // Open documents follow the committed files before the writer is released, so the follow-up cook sees them
    // consistent. A participant failure leaves its document to report the file conflict on its next save.
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

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "The follow-up cook runs after the change committed; its failure is logged and shown in the Cooking panel.")]
    private async Task CookAfterChangeAsync(string operation, TaskCompletionSource<string?> published)
    {
        string? problem = null;
        try
        {
            if (pipeline is null)
            {
                return;
            }

            this.LogCookStarted(operation);
            var result = await pipeline.CookProjectAsync(CancellationToken.None).ConfigureAwait(false);
            if (result.Status is Oxygen.Managed.Core.Diagnostics.OperationStatus.Succeeded or Oxygen.Managed.Core.Diagnostics.OperationStatus.SucceededWithWarnings)
            {
                this.LogCookPublished(operation, result.Status);
            }
            else
            {
                problem = string.Join(" ", result.Diagnostics.Select(static diagnostic => diagnostic.Message));
                if (problem.Length == 0)
                {
                    problem = $"The cook ended {result.Status}.";
                }

                this.LogCookNotSucceeded(operation, result.Status, problem);
            }
        }
        catch (OperationCanceledException)
        {
            problem = "Republishing was cancelled.";
            this.LogCookCancelled(operation);
        }
        catch (Exception failure)
        {
            problem = failure.Message;
            this.LogCookFailed(failure, operation);
        }
        finally
        {
            _ = published.TrySetResult(problem);
        }
    }

    private async Task<T> ThenCookAsync<T>(string operation, Task<T> change)
    {
        var result = await change.ConfigureAwait(false);
        _ = this.CookAfterChangeAsync(operation, new TaskCompletionSource<string?>(TaskCreationOptions.RunContinuationsAsynchronously));
        return result;
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
            this.LogUnreadableFile(path);
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
