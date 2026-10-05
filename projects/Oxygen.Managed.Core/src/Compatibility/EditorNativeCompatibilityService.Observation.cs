// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Core.Compatibility;

/// <summary>Publishes display-only producer identity and invalidates it on installation changes.</summary>
public sealed partial class EditorNativeCompatibilityService
{
    private readonly Lock observationSync = new();
    private readonly List<FileSystemWatcher> watchers = [];
    private readonly string[] producerRoots = [Path.Combine(installation.EngineRoot, "bin"), installation.SchemaDirectory,
        Path.GetFullPath(installation.EditorRoot), Path.Combine(installation.EditorRoot, "Schemas")];

    private NativeProducerObservation observation = NativeProducerObservation.Unknown;
    private long observationGeneration;
    private bool disposed;

    /// <inheritdoc />
    public event EventHandler? ObservationChanged;

    /// <inheritdoc />
    public NativeProducerObservation Observation
    {
        get
        {
            lock (this.observationSync)
            {
                return this.observation;
            }
        }
    }

    /// <inheritdoc />
    public void Dispose()
    {
        FileSystemWatcher[] retired;
        lock (this.observationSync)
        {
            this.disposed = true;
            this.observationGeneration++;
            this.observation = new(this.observation.Revision + 1, Fingerprint: null, []);
            retired = this.DetachWatchers();
        }

        DisposeWatchers(retired);
    }

    private static void DisposeWatchers(IEnumerable<FileSystemWatcher> watchers)
    {
        foreach (var watcher in watchers)
        {
            watcher.Dispose();
        }
    }

