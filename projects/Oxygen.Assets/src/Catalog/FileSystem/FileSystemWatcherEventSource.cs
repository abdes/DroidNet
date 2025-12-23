// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;

namespace Oxygen.Assets.Catalog.FileSystem;

internal sealed class FileSystemWatcherEventSource : IFileSystemCatalogEventSource
{
    private readonly FileSystemWatcher watcher;

    public FileSystemWatcherEventSource(string rootPath, string filter)
    {
        ArgumentException.ThrowIfNullOrEmpty(rootPath);
        ArgumentException.ThrowIfNullOrEmpty(filter);

        this.watcher = new FileSystemWatcher(rootPath)
        {
            IncludeSubdirectories = true,
            Filter = filter,
            NotifyFilter = NotifyFilters.FileName
                | NotifyFilters.DirectoryName
                | NotifyFilters.LastWrite
                | NotifyFilters.Size,
            EnableRaisingEvents = true,
        };

        var created = Observable.FromEventPattern<FileSystemEventHandler, FileSystemEventArgs>(
                h => this.watcher.Created += h,
                h => this.watcher.Created -= h)
            .Select(ep => new FileSystemCatalogEvent(FileSystemCatalogEventKind.Created, ep.EventArgs.FullPath));

        var changed = Observable.FromEventPattern<FileSystemEventHandler, FileSystemEventArgs>(
                h => this.watcher.Changed += h,
                h => this.watcher.Changed -= h)
            .Select(ep => new FileSystemCatalogEvent(FileSystemCatalogEventKind.Changed, ep.EventArgs.FullPath));

        var deleted = Observable.FromEventPattern<FileSystemEventHandler, FileSystemEventArgs>(
                h => this.watcher.Deleted += h,
                h => this.watcher.Deleted -= h)
            .Select(ep => new FileSystemCatalogEvent(FileSystemCatalogEventKind.Deleted, ep.EventArgs.FullPath));

        var renamed = Observable.FromEventPattern<RenamedEventHandler, RenamedEventArgs>(
                h => this.watcher.Renamed += h,
                h => this.watcher.Renamed -= h)
            .Select(ep => new FileSystemCatalogEvent(
                FileSystemCatalogEventKind.Renamed,
                ep.EventArgs.FullPath,
                OldFullPath: ep.EventArgs.OldFullPath));

        var rescan = Observable.FromEventPattern<ErrorEventHandler, ErrorEventArgs>(
                h => this.watcher.Error += h,
                h => this.watcher.Error -= h)
            .Select(_ => new FileSystemCatalogEvent(FileSystemCatalogEventKind.RescanRequired, rootPath));

        this.Events = Observable.Merge(created, changed, deleted, renamed, rescan);
    }

    public IObservable<FileSystemCatalogEvent> Events { get; }

    public void Dispose()
        => this.watcher.Dispose();
}
