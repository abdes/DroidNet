// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.IO.Abstractions;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.ContentPipeline.Mounting;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Mounts;
using Oxygen.Editor.Runtime.Diagnostics;
using Oxygen.Managed.Assets.Persistence.Publication;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.LivePreview.Session;

/// <summary>
/// Applies a change of what the session's mount table reads: when a <c>.mounts.json</c> file is created, changed, renamed
/// or deleted in the project root or in a folder above it, or a library project bound by its <c>.cooked</c> folder
/// publishes a new <c>head.json</c>, the mount table is resolved again, and a table that differs is applied as a mount
/// change. The change rebuilds the catalogs, the preview's content and the cook's libraries in one transaction, as a
/// change made in the Mounts dialog does.
/// </summary>
/// <remarks>
/// A file can be written in several steps, so a change is applied once the files have been quiet for a moment. A table
/// that refuses the files, such as one that binds two mounts to one folder, is not applied: the project keeps its mounts
/// and the log says why. So is a new generation that would break a pak mounted above it: the old generation stays, and
/// the finding is published.
/// </remarks>
public sealed partial class MountsFileWatcher : IDisposable
{
    private static readonly TimeSpan QuietWindow = TimeSpan.FromMilliseconds(250);

    private readonly IProjectSession session;
    private readonly IContentMountChanges changes;
    private readonly IOperationResultPublisher results;
    private readonly IStatusReducer status;
    private readonly ILogger logger;
    private readonly List<IFileSystemWatcher> watchers = [];
    private readonly List<IFileSystemWatcher> libraryWatchers = [];
    private readonly Lock libraryGate = new();
    private readonly Timer quiet;

    /// <summary>Initializes a new instance of the <see cref="MountsFileWatcher"/> class, watching the session's
    /// project.</summary>
    /// <param name="session">The project session.</param>
    /// <param name="changes">Applies a mount change to the session.</param>
    /// <param name="results">Publishes a new generation that was refused.</param>
    /// <param name="status">Gives that result its status.</param>
    /// <param name="logger">The logger.</param>
    public MountsFileWatcher(IProjectSession session, IContentMountChanges changes, IOperationResultPublisher results, IStatusReducer status, ILogger<MountsFileWatcher> logger)
    {
        ArgumentNullException.ThrowIfNull(session);
        ArgumentNullException.ThrowIfNull(changes);
        ArgumentNullException.ThrowIfNull(results);
        ArgumentNullException.ThrowIfNull(status);
        ArgumentNullException.ThrowIfNull(logger);
        this.session = session;
        this.changes = changes;
        this.results = results;
        this.status = status;
        this.logger = logger;
        this.quiet = new(_ => this.ApplyInBackground(), state: null, Timeout.InfiniteTimeSpan, Timeout.InfiniteTimeSpan);
        var fileSystem = session.Context.FileSystem;
        for (var folder = session.Context.ProjectRoot; folder is not null; folder = folder.Parent)
        {
            if (fileSystem.Directory.Exists(folder.FullPath))
            {
                this.watchers.Add(this.Watch(fileSystem, folder.FullPath, ProjectMounts.FileName));
            }
        }

        this.WatchLibraries();
        session.ContextChanged += this.OnContextChanged;
    }

    /// <inheritdoc/>
    public void Dispose()
    {
        this.session.ContextChanged -= this.OnContextChanged;
        foreach (var watcher in this.watchers)
        {
            watcher.Dispose();
        }

        lock (this.libraryGate)
        {
            foreach (var watcher in this.libraryWatchers)
            {
                watcher.Dispose();
            }

            this.libraryWatchers.Clear();
        }

        this.quiet.Dispose();
    }

    /// <summary>Resolves the mount table again, and applies it when it differs from the session's.</summary>
    /// <param name="cancellationToken">Cancels the change.</param>
    /// <returns><see langword="true"/> when a new table was applied.</returns>
    internal async Task<bool> ApplyAsync(CancellationToken cancellationToken)
    {
        var current = this.session.Context;
        var table = ProjectMounts.Resolve(current.FileSystem, current.ProjectRoot, current.DeclaredMounts);
        if (IsSame(table.Mounts, current.Mounts) && table.Findings.SequenceEqual(current.MountFindings))
        {
            this.LogUnchanged(current);
            return false;
        }

        if (RefusedGeneration(current, table) is { } refusal)
        {
            this.LogGenerationRefused(refusal);
            RuntimeOperationResults.PublishFailure(
                this.results,
                this.status,
                RuntimeOperationKinds.CookedRootRefresh,
                FailureDomain.AssetMount,
                MountCodes.CookedDependencyMissing,
                "Library not updated",
                $"The new cook of a library is refused, and the old one stays mounted: {refusal.Message}",
                new AffectedScope { ProjectId = current.ProjectId, ProjectName = current.Name, ProjectPath = current.ProjectRoot.FullPath },
                current.ProjectRoot.FullPath);
            return false;
        }

        this.LogApplying(current, table);
        return await this.changes.ChangeMountsAsync(current, MountChange.ProjectInfoOf(current, this.session.Settings), MountBindings.None, cancellationToken).ConfigureAwait(false);
    }