    private void EnsureWatcher()
    {
        if (!cooking || this.watchers.Count != 0)
        {
            return;
        }

        var candidates = new List<FileSystemWatcher>();
        try
        {
            var parents = new[] { Path.GetDirectoryName(Path.GetFullPath(installation.EngineRoot)) ?? Path.GetFullPath(installation.EngineRoot), Path.GetDirectoryName(Path.GetFullPath(installation.EditorRoot)) ?? Path.GetFullPath(installation.EditorRoot) }
                .Distinct(StringComparer.OrdinalIgnoreCase).ToArray();
            foreach (var path in parents.Where(path => !parents.Any(other => !string.Equals(path, other, StringComparison.OrdinalIgnoreCase)
                && path.StartsWith(other.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar) + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))))
            {
                if (!Directory.Exists(path))
                {
                    return;
                }

                // Keeping watcher handles outside the installation permits SDK directory replacement.
                var watcher = new FileSystemWatcher(path)
                {
                    IncludeSubdirectories = true,
                    InternalBufferSize = 64 * 1024,
                    NotifyFilter = NotifyFilters.FileName | NotifyFilters.DirectoryName | NotifyFilters.LastWrite | NotifyFilters.Size,
                };
                candidates.Add(watcher);
                watcher.Changed += this.OnArtifactChanged;
                watcher.Created += this.OnArtifactChanged;
                watcher.Deleted += this.OnArtifactChanged;
                watcher.Renamed += this.OnArtifactRenamed;
                watcher.Error += this.OnWatcherError;
                watcher.EnableRaisingEvents = true;
            }

            this.watchers.AddRange(candidates);
            candidates.Clear();
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or System.ComponentModel.Win32Exception)
        {
            System.Diagnostics.Trace.TraceWarning("Producer observation could not monitor installation changes: {0}", error.Message);
        }
        finally
        {
            DisposeWatchers(candidates);
        }
    }

    private FileSystemWatcher[] DetachWatchers()
    {
        var retired = this.watchers.ToArray();
        this.watchers.Clear();
        return retired;
    }

    private void OnArtifactChanged(object sender, FileSystemEventArgs change)
    {
        if (!this.IsActiveWatcher(sender))
        {
            return;
        }

        if (change.ChangeType == WatcherChangeTypes.Changed && this.IsProducerDirectory(change.FullPath))
        {
            return;
        }

        if (this.AffectsProducer(change.FullPath))
        {
            if (this.IsProducerDirectory(change.FullPath))
            {
                this.ResetMonitoring();
            }
            else
            {
                this.InvalidateObservation();
            }
        }
    }

    private void OnArtifactRenamed(object sender, RenamedEventArgs change)
    {
        if (!this.IsActiveWatcher(sender))
        {
            return;
        }

        if (this.AffectsProducer(change.FullPath) || this.AffectsProducer(change.OldFullPath))
        {
            this.ResetMonitoring();
        }
    }

    private void OnWatcherError(object sender, ErrorEventArgs error)
    {
        if (this.IsActiveWatcher(sender))
        {
            this.ResetMonitoring();
        }
    }

    private bool IsActiveWatcher(object sender)
    {
        lock (this.observationSync)
        {
            return !this.disposed && sender is FileSystemWatcher watcher && this.watchers.Contains(watcher);
        }
    }

    private void ResetMonitoring()
    {
        FileSystemWatcher[] retired;
        bool notify;
        lock (this.observationSync)
        {
            retired = this.DetachWatchers();
            notify = this.InvalidateObservationNoLock();
        }

        DisposeWatchers(retired);
        if (notify)
        {
            this.NotifyObservationChanged();
        }
    }

    private bool IsProducerDirectory(string path)
        => this.producerRoots.Any(root => string.Equals(root, path, StringComparison.OrdinalIgnoreCase)
            || root.StartsWith(path.TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase));

    private bool AffectsProducer(string path)
    {
        var extension = Path.GetExtension(path);
        var producerFile = extension.Equals(".dll", StringComparison.OrdinalIgnoreCase)
            || extension.Equals(".exe", StringComparison.OrdinalIgnoreCase)
            || extension.Equals(".json", StringComparison.OrdinalIgnoreCase);
        return this.IsProducerDirectory(path)
            || (producerFile && this.producerRoots.Contains(Path.GetDirectoryName(path), StringComparer.OrdinalIgnoreCase));
    }

    private void InvalidateObservation()
    {
        bool notify;
        lock (this.observationSync)
        {
            notify = this.InvalidateObservationNoLock();
        }

        if (notify)
        {
            this.NotifyObservationChanged();
        }
    }

    private bool InvalidateObservationNoLock()
    {
        if (this.disposed)
        {
            return false;
        }

        this.observationGeneration++;
        if (this.observation.Fingerprint is null && this.observation.Diagnostics.IsEmpty)
        {
            return false;
        }

        this.observation = new(this.observation.Revision + 1, Fingerprint: null, []);
        return true;
    }

    private void PublishObservation(long revision, NativeCompatibilityResult result)
    {
        lock (this.observationSync)
        {
            if (this.disposed || this.observationGeneration != revision)
            {
                return;
            }

            var fingerprint = result.Succeeded && (!cooking || this.watchers.Count != 0) ? result.Artifacts!.Fingerprint : null;
            if (string.Equals(fingerprint, this.observation.Fingerprint, StringComparison.Ordinal)
                && this.observation.Diagnostics.SequenceEqual(result.Diagnostics))
            {
                return;
            }

            this.observation = new(this.observation.Revision + 1, fingerprint, result.Diagnostics);
        }

        this.NotifyObservationChanged();
    }

    private void NotifyObservationChanged()
    {
        foreach (var handler in this.ObservationChanged?.GetInvocationList().Cast<EventHandler>() ?? [])
        {
            _ = Task.Run(() => handler(this, EventArgs.Empty)).ContinueWith(
                static failed => System.Diagnostics.Trace.TraceError("Producer observation listener failed: {0}", failed.Exception),
                CancellationToken.None,
                TaskContinuationOptions.OnlyOnFaulted | TaskContinuationOptions.ExecuteSynchronously,
                TaskScheduler.Default);
        }
    }
}
