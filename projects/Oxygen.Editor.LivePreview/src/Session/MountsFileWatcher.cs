// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.IO.Abstractions;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.ContentPipeline.Mounting;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Mounts;

namespace Oxygen.Editor.LivePreview.Session;

/// <summary>
/// Applies a change of a <c>.mounts.json</c> file to the session's project: when one is created, changed, renamed or
/// deleted in the project root or in a folder above it, the mount table is resolved again, and a table that differs is
/// applied as a mount change. The change rebuilds the catalogs, the preview's content and the cook's libraries in one
/// transaction, as a change made in the Mounts dialog does.
/// </summary>
/// <remarks>
/// A file can be written in several steps, so a change is applied once the files have been quiet for a moment. A table
/// that refuses the files, such as one that binds two mounts to one folder, is not applied: the project keeps its mounts
/// and the log says why.
/// </remarks>
public sealed partial class MountsFileWatcher : IDisposable
{
    private static readonly TimeSpan QuietWindow = TimeSpan.FromMilliseconds(250);

    private readonly IProjectSession session;
    private readonly IContentMountChanges changes;
    private readonly ILogger logger;
    private readonly List<IFileSystemWatcher> watchers = [];
    private readonly Timer quiet;

    /// <summary>Initializes a new instance of the <see cref="MountsFileWatcher"/> class, watching the session's
    /// project.</summary>
    /// <param name="session">The project session.</param>
    /// <param name="changes">Applies a mount change to the session.</param>
    /// <param name="logger">The logger.</param>
    public MountsFileWatcher(IProjectSession session, IContentMountChanges changes, ILogger<MountsFileWatcher> logger)
    {
        ArgumentNullException.ThrowIfNull(session);
        ArgumentNullException.ThrowIfNull(changes);
        ArgumentNullException.ThrowIfNull(logger);
        this.session = session;
        this.changes = changes;
        this.logger = logger;
        this.quiet = new(_ => this.ApplyInBackground(), state: null, Timeout.InfiniteTimeSpan, Timeout.InfiniteTimeSpan);
        var fileSystem = session.Context.FileSystem;
        for (var folder = session.Context.ProjectRoot; folder is not null; folder = folder.Parent)
        {
            if (fileSystem.Directory.Exists(folder.FullPath))
            {
                this.watchers.Add(this.Watch(fileSystem, folder.FullPath));
            }
        }
    }

    /// <inheritdoc/>
    public void Dispose()
    {
        foreach (var watcher in this.watchers)
        {
            watcher.Dispose();
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
        if (table.Mounts.SequenceEqual(current.Mounts) && table.Findings.SequenceEqual(current.MountFindings))
        {
            this.LogUnchanged(current);
            return false;
        }

        this.LogApplying(current, table);
        return await this.changes.ChangeMountsAsync(current, MountChange.ProjectInfoOf(current, this.session.Settings), MountBindings.None, cancellationToken).ConfigureAwait(false);
    }

    private IFileSystemWatcher Watch(IFileSystem fileSystem, string folder)
    {
        var watcher = fileSystem.FileSystemWatcher.New(folder, ProjectMounts.FileName);
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