    // A library's facts are read anew by each resolution; its bindings, generation and findings say whether it changed.
    private static bool IsSame(IReadOnlyList<ResolvedMount> left, IReadOnlyList<ResolvedMount> right)
    {
        return left.Count == right.Count && left.Zip(right).All(static pair => Plain(pair.First) == Plain(pair.Second)
            && pair.First.Findings.SequenceEqual(pair.Second.Findings));

        static ResolvedMount Plain(ResolvedMount mount) => mount with { Library = null, Findings = [] };
    }

    // The bindings are as they were and a generation moved: a pak above it that it would break refuses the swap.
    private static MountFinding? RefusedGeneration(ProjectContext current, ProjectMountTable table)
    {
        if (table.Mounts.Count != current.Mounts.Count || !table.Mounts.Zip(current.Mounts).All(static pair => pair.First.BoundTo == pair.Second.BoundTo))
        {
            return null;
        }

        return table.Mounts.Zip(current.Mounts)
            .Where(static pair => pair.Second.IsBound && pair.First is { State: MountState.Invalid, Container: MountContainer.Pak })
            .SelectMany(static pair => pair.First.Findings)
            .FirstOrDefault(static finding => string.Equals(finding.Code, MountCodes.CookedDependencyMissing, StringComparison.Ordinal));
    }

    private void OnContextChanged(object? sender, EventArgs args) => this.WatchLibraries();

    // A library project's output folder says it publishes by its head; the project's mounts say which ones to watch.
    private void WatchLibraries()
    {
        lock (this.libraryGate)
        {
            foreach (var watcher in this.libraryWatchers)
            {
                watcher.Dispose();
            }

            this.libraryWatchers.Clear();
            var context = this.session.Context;
            foreach (var mount in context.CookedMounts)
            {
                if (mount.BoundTo is { } bound && context.FileSystem.File.Exists(bound.Combine(CookPublicationFormat.HeadFileName).FullPath))
                {
                    this.libraryWatchers.Add(this.Watch(context.FileSystem, bound.FullPath, CookPublicationFormat.HeadFileName));
                }
            }
        }
    }

    private IFileSystemWatcher Watch(IFileSystem fileSystem, string folder, string file)
    {
        var watcher = fileSystem.FileSystemWatcher.New(folder, file);
        watcher.NotifyFilter = NotifyFilters.FileName | NotifyFilters.LastWrite | NotifyFilters.Size;
        watcher.Created += this.OnChanged;
        watcher.Changed += this.OnChanged;
        watcher.Deleted += this.OnChanged;
        watcher.Renamed += this.OnChanged;
        watcher.EnableRaisingEvents = true;
        return watcher;
    }

    private void OnChanged(object sender, FileSystemEventArgs args)
    {
        this.LogFileChanged(args.FullPath, args.ChangeType);
        _ = this.quiet.Change(QuietWindow, Timeout.InfiniteTimeSpan);
    }

    // A change the session no longer needs ends with it; one that another mount change overtook is checked again against
    // the project's new mounts; one the mount table refuses leaves the project's mounts as they are.
    private void ApplyInBackground()
        => _ = Task.Run(
            async () =>
            {
                try
                {
                    _ = await this.ApplyAsync(this.session.Lifetime).ConfigureAwait(false);
                }
                catch (OperationCanceledException) when (this.session.Lifetime.IsCancellationRequested)
                {
                    // The session closed.
                }
                catch (OperationCanceledException)
                {
                    this.LogSuperseded();
                    _ = this.quiet.Change(QuietWindow, Timeout.InfiniteTimeSpan);
                }
                catch (Exception failure) when (failure is InvalidDataException or InvalidOperationException or IOException or UnauthorizedAccessException)
                {
                    this.LogRefused(failure);
                }
            },
            CancellationToken.None);
}
